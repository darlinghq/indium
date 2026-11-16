#include <indium/command-buffer.private.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/render-command-encoder.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/drawable.hpp>
#include <indium/blit-command-encoder.private.hpp>
#include <indium/compute-command-encoder.private.hpp>
#include <indium/dynamic-vk.hpp>

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

void Indium::PrivateCommandBuffer::beginEncoderSegment() {
	if (!_privateDevice->hasBufferShadowTextures()) {
		return;
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

void Indium::PrivateCommandBuffer::endEncoderSegment(const std::vector<std::shared_ptr<Texture>>& used, const std::vector<std::shared_ptr<Texture>>& written) {
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

	if (!toRead.empty()) {
		auto prologue = newSegment();
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
	for (auto segment: _allSegments) {
		DynamicVK::vkFreeCommandBuffers(_privateDevice->device(), _commandPool, 1, &segment);
	}
	_privateCommandQueue->releaseCommandPool(_commandPool);
};

std::shared_ptr<Indium::RenderCommandEncoder> Indium::PrivateCommandBuffer::renderCommandEncoder(const RenderPassDescriptor& descriptor) {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateRenderCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
	}
	return encoder;
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder() {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateBlitCommandEncoder>(shared_from_this());
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
	}
	return encoder;
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder(const BlitPassDescriptor& descriptor) {
	beginEncoderSegment();
	auto encoder = std::make_shared<Indium::PrivateBlitCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
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
	}
	return encoder;
};

std::shared_ptr<Indium::ComputeCommandEncoder> Indium::PrivateCommandBuffer::computeCommandEncoder(DispatchType dispatchType) {
	return computeCommandEncoder(ComputePassDescriptor { {}, dispatchType });
};

void Indium::PrivateCommandBuffer::commit() {
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

	if (DynamicVK::vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	// to keep ourselves alive until we're done
	auto self = shared_from_this();

	// for the event loop
	auto timelineSemaphore = _privateDevice->getWrappedTimelineSemaphore();

	++timelineSemaphore->count;

	std::vector<std::shared_ptr<Texture>> readOnlyTextures;
	std::vector<std::shared_ptr<Texture>> readWriteTextures;
	std::vector<std::shared_ptr<BinarySemaphore>> presentationSemaphores;

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

	// Texture timeline values are handed out below and must reach the queue in the same order,
	// or a submission could wait on a binary semaphore that a later-reserved one signals first.
	std::unique_lock queueLock(_privateDevice->graphicsQueueMutex());

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
	std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;

	VkSemaphoreSubmitInfo signalEventLoopInfo {};
	signalEventLoopInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalEventLoopInfo.semaphore = timelineSemaphore->semaphore;
	signalEventLoopInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	signalEventLoopInfo.value = timelineSemaphore->count;
	signalInfos.push_back(signalEventLoopInfo);

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

	// FIXME: apparently, sometimes, this callback will not be invoked. this is obviously bad because users might want to know when we're done,
	//        but it also leaves the resources for this command buffer tied up, essentially becoming a memory leak.
	//        i've observed this in the cube example, and it happens more than once (because the example display semaphore is exhausted and never signaled).
	// UPDATE: upon further testing, it seems that this only occurs when the view is off-screen/hidden. weird.
	_privateDevice->waitForSemaphore(timelineSemaphore->semaphore, timelineSemaphore->count, [self, timelineSemaphore, extraWaitSemaphores, presentationSemaphores]() {
		std::vector<Handler> handlers;
		{
			std::unique_lock lock(self->_mutex);
			self->_completed = true;
			if (self->_scheduledHandlersFinished) {
				handlers.swap(self->_completedHandlers);
			}
		}

		self->_completedCondvar.notify_all();
		invokeHandlers(handlers, self);
	});

	commandBufferInfos.reserve(_finishedSegments.size() + 1);
	for (auto segment: _finishedSegments) {
		commandBufferInfos.push_back(VkCommandBufferSubmitInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, segment, 0 });
	}
	commandBufferInfos.push_back(VkCommandBufferSubmitInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, nullptr, _commandBuffer, 0 });

	VkSubmitInfo2 info {};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	info.commandBufferInfoCount = commandBufferInfos.size();
	info.pCommandBufferInfos = commandBufferInfos.data();
	info.signalSemaphoreInfoCount = signalInfos.size();
	info.pSignalSemaphoreInfos = signalInfos.data();
	info.waitSemaphoreInfoCount = waitInfos.size();
	info.pWaitSemaphoreInfos = waitInfos.data();

	// FIXME: we need to check if the queue we're submitting on supports the operations encoded in the command buffer.
	//        the Device constructor tries to choose command queues that support as many operations as possible, but it's possible
	//        that a particular device only supports certain operations on certain queues (e.g. maybe it only supports transfer operations
	//        on an exclusive queue that doesn't support graphics or compute).
	if (DynamicVK::vkQueueSubmit2(_privateDevice->graphicsQueue(), 1, &info, VK_NULL_HANDLE) != VK_SUCCESS) {
		// TODO
		abort();
	}

	queueLock.unlock();
	lock.unlock();

	// now that the binary semaphore signals are pending, we can allow them to be used
	for (size_t i = 0; i < readWriteTextures.size(); ++i) {
		if (presentationSemaphores[i]) {
			std::dynamic_pointer_cast<PrivateTexture>(readWriteTextures[i])->endUpdatingPresentationSemaphore();
		}
	}

	std::vector<Handler> scheduledHandlers;
	{
		std::unique_lock callbackLock(_mutex);
		scheduledHandlers.swap(_scheduledHandlers);
	}
	invokeHandlers(scheduledHandlers, self);

	std::vector<Handler> completedHandlers;
	{
		std::unique_lock callbackLock(_mutex);
		_scheduledHandlersFinished = true;
		if (_completed) {
			completedHandlers.swap(_completedHandlers);
		}
	}
	_scheduledCondvar.notify_all();
	invokeHandlers(completedHandlers, self);

	// we can now queue drawables for presentation and they'll be synchronized properly
	for (const auto& drawable: _drawablesToPresent) {
		drawable->present();
	}
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
