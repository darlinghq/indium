#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/command-buffer.private.hpp>

Indium::CommandQueue::~CommandQueue() {};

std::shared_ptr<Indium::Device> Indium::PrivateCommandQueue::device() {
	return _privateDevice;
};

Indium::PrivateCommandQueue::PrivateCommandQueue(std::shared_ptr<PrivateDevice> device):
	_privateDevice(device)
{
	// TODO: support the case of having different graphics and compute queues
	if (_privateDevice->graphicsQueueFamilyIndex() || _privateDevice->computeQueueFamilyIndex()) {
		VkCommandPoolCreateInfo createInfo {};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.queueFamilyIndex = _privateDevice->graphicsQueueFamilyIndex() ? *_privateDevice->graphicsQueueFamilyIndex() : *_privateDevice->computeQueueFamilyIndex();

		if (vkCreateCommandPool(_privateDevice->device(), &createInfo, nullptr, &_commandPool) != VK_SUCCESS) {
			// TODO: handle this in a more C++-friendly way
			abort();
		}

		_supportsGraphics = !!_privateDevice->graphicsQueueFamilyIndex();
		_supportsCompute = _supportsGraphics ? (_privateDevice->computeQueueFamilyIndex() ? *_privateDevice->computeQueueFamilyIndex() == *_privateDevice->graphicsQueueFamilyIndex() : false) : !!_privateDevice->computeQueueFamilyIndex();
	}
};

Indium::PrivateCommandQueue::~PrivateCommandQueue() {
	if (_commandPool) {
		vkDestroyCommandPool(_privateDevice->device(), _commandPool, nullptr);
	}
};

std::shared_ptr<Indium::CommandBuffer> Indium::PrivateCommandQueue::commandBuffer() {
	return std::make_shared<PrivateCommandBuffer>(shared_from_this());
};
