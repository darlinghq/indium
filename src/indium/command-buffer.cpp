#include <indium/command-buffer.private.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/render-command-encoder.private.hpp>
#include <indium/parallel-render-command-encoder.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/drawable.hpp>
#include <indium/blit-command-encoder.private.hpp>
#include <indium/compute-command-encoder.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/event.private.hpp>

#include <algorithm>
#include <condition_variable>

static void invokeHandlers(const std::vector<Indium::CommandBuffer::Handler>& handlers,
                           const std::shared_ptr<Indium::CommandBuffer>& buffer) noexcept {
	for (const auto& handler: handlers) {
		handler(buffer);
	}
}

Indium::CommandBuffer::~CommandBuffer() {};

std::shared_ptr<Indium::CommandQueue> Indium::PrivateCommandBuffer::commandQueue() {
	return _privateCommandQueue;
};
std::shared_ptr<Indium::Device> Indium::PrivateCommandBuffer::device() {
	return _privateDevice;
};

Indium::PrivateCommandBuffer::PrivateCommandBuffer(std::shared_ptr<PrivateCommandQueue> commandQueue):
	_privateCommandQueue(commandQueue)
{
	_privateDevice = _privateCommandQueue->privateDevice();

	_commandPool = _privateCommandQueue->acquireCommandPool();
	_commandBuffer = newSegment();
};

VkCommandBuffer Indium::PrivateCommandBuffer::newSegment() {
	VkCommandBufferAllocateInfo allocInfo {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = _commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer segment = VK_NULL_HANDLE;
	if (DynamicVK::vkAllocateCommandBuffers(_privateDevice->device(), &allocInfo, &segment) != VK_SUCCESS) {
		// TODO: handle this in a more C++-friendly way
		abort();
	}
	_allSegments.push_back(segment);

	VkCommandBufferBeginInfo beginInfo {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (DynamicVK::vkBeginCommandBuffer(segment, &beginInfo) != VK_SUCCESS) {
		// TODO: same
		abort();
	}
	return segment;
};

void Indium::PrivateCommandBuffer::endUntrackedEncoder() {
	_hazards.fullBarrier(_commandBuffer);
};

void Indium::PrivateCommandBuffer::beginEncoderSegment(bool wantsPrologue) {
	// A texture kept equal to a buffer by copies around the encoders is outside what the tracker knows, and the
	// copies are recorded between the encoders, so with one every encoder ends with a barrier on everything.
	bool shadow = _privateDevice->hasBufferShadowTextures();
	_hazardsTracked = !shadow;
	if (!shadow && !wantsPrologue) {
		return;
	}
	if (shadow && _hazards.dirty()) {
		// what the encoders before this one left unordered must be before the copies that come next
		_hazards.fullBarrier(_commandBuffer);
	}
	// what was recorded so far ends here, so that copies for the encoder can go in front of its commands
	if (DynamicVK::vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
		// an unrecoverable failure, like the other command buffer calls that abort
		abort();
	}
	_finishedSegments.push_back(_commandBuffer);
	_commandBuffer = newSegment();
	_encoderSegmentOpen = true;
};

void Indium::PrivateCommandBuffer::endEncoderSegment(const std::vector<std::shared_ptr<Texture>>& used, const std::vector<std::shared_ptr<Texture>>& written, const HazardTracker::Barriers& hazardBarriers) {
	_encoderActive = false;

	// an encoder that began before the first such texture was made has no segment of its own and cannot have
	// copies recorded in front of it
	if (!_encoderSegmentOpen) {
		return;
	}
	_encoderSegmentOpen = false;

	const auto collect = [](const std::vector<std::shared_ptr<Texture>>& textures, std::vector<std::shared_ptr<PrivateTexture>>& out) {
		for (const auto& texture: textures) {
			auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
			if (!privateTexture || !privateTexture->isBufferShadow()) {
				continue;
			}
			// a view and its texture are one image
			std::shared_ptr<Texture> root = privateTexture;
			while (auto parent = root->parentTexture()) {
				root = parent;
			}
			auto privateRoot = std::dynamic_pointer_cast<PrivateTexture>(root);
			if (std::find(out.begin(), out.end(), privateRoot) == out.end()) {
				out.push_back(privateRoot);
			}
		}
	};
	std::vector<std::shared_ptr<PrivateTexture>> toRead;
	std::vector<std::shared_ptr<PrivateTexture>> toWrite;
	collect(used, toRead);
	collect(written, toRead);
	collect(written, toWrite);
	for (const auto& texture: toRead) {
		if (texture->mayBeWrittenByShader() && std::find(toWrite.begin(), toWrite.end(), texture) == toWrite.end()) {
			toWrite.push_back(texture);
		}
	}

	if (DynamicVK::vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
		// an unrecoverable failure, like the other command buffer calls that abort
		abort();
	}
	auto encoderSegment = _commandBuffer;

	if (!toRead.empty() || !hazardBarriers.empty()) {
		auto prologue = newSegment();
		HazardTracker::record(prologue, hazardBarriers);
		for (const auto& texture: toRead) {
			texture->recordBufferToImage(prologue);
		}
		DynamicVK::vkEndCommandBuffer(prologue);
		_finishedSegments.push_back(prologue);
	}
	_finishedSegments.push_back(encoderSegment);
	if (!toWrite.empty()) {
		auto epilogue = newSegment();
		for (const auto& texture: toWrite) {
			texture->recordImageToBuffer(epilogue);
		}
		DynamicVK::vkEndCommandBuffer(epilogue);
		_finishedSegments.push_back(epilogue);
	}

	// whatever is recorded between this encoder and the next
	_commandBuffer = newSegment();
};

Indium::PrivateCommandBuffer::~PrivateCommandBuffer() {
	if (!_committed) {
		// one that was enqueued must not hold the queue back
		_privateCommandQueue->abandon(this);
	}
	if (_commandPool != VK_NULL_HANDLE) {
		freeSegments(_commandPool, _allSegments);
	}
};

std::shared_ptr<Indium::RenderCommandEncoder> Indium::PrivateCommandBuffer::renderCommandEncoder(const RenderPassDescriptor& descriptor) {
	// the barriers for what the pass reads cannot be recorded once it has begun: they go in front of it
	beginEncoderSegment(true);
	auto encoder = std::make_shared<Indium::PrivateRenderCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
		_encoderActive = true;
	}
	return encoder;
};

std::shared_ptr<Indium::ParallelRenderCommandEncoder> Indium::PrivateCommandBuffer::parallelRenderCommandEncoder(const RenderPassDescriptor& descriptor) {
	// nothing is recorded until the pass ends, but it is an encoder in the meantime for what asks whether one is active
	_encoderActive = true;
	return std::make_shared<Indium::PrivateParallelRenderCommandEncoder>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder() {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateBlitCommandEncoder>(shared_from_this());
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
		_encoderActive = true;
	}
	return encoder;
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder(const BlitPassDescriptor& descriptor) {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateBlitCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
		_encoderActive = true;
	}
	return encoder;
};

std::shared_ptr<Indium::ComputeCommandEncoder> Indium::PrivateCommandBuffer::computeCommandEncoder() {
	return computeCommandEncoder(ComputePassDescriptor {});
};

std::shared_ptr<Indium::ComputeCommandEncoder> Indium::PrivateCommandBuffer::computeCommandEncoder(const ComputePassDescriptor& descriptor) {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateComputeCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
		_encoderActive = true;
	}
	return encoder;
};

std::shared_ptr<Indium::ComputeCommandEncoder> Indium::PrivateCommandBuffer::computeCommandEncoder(DispatchType dispatchType) {
	return computeCommandEncoder(ComputePassDescriptor { {}, dispatchType });
};

void Indium::PrivateCommandBuffer::encodeSignalEvent(const std::shared_ptr<Event>& event, uint64_t value) {
	addEventOperation(event, value, true);
};

void Indium::PrivateCommandBuffer::encodeWaitForEvent(const std::shared_ptr<Event>& event, uint64_t value) {
	addEventOperation(event, value, false);
};

void Indium::PrivateCommandBuffer::addEventOperation(const std::shared_ptr<Event>& event, uint64_t value, bool signal) {
	auto eventBase = std::dynamic_pointer_cast<PrivateEventBase>(event);
	if (!eventBase || eventBase->core()->device() != _privateDevice) {
		throw std::invalid_argument("The event does not belong to the command buffer's device");
	}

	std::scoped_lock lock(_mutex);
	if (_committed) {
		throw std::logic_error("The command buffer has been committed");
	}
	if (_encoderActive) {
		throw std::logic_error("An encoder is still active");
	}

	// the operation takes effect between the commands recorded so far and the ones recorded next, so they end up
	// in command buffers of their own
	if (DynamicVK::vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
		// an unrecoverable failure, like the other command buffer calls that abort
		abort();
	}
	_finishedSegments.push_back(_commandBuffer);
	_commandBuffer = newSegment();
	_eventOperations.push_back(EventOperation { eventBase->core(), value, signal, _finishedSegments.size() });
};

void Indium::PrivateCommandBuffer::commit() {
	// buffers that stand in for the caller's memory take the bytes it holds now
	_privateDevice->syncShadowBuffers(true);

	{
		std::unique_lock lock(_mutex);

		_committed = true;

		for (const auto& encoder: _commandEncoders) {
			if (auto renderEncoder = std::dynamic_pointer_cast<PrivateRenderCommandEncoder>(encoder)) {
				for (const auto& texture: renderEncoder->readOnlyTextures()) {
					if (auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture)) {
						privateTexture->precommit(shared_from_this());
					}
				}
				for (const auto& texture: renderEncoder->readWriteTextures()) {
					if (auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture)) {
						privateTexture->precommit(shared_from_this());
					}
				}
			} else if (auto blitEncoder = std::dynamic_pointer_cast<PrivateBlitCommandEncoder>(encoder)) {
				for (const auto& texture: blitEncoder->readOnlyTextures()) {
					if (auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture)) {
						privateTexture->precommit(shared_from_this());
					}
				}
				for (const auto& texture: blitEncoder->readWriteTextures()) {
					if (auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture)) {
						privateTexture->precommit(shared_from_this());
					}
				}
			}

			// TODO: same for other encoders
		}

		// The batches of a command buffer with event operations are separate queue submissions, which a queue need
		// not finish in order, but the command buffer is completed by the signal of the last one. The barrier
		// that ends it covers every earlier command of the queue, so that signal comes after theirs.
		if (!_eventOperations.empty()) {
			_hazards.fullBarrier(_commandBuffer);
		}

		if (DynamicVK::vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}

	// the queue's submission thread submits it once everything it waits for has happened and the command buffers
	// enqueued before it have been submitted
	_privateCommandQueue->commit(shared_from_this());
};

void Indium::PrivateCommandBuffer::enqueue() {
	{
		std::scoped_lock lock(_mutex);
		if (_committed) {
			return;
		}
	}
	_privateCommandQueue->enqueue(shared_from_this());
};

bool Indium::PrivateCommandBuffer::submitReady(std::vector<std::function<void()>>& afterSubmit) {
	std::unique_lock lock(_mutex);

	// Texture timeline values are handed out below and must reach the queue in the same order,
	// or a submission could wait on a binary semaphore that a later-reserved one signals first.
	std::unique_lock queueLock(_privateDevice->graphicsQueueMutex());

	if (!_submission) {
		prepareSubmission();
	}
	auto& submission = *_submission;

	// A queue submission that waits for an event value nothing has signaled yet holds up everything submitted
	// after it on the Vulkan queue, including the work that is to signal it from another command queue. So
	// a batch is only submitted once the values it waits for have been reached.
	size_t end = submission.next;
	for (; end < submission.batches.size(); ++end) {
		bool blocked = false;
		for (const auto& [core, value]: submission.batches[end].eventWaits) {
			if (core->reached(value)) {
				continue;
			}
			blocked = true;
			if (!submission.waiterRegistered) {
				submission.waiterRegistered = true;
				auto self = shared_from_this();
				_privateDevice->waitForSemaphore(core->semaphore(), value, [self, core = core]() {
					{
						std::scoped_lock waiterLock(self->_mutex);
						if (self->_submission) {
							self->_submission->waiterRegistered = false;
						}
					}
					self->_privateCommandQueue->wakeSubmission();
				});
			}
			break;
		}
		if (blocked) {
			break;
		}
	}

	if (end > submission.next) {
		// Host writes to textures (replaceRegion) made since the last submission are copied into their images
		// before this command buffer's work, so that it sees them.
		_privateDevice->textureUploader().flushLocked();

		std::vector<VkSubmitInfo2> infos;
		infos.reserve(end - submission.next);
		for (size_t i = submission.next; i < end; ++i) {
			auto& batch = submission.batches[i];
			// A signal that does not raise the value (another command buffer or the host got there first) is
			// dropped; the order of the submissions decides.
			for (const auto& [core, value]: batch.eventSignals) {
				if (core->reserveDeviceSignal(value)) {
					VkSemaphoreSubmitInfo signalInfo {};
					signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
					signalInfo.semaphore = core->semaphore();
					signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
					signalInfo.value = value;
					batch.signals.push_back(signalInfo);
				}
			}
			VkSubmitInfo2 info {};
			info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
			info.commandBufferInfoCount = batch.commandBuffers.size();
			info.pCommandBufferInfos = batch.commandBuffers.data();
			info.signalSemaphoreInfoCount = batch.signals.size();
			info.pSignalSemaphoreInfos = batch.signals.data();
			info.waitSemaphoreInfoCount = batch.waits.size();
			info.pWaitSemaphoreInfos = batch.waits.data();
			infos.push_back(info);
		}

		// FIXME: we need to check if the queue we're submitting on supports the operations encoded in the command buffer.
		//        the Device constructor tries to choose command queues that support as many operations as possible, but it's possible
		//        that a particular device only supports certain operations on certain queues (e.g. maybe it only supports transfer operations
		//        on an exclusive queue that doesn't support graphics or compute).
		if (DynamicVK::vkQueueSubmit2(_privateDevice->graphicsQueue(), infos.size(), infos.data(), VK_NULL_HANDLE) != VK_SUCCESS) {
			// TODO
			abort();
		}
		submission.next = end;
	}

	if (submission.next < submission.batches.size()) {
		return false;
	}

	auto self = shared_from_this();
	auto readWriteTextures = std::move(submission.readWriteTextures);
	auto presentationSemaphores = std::move(submission.presentationSemaphores);
	_submission.reset();

	queueLock.unlock();
	lock.unlock();

	afterSubmit.push_back([self, readWriteTextures = std::move(readWriteTextures), presentationSemaphores = std::move(presentationSemaphores)]() {
		self->finishSubmission(readWriteTextures, presentationSemaphores);
	});
	return true;
};

void Indium::PrivateCommandBuffer::prepareSubmission() {
	auto submissionPtr = std::make_unique<Submission>();
	auto& submission = *submissionPtr;

	// to keep ourselves alive until we're done
	auto self = shared_from_this();

	std::vector<std::shared_ptr<Texture>> readOnlyTextures;
	std::vector<std::shared_ptr<Texture>>& readWriteTextures = submission.readWriteTextures;
	std::vector<std::shared_ptr<BinarySemaphore>>& presentationSemaphores = submission.presentationSemaphores;

	for (const auto& encoder: _commandEncoders) {
		if (auto renderEncoder = std::dynamic_pointer_cast<PrivateRenderCommandEncoder>(encoder)) {
			for (const auto& tex : renderEncoder->readOnlyTextures()) {
				if (std::find(readOnlyTextures.begin(), readOnlyTextures.end(), tex) == readOnlyTextures.end()) {
					readOnlyTextures.push_back(tex);
				}
			}
			for (const auto& tex : renderEncoder->readWriteTextures()) {
				if (std::find(readWriteTextures.begin(), readWriteTextures.end(), tex) == readWriteTextures.end()) {
					readWriteTextures.push_back(tex);
				}
			}
		} else if (auto blitEncoder = std::dynamic_pointer_cast<PrivateBlitCommandEncoder>(encoder)) {
			for (const auto& tex : blitEncoder->readOnlyTextures()) {
				if (std::find(readOnlyTextures.begin(), readOnlyTextures.end(), tex) == readOnlyTextures.end()) {
					readOnlyTextures.push_back(tex);
				}
			}
			for (const auto& tex : blitEncoder->readWriteTextures()) {
				if (std::find(readWriteTextures.begin(), readWriteTextures.end(), tex) == readWriteTextures.end()) {
					readWriteTextures.push_back(tex);
				}
			}
		}

		// TODO: implement this for other encoders (we need to synchronize those texture accesses as well)
	}

	// A view shares its synchronization state with the texture it was made from, so acquiring a
	// texture and a view of it separately would make this submission wait for its own signal.
	// Synchronize each underlying texture once, and only as written if any use of it writes.
	const auto rootTexture = [](std::shared_ptr<Texture> texture) {
		while (auto parent = texture->parentTexture()) {
			texture = parent;
		}
		return texture;
	};
	const auto toRoots = [&](std::vector<std::shared_ptr<Texture>>& textures) {
		std::vector<std::shared_ptr<Texture>> roots;
		for (const auto& texture: textures) {
			auto root = rootTexture(texture);
			if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
				roots.push_back(root);
			}
		}
		textures = std::move(roots);
	};
	toRoots(readWriteTextures);
	toRoots(readOnlyTextures);
	readOnlyTextures.erase(std::remove_if(readOnlyTextures.begin(), readOnlyTextures.end(), [&](const auto& texture) {
		return std::find(readWriteTextures.begin(), readWriteTextures.end(), texture) != readWriteTextures.end();
	}), readOnlyTextures.end());

	for (const auto& texture: readWriteTextures) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
		// Only textures something else presents consume the presentation semaphore. Signaling
		// one that is never waited on would leave it signaled when it is reused.
		std::shared_ptr<BinarySemaphore> sema;
		if (!std::dynamic_pointer_cast<ConcreteTexture>(privateTexture)) {
			sema = _privateDevice->getWrappedBinarySemaphore(privateTexture->needsExportablePresentationSemaphore());
			privateTexture->beginUpdatingPresentationSemaphore(sema);
		}
		presentationSemaphores.push_back(sema);
	}

	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	std::vector<std::shared_ptr<BinarySemaphore>> extraWaitSemaphores;

	const auto handleTextureSemaphores = [&](const std::shared_ptr<PrivateTexture>& privateTexture) {
		uint64_t waitValue;
		std::shared_ptr<BinarySemaphore> extraWaitSema;
		uint64_t signalValue;
		const auto& sema = privateTexture->acquire(waitValue, extraWaitSema, signalValue);

		if (extraWaitSema) {
			extraWaitSemaphores.push_back(extraWaitSema);
			VkSemaphoreSubmitInfo extraWaitInfo {};
			extraWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			extraWaitInfo.semaphore = extraWaitSema->semaphore;
			extraWaitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			waitInfos.push_back(extraWaitInfo);
		}

		VkSemaphoreSubmitInfo waitInfo {};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waitInfo.semaphore = sema.semaphore;
		waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		waitInfo.value = waitValue;
		waitInfos.push_back(waitInfo);

		if (signalValue != 0) {
			VkSemaphoreSubmitInfo signalInfo {};
			signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			signalInfo.semaphore = sema.semaphore;
			signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			signalInfo.value = signalValue;
			signalInfos.push_back(signalInfo);
		}
	};

	for (const auto& texture: readOnlyTextures) {
		handleTextureSemaphores(std::dynamic_pointer_cast<PrivateTexture>(texture));
	}

	for (size_t i = 0; i < readWriteTextures.size(); ++i) {
		const auto& texture = readWriteTextures[i];
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
		const auto& presentSema = presentationSemaphores[i];

		if (presentSema) {
			VkSemaphoreSubmitInfo signalInfo {};
			signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			signalInfo.semaphore = presentSema->semaphore;
			signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			signalInfos.push_back(signalInfo);
		}

		handleTextureSemaphores(privateTexture);
	}

	// One batch per stretch between event operations. A signal ends its batch, so that nothing recorded after it
	// delays it; a wait starts one, so that it only holds back what is recorded after it.
	std::vector<VkCommandBuffer> segments = _finishedSegments;
	segments.push_back(_commandBuffer);

	auto& batches = submission.batches;
	batches.emplace_back();
	size_t nextOperation = 0;
	for (size_t i = 0; i < segments.size(); ++i) {
		for (; nextOperation < _eventOperations.size() && _eventOperations[nextOperation].position <= i; ++nextOperation) {
			const auto& operation = _eventOperations[nextOperation];

			if (operation.signal) {
				// whether it is submitted is decided when its batch is
				batches.back().eventSignals.emplace_back(operation.core, operation.value);
				batches.back().closed = true;
			} else {
				if (batches.back().closed || !batches.back().commandBuffers.empty() || !batches.back().signals.empty() || !batches.back().eventSignals.empty()) {
					batches.emplace_back();
				}
				VkSemaphoreSubmitInfo info {};
				info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				info.semaphore = operation.core->semaphore();
				info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				info.value = operation.value;
				batches.back().waits.push_back(info);
				batches.back().eventWaits.emplace_back(operation.core, operation.value);
			}
		}

		if (batches.back().closed) {
			batches.emplace_back();
		}
		batches.back().commandBuffers.push_back(VkCommandBufferSubmitInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, segments[i], 0 });
	}

	batches.front().waits.insert(batches.front().waits.begin(), waitInfos.begin(), waitInfos.end());
	batches.back().signals.insert(batches.back().signals.end(), signalInfos.begin(), signalInfos.end());

	// the queue watches this signal of the last batch (see commit() for why it comes after the others): its
	// timeline semaphore reaches the value once the GPU has finished the command buffer
	VkSemaphoreSubmitInfo completionInfo {};
	completionInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	completionInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	_privateCommandQueue->nextCompletion(completionInfo.semaphore, _completionValue);
	completionInfo.value = _completionValue;
	batches.back().signals.push_back(completionInfo);

	// what the submission waits on and signals has to outlive the GPU's use of it
	for (const auto& semaphore: extraWaitSemaphores) {
		_retainedSemaphores.push_back(semaphore);
	}
	for (const auto& semaphore: presentationSemaphores) {
		if (semaphore) {
			_retainedSemaphores.push_back(semaphore);
		}
	}

	_submission = std::move(submissionPtr);
};

void Indium::PrivateCommandBuffer::finishSubmission(const std::vector<std::shared_ptr<Texture>>& readWriteTextures, const std::vector<std::shared_ptr<BinarySemaphore>>& presentationSemaphores) {
	// now that the binary semaphore signals are pending, we can allow them to be used
	for (size_t i = 0; i < readWriteTextures.size(); ++i) {
		if (presentationSemaphores[i]) {
			std::dynamic_pointer_cast<PrivateTexture>(readWriteTextures[i])->endUpdatingPresentationSemaphore();
		}
	}

	// we can now queue drawables for presentation and they'll be synchronized properly
	std::vector<std::shared_ptr<Drawable>> drawables;
	{
		std::scoped_lock lock(_mutex);
		drawables = _drawablesToPresent;
	}
	for (const auto& drawable: drawables) {
		drawable->present();
	}
};

void Indium::PrivateCommandBuffer::markCompleted() {
	// the caller's memory gets what the GPU wrote before anything learns that the command buffer is done
	_privateDevice->syncShadowBuffers(false);
	{
		std::unique_lock lock(_mutex);
		_completed = true;
	}
	_completedCondvar.notify_all();
};

void Indium::PrivateCommandBuffer::runScheduledHandlers() {
	auto self = shared_from_this();

	std::vector<Handler> scheduledHandlers;
	{
		std::unique_lock lock(_mutex);
		scheduledHandlers.swap(_scheduledHandlers);
	}
	invokeHandlers(scheduledHandlers, self);

	{
		std::unique_lock lock(_mutex);
		_scheduledHandlersFinished = true;
	}
	_scheduledCondvar.notify_all();
};

bool Indium::PrivateCommandBuffer::retirable() {
	std::unique_lock lock(_mutex);
	return _completed && _scheduledHandlersFinished;
};

void Indium::PrivateCommandBuffer::retire() {
	auto self = shared_from_this();

	std::vector<Handler> completedHandlers;
	{
		std::unique_lock lock(_mutex);
		completedHandlers.swap(_completedHandlers);
	}
	invokeHandlers(completedHandlers, self);

	// The GPU is done with everything the command buffer recorded: release what it held for the GPU's sake, though
	// the client may keep the command buffer itself for as long as it likes.
	std::vector<std::shared_ptr<CommandEncoder>> encoders;
	std::vector<std::shared_ptr<Drawable>> drawables;
	std::vector<EventOperation> eventOperations;
	std::vector<VkCommandBuffer> segments;
	std::vector<std::shared_ptr<BinarySemaphore>> semaphores;
	std::vector<std::shared_ptr<Resource>> resources;
	VkCommandPool pool = VK_NULL_HANDLE;
	{
		std::unique_lock lock(_mutex);
		semaphores.swap(_retainedSemaphores);
		resources.swap(_retainedResources);
		encoders.swap(_commandEncoders);
		drawables.swap(_drawablesToPresent);
		eventOperations.swap(_eventOperations);
		segments.swap(_allSegments);
		_finishedSegments.clear();
		_commandBuffer = VK_NULL_HANDLE;
		std::swap(pool, _commandPool);
	}
	if (pool != VK_NULL_HANDLE) {
		freeSegments(pool, segments);
	}
	// what these held may have been the last reference to the device: nothing is locked here
	encoders.clear();
	drawables.clear();
	eventOperations.clear();
	semaphores.clear();
	resources.clear();
};

void Indium::PrivateCommandBuffer::freeSegments(VkCommandPool pool, const std::vector<VkCommandBuffer>& segments) {
	for (auto segment: segments) {
		DynamicVK::vkFreeCommandBuffers(_privateDevice->device(), pool, 1, &segment);
	}
	_privateCommandQueue->releaseCommandPool(pool);
};

void Indium::PrivateCommandBuffer::presentDrawable(std::shared_ptr<Drawable> drawable) {
	std::scoped_lock lock(_mutex);
	if (_committed) {
		// TODO
		abort();
	}
	_drawablesToPresent.push_back(drawable);
};

void Indium::PrivateCommandBuffer::addScheduledHandlerLocked(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	_scheduledHandlers.push_back(handler);
};

void Indium::PrivateCommandBuffer::addCompletedHandlerLocked(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	_completedHandlers.push_back(handler);
};

void Indium::PrivateCommandBuffer::addScheduledHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	std::scoped_lock lock(_mutex);
	if (_committed) {
		// TODO
		abort();
	}
	addScheduledHandlerLocked(handler);
};

void Indium::PrivateCommandBuffer::addCompletedHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	std::scoped_lock lock(_mutex);
	if (_committed) {
		// TODO
		abort();
	}
	addCompletedHandlerLocked(handler);
};

void Indium::PrivateCommandBuffer::waitUntilCompleted() {
	std::unique_lock lock(_mutex);

	while (!_completed) {
		_completedCondvar.wait(lock);
	}
};

void Indium::PrivateCommandBuffer::waitUntilScheduled() {
	std::unique_lock lock(_mutex);
	while (!_scheduledHandlersFinished) {
		_scheduledCondvar.wait(lock);
	}
};
