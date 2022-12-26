#include <indium/command-buffer.private.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/render-command-encoder.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/drawable.hpp>
#include <indium/blit-command-encoder.private.hpp>
#include <indium/compute-command-encoder.private.hpp>

#include <condition_variable>

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

	VkCommandBufferAllocateInfo allocInfo {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = _privateCommandQueue->commandPool();
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(_privateDevice->device(), &allocInfo, &_commandBuffer) != VK_SUCCESS) {
		// TODO: handle this in a more C++-friendly way
		abort();
	}

	VkCommandBufferBeginInfo beginInfo {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (vkBeginCommandBuffer(_commandBuffer, &beginInfo) != VK_SUCCESS) {
		// TODO: same
		abort();
	}
};

Indium::PrivateCommandBuffer::~PrivateCommandBuffer() {
	vkFreeCommandBuffers(_privateDevice->device(), _privateCommandQueue->commandPool(), 1, &_commandBuffer);
};

std::shared_ptr<Indium::RenderCommandEncoder> Indium::PrivateCommandBuffer::renderCommandEncoder(const RenderPassDescriptor& descriptor) {
	auto encoder = std::make_shared<Indium::PrivateRenderCommandEncoder>(shared_from_this(), descriptor);
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
	}
	return encoder;
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder() {
	auto encoder = std::make_shared<Indium::PrivateBlitCommandEncoder>(shared_from_this());
	{
		std::scoped_lock lock(_mutex);
		_commandEncoders.push_back(encoder);
	}
	return encoder;
};

std::shared_ptr<Indium::BlitCommandEncoder> Indium::PrivateCommandBuffer::blitCommandEncoder(const BlitPassDescriptor& descriptor) {
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
		}

		// TODO: same for other encoders
	}

	if (vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS) {
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
			auto& readOnly = renderEncoder->readOnlyTextures();
			auto& readWrite = renderEncoder->readWriteTextures();
			readOnlyTextures.insert(readOnlyTextures.end(), readOnly.begin(), readOnly.end());
			readWriteTextures.insert(readWriteTextures.end(), readWrite.begin(), readWrite.end());
		}

		// TODO: implement this for other encoders (we need to synchronize those texture accesses as well)
	}

	for (const auto& texture: readWriteTextures) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
		auto sema = _privateDevice->getWrappedBinarySemaphore(privateTexture->needsExportablePresentationSemaphore());
		presentationSemaphores.push_back(sema);
		privateTexture->beginUpdatingPresentationSemaphore(sema);
	}

	std::vector<VkSemaphoreSubmitInfo> signalInfos;
	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	VkCommandBufferSubmitInfo commandBufferInfo {};

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

		VkSemaphoreSubmitInfo signalInfo {};
		signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signalInfo.semaphore = presentSema->semaphore;
		signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		signalInfos.push_back(signalInfo);

		handleTextureSemaphores(privateTexture);
	}

	// FIXME: apparently, sometimes, this callback will not be invoked. this is obviously bad because users might want to know when we're done,
	//        but it also leaves the resources for this command buffer tied up, essentially becoming a memory leak.
	//        i've observed this in the cube example, and it happens more than once (because the example display semaphore is exhausted and never signaled).
	// UPDATE: upon further testing, it seems that this only occurs when the view is off-screen/hidden. weird.
	_privateDevice->waitForSemaphore(timelineSemaphore->semaphore, timelineSemaphore->count, [self, timelineSemaphore, extraWaitSemaphores, presentationSemaphores]() {
		{
			std::unique_lock lock(self->_mutex);
			self->_completed = true;
		}

		self->_completedCondvar.notify_all();

		// TODO: invoke scheduled handlers when the command buffer is scheduled instead of completed
		for (const auto& handler: self->_scheduledHandlers) {
			handler(self);
		}
		for (const auto& handler: self->_completedHandlers) {
			handler(self);
		}
	});

	commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandBufferInfo.commandBuffer = _commandBuffer;

	VkSubmitInfo2 info {};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = &commandBufferInfo;
	info.signalSemaphoreInfoCount = signalInfos.size();
	info.pSignalSemaphoreInfos = signalInfos.data();
	info.waitSemaphoreInfoCount = waitInfos.size();
	info.pWaitSemaphoreInfos = waitInfos.data();

	// FIXME: we need to check if the queue we're submitting on supports the operations encoded in the command buffer.
	//        the Device constructor tries to choose command queues that support as many operations as possible, but it's possible
	//        that a particular device only supports certain operations on certain queues (e.g. maybe it only supports transfer operations
	//        on an exclusive queue that doesn't support graphics or compute).
	if (vkQueueSubmit2(_privateDevice->graphicsQueue(), 1, &info, VK_NULL_HANDLE) != VK_SUCCESS) {
		// TODO
		abort();
	}

	lock.unlock();

	// now that the binary semaphore signals are pending, we can allow them to be used
	for (const auto& texture: readWriteTextures) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
		privateTexture->endUpdatingPresentationSemaphore();
	}

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

void Indium::PrivateCommandBuffer::addScheduledHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	std::scoped_lock lock(_mutex);
	if (_committed) {
		// TODO
		abort();
	}
	_scheduledHandlers.push_back(handler);
};

void Indium::PrivateCommandBuffer::addCompletedHandler(std::function<void(std::shared_ptr<CommandBuffer>)> handler) {
	std::scoped_lock lock(_mutex);
	if (_committed) {
		// TODO
		abort();
	}
	_completedHandlers.push_back(handler);
};

void Indium::PrivateCommandBuffer::waitUntilCompleted() {
	std::unique_lock lock(_mutex);

	while (!_completed) {
		_completedCondvar.wait(lock);
	}
};
