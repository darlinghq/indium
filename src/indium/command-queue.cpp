#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <stdexcept>

Indium::CommandQueue::~CommandQueue() {};

std::shared_ptr<Indium::Device> Indium::PrivateCommandQueue::device() {
	return _privateDevice;
};

Indium::PrivateCommandQueue::PrivateCommandQueue(std::shared_ptr<PrivateDevice> device):
	_privateDevice(device)
{
	// TODO: support the case of having different graphics and compute queues
	if (_privateDevice->graphicsQueueFamilyIndex() || _privateDevice->computeQueueFamilyIndex()) {
		_queueFamilyIndex = _privateDevice->graphicsQueueFamilyIndex() ? *_privateDevice->graphicsQueueFamilyIndex() : *_privateDevice->computeQueueFamilyIndex();

		_supportsGraphics = !!_privateDevice->graphicsQueueFamilyIndex();
		_supportsCompute = _supportsGraphics ? (_privateDevice->computeQueueFamilyIndex() ? *_privateDevice->computeQueueFamilyIndex() == *_privateDevice->graphicsQueueFamilyIndex() : false) : !!_privateDevice->computeQueueFamilyIndex();
	}
};

Indium::PrivateCommandQueue::~PrivateCommandQueue() {
	// every command buffer holds a reference to its queue, so no pool is leased any more
	for (auto commandPool: _idleCommandPools) {
		DynamicVK::vkDestroyCommandPool(_privateDevice->device(), commandPool, nullptr);
	}
};

VkCommandPool Indium::PrivateCommandQueue::acquireCommandPool() {
	{
		std::scoped_lock lock(_commandPoolsMutex);
		if (!_idleCommandPools.empty()) {
			auto commandPool = _idleCommandPools.back();
			_idleCommandPools.pop_back();
			return commandPool;
		}
	}

	if (!_queueFamilyIndex) {
		throw std::runtime_error("Command queue has no queue family to record for");
	}

	VkCommandPoolCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = *_queueFamilyIndex;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateCommandPool(_privateDevice->device(), &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a command pool");
	}
	return commandPool;
};

void Indium::PrivateCommandQueue::releaseCommandPool(VkCommandPool commandPool) {
	// the pool's command buffers have been freed, so resetting returns their memory to it
	DynamicVK::vkResetCommandPool(_privateDevice->device(), commandPool, 0);
	std::scoped_lock lock(_commandPoolsMutex);
	_idleCommandPools.push_back(commandPool);
};

std::shared_ptr<Indium::CommandBuffer> Indium::PrivateCommandQueue::commandBuffer() {
	return std::make_shared<PrivateCommandBuffer>(shared_from_this());
};
