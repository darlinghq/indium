#include <indium/device.private.hpp>
#include <indium/instance.private.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/render-pipeline.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/library.private.hpp>
#include <indium/sampler.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/depth-stencil.private.hpp>
#include <indium/compute-pipeline.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <iridium/iridium.hpp>

#include <set>
#include <thread>
#include <unordered_set>

#include <cstring>

std::vector<std::shared_ptr<Indium::PrivateDevice>> Indium::globalDeviceList;

void Indium::initGlobalDeviceList() {
	std::vector<VkPhysicalDevice> physicalDevices;
	uint32_t count = 0;

	auto result = DynamicVK::vkEnumeratePhysicalDevices(globalInstance, &count, nullptr);
	if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
		// TODO
		abort();
	}

	physicalDevices.resize(count);
	result = DynamicVK::vkEnumeratePhysicalDevices(globalInstance, &count, physicalDevices.data());
	if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
		// TODO
		abort();
	}

	for (auto&& device: physicalDevices) {
		VkPhysicalDeviceProperties props;
		DynamicVK::vkGetPhysicalDeviceProperties(device, &props);

		if (VK_API_VERSION_VARIANT(props.apiVersion) != 0 || props.apiVersion < VK_API_VERSION_1_3) {
			// unsupported device
			continue;
		}

		VkPhysicalDeviceFeatures2 features {};
		VkPhysicalDeviceVulkan11Features features11 {};
		VkPhysicalDeviceVulkan12Features features12 {};
		VkPhysicalDeviceVulkan13Features features13 {};

		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &features11;

		features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		features11.pNext = &features12;

		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.pNext = &features13;

		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

		DynamicVK::vkGetPhysicalDeviceFeatures2(device, &features);

		if (!features12.timelineSemaphore) {
			// unsupported device
			continue;
		}

		globalDeviceList.push_back(std::make_shared<PrivateDevice>(std::move(device)));
	}
};

void Indium::finitGlobalDeviceList() {
	globalDeviceList.clear();
};

Indium::Device::~Device() {};

Indium::PrivateDevice::PrivateDevice(VkPhysicalDevice physicalDevice):
	_physicalDevice(physicalDevice)
{
	DynamicVK::vkGetPhysicalDeviceProperties(_physicalDevice, &_properties);

	std::vector<VkQueueFamilyProperties> queueFamilies;
	uint32_t count = 0;

	DynamicVK::vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &count, nullptr);
	queueFamilies.resize(count);
	DynamicVK::vkGetPhysicalDeviceQueueFamilyProperties(_physicalDevice, &count, queueFamilies.data());

	DynamicVK::vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &_memoryProperties);

	uint32_t index = 0;
	size_t maxSupportedSameQueueFamily = 0;
	for (const auto& queueFamily: queueFamilies) {
		bool supportsGraphics = false;
		bool supportsCompute = false;
		bool supportsTransfer = false;
		bool supportsPresent = false;
		size_t supported = 0;

		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			supportsGraphics = true;
			++supported;
		}

		if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
			supportsCompute = true;
			++supported;
		}

		if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
			supportsTransfer = true;
			++supported;
		}

		if (supportsGraphics && !_graphicsQueueFamilyIndex) {
			_graphicsQueueFamilyIndex = index;
		}

		if (supportsCompute && !_computeQueueFamilyIndex) {
			_computeQueueFamilyIndex = index;
		}

		if (supportsTransfer && !_transferQueueFamilyIndex) {
			_transferQueueFamilyIndex = index;
		}

		// we want to use the same queue family as much as possible to conserve resources,
		// so if this queue family supports more operations than the currently saved queue families,
		// we want to use this queue family instead of those.

		if (supported > maxSupportedSameQueueFamily) {
			maxSupportedSameQueueFamily = supported;

			if (supportsGraphics) {
				_graphicsQueueFamilyIndex = index;
			}

			if (supportsCompute) {
				_computeQueueFamilyIndex = index;
			}

			if (supportsTransfer) {
				_transferQueueFamilyIndex = index;
			}
		}

		if (supported == 3) {
			// we've found the best queue family: one that supports everything.
			// we can stop looking now.
			break;
		}

		++index;
	}

	// TODO: we should try to ensure that the same queue is used for graphics and compute
	//       to make it to create CommandBuffers that can do either one.

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> queueFamilyIndices;
	std::vector<float> queuePriorities = { 1.0f };

	if (_graphicsQueueFamilyIndex) {
		queueFamilyIndices.insert(*_graphicsQueueFamilyIndex);
	}

	if (_computeQueueFamilyIndex) {
		queueFamilyIndices.insert(*_computeQueueFamilyIndex);
	}

	if (_transferQueueFamilyIndex) {
		queueFamilyIndices.insert(*_transferQueueFamilyIndex);
	}

	for (const auto& index: queueFamilyIndices) {
		VkDeviceQueueCreateInfo createInfo {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		createInfo.queueFamilyIndex = index;
		createInfo.pQueuePriorities = queuePriorities.data();
		createInfo.queueCount = queuePriorities.size();
		queueCreateInfos.push_back(createInfo);
	}

	VkPhysicalDeviceFeatures2 features {};
	VkPhysicalDeviceVulkan11Features features11 {};
	VkPhysicalDeviceVulkan12Features features12 {};
	VkPhysicalDeviceVulkan13Features features13 {};

	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features11;

	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.pNext = &features12;

	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.pNext = &features13;

	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

	DynamicVK::vkGetPhysicalDeviceFeatures2(_physicalDevice, &features);

	std::vector<VkExtensionProperties> extProps;
	DynamicVK::vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &count, nullptr);
	extProps.resize(count);
	DynamicVK::vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &count, extProps.data());

	std::unordered_set<const char*, std::hash<std::string_view>, std::equal_to<std::string_view>> extensionSet {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
	};
	std::vector<const char*> extensions(extensionSet.begin(), extensionSet.end());

	for (const auto& prop: extProps) {
		if (strcmp(prop.extensionName, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME) == 0) {
			extensionSet.insert(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
			break;
		}
	}

	VkDeviceCreateInfo deviceCreateInfo {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
	deviceCreateInfo.pNext = &features; // enable all features
	deviceCreateInfo.enabledExtensionCount = extensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = extensions.data();
	if (DynamicVK::vkCreateDevice(_physicalDevice, &deviceCreateInfo, nullptr, &_device) != VK_SUCCESS) {
		// TODO
		abort();
	}

	for (const auto& index: queueFamilyIndices) {
		VkQueue queue;
		DynamicVK::vkGetDeviceQueue(_device, index, 0, &queue);

		if (_graphicsQueueFamilyIndex && *_graphicsQueueFamilyIndex == index) {
			_graphicsQueue = queue;
		}

		if (_computeQueueFamilyIndex && *_computeQueueFamilyIndex == index) {
			_computeQueue = queue;
		}

		if (_transferQueueFamilyIndex && *_transferQueueFamilyIndex == index) {
			_transferQueue = queue;
		}
	}

	// create the event loop semaphore

	VkSemaphoreTypeCreateInfo semaphoreTypeInfo {};
	semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreCreateInfo semaphoreCreateInfo {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = &semaphoreTypeInfo;

	VkSemaphore wakeupSemaphore;
	if (DynamicVK::vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &wakeupSemaphore) != VK_SUCCESS) {
		// TODO
		abort();
	}

	_eventLoopSemaphores.push_back(wakeupSemaphore);
	_eventLoopWaitValues.push_back(1);
	_eventLoopCallbacks.push_back(nullptr);

	if (_graphicsQueueFamilyIndex || _computeQueueFamilyIndex) {
		VkCommandPoolCreateInfo createInfo {};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.queueFamilyIndex = _graphicsQueueFamilyIndex ? *_graphicsQueueFamilyIndex : *_computeQueueFamilyIndex;

		if (DynamicVK::vkCreateCommandPool(_device, &createInfo, nullptr, &_oneshotCommandPool) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}
};

Indium::PrivateDevice::~PrivateDevice() {
	if (_oneshotCommandPool) {
		DynamicVK::vkDestroyCommandPool(_device, _oneshotCommandPool, nullptr);
	}
	DynamicVK::vkDestroySemaphore(_device, _eventLoopSemaphores[0], nullptr);
	DynamicVK::vkDestroyDevice(_device, nullptr);
};

std::string Indium::PrivateDevice::name() const {
	return _properties.deviceName;
};

std::shared_ptr<Indium::CommandQueue> Indium::PrivateDevice::newCommandQueue() {
	return std::make_shared<PrivateCommandQueue>(shared_from_this());
};

std::shared_ptr<Indium::RenderPipelineState> Indium::PrivateDevice::newRenderPipelineState(const RenderPipelineDescriptor& descriptor) {
	return std::make_shared<PrivateRenderPipelineState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateDevice::newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) {
	if (options != PipelineOption::None) {
		throw std::runtime_error("TODO: support compute pipeline options");
	}
	return std::make_shared<PrivateComputePipelineState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateDevice::newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) {
	return newComputePipelineState(ComputePipelineDescriptor { computeFunction }, options, reflection);
};

std::shared_ptr<Indium::Buffer> Indium::PrivateDevice::newBuffer(size_t length, ResourceOptions options) {
	return std::make_shared<PrivateBuffer>(shared_from_this(), length, options);
};

std::shared_ptr<Indium::Buffer> Indium::PrivateDevice::newBuffer(const void* pointer, size_t length, ResourceOptions options) {
	return std::make_shared<PrivateBuffer>(shared_from_this(), pointer, length, options);
};

std::shared_ptr<Indium::Library> Indium::PrivateDevice::newLibrary(const void* data, size_t length) {
	// TODO: cache translated libraries
	size_t translatedSize = 0;
	Iridium::OutputInfo outputInfo;
	auto translatedData = Iridium::translate(data, length, translatedSize, outputInfo);
	PrivateLibrary::FunctionInfoMap funcInfoMap;

	for (const auto& [name, info]: outputInfo.functionInfos) {
		auto& funcInfo = funcInfoMap[name];

		switch (info.type) {
			case Iridium::FunctionType::Fragment:
				funcInfo.functionType = FunctionType::Fragment;
				break;
			case Iridium::FunctionType::Vertex:
				funcInfo.functionType = FunctionType::Vertex;
				break;
			case Iridium::FunctionType::Kernel:
				funcInfo.functionType = FunctionType::Kernel;
				break;
		}

		funcInfo.bindings.insert(funcInfo.bindings.end(), info.bindings.begin(), info.bindings.end());
		funcInfo.embeddedSamplers.insert(funcInfo.embeddedSamplers.end(), info.embeddedSamplers.begin(), info.embeddedSamplers.end());
	}

	auto lib = std::make_shared<PrivateLibrary>(shared_from_this(), static_cast<const char*>(translatedData), translatedSize, funcInfoMap);
	free(translatedData);
	return lib;
};

std::shared_ptr<Indium::Texture> Indium::PrivateDevice::newTexture(const TextureDescriptor& descriptor) {
	return std::make_shared<ConcreteTexture>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::SamplerState> Indium::PrivateDevice::newSamplerState(const SamplerDescriptor& descriptor) {
	return std::make_shared<PrivateSamplerState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::DepthStencilState> Indium::PrivateDevice::newDepthStencilState(const DepthStencilDescriptor& descriptor) {
	return std::make_shared<PrivateDepthStencilState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::Device> Indium::createSystemDefaultDevice() {
	return globalDeviceList.empty() ? nullptr : globalDeviceList.front();
};

void Indium::PrivateDevice::pollEvents(uint64_t timeoutNanoseconds) {
	// this is held for the entire duration of the poll so we can
	// ensure we're the only one polling, which allows us to avoid
	// some extra logic to handle the case of multiple thread polling simultaneously
	std::unique_lock pollingLock(_pollingMutex);

	std::unique_lock lock(_eventLoopMutex);

	const std::vector<VkSemaphore> semaphores = _eventLoopSemaphores;
	const std::vector<uint64_t> values = _eventLoopWaitValues;
	const std::vector<std::function<void()>> callbacks = _eventLoopCallbacks;

	lock.unlock();

	VkSemaphoreWaitInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	info.semaphoreCount = semaphores.size();
	info.pSemaphores = semaphores.data();
	info.pValues = values.data();
	info.flags = VK_SEMAPHORE_WAIT_ANY_BIT;

	auto result = DynamicVK::vkWaitSemaphores(_device, &info, timeoutNanoseconds);

	if (result == VK_TIMEOUT) {
		// timed out with no semaphores ready
		return;
	}

	std::vector<size_t> readyIndices;

	// now check which semaphores are ready
	// (excluding 0 because that's the special event loop wakeup semaphore)
	for (size_t i = 1; i < semaphores.size(); ++i) {
		uint64_t count;

		if (DynamicVK::vkGetSemaphoreCounterValue(_device, semaphores[i], &count) != VK_SUCCESS) {
			// TODO
			abort();
		}

		if (count >= values[i]) {
			readyIndices.push_back(i);
		}
	}

	// note that we're the only ones allowed to remove elements from the vectors
	// and this method cannot be invoked concurrently by different threads,
	// so we assume that the front portions of the vectors (the portions we copied
	// earlier) remain the same.

	lock.lock();

	for (auto it = readyIndices.rbegin(); it != readyIndices.rend(); ++it) {
		_eventLoopSemaphores.erase(_eventLoopSemaphores.begin() + *it);
		_eventLoopWaitValues.erase(_eventLoopWaitValues.begin() + *it);
		_eventLoopCallbacks.erase(_eventLoopCallbacks.begin() + *it);
	}

	lock.unlock();

	// now let's invoke callbacks for ready semaphores

	for (auto it = readyIndices.begin(); it != readyIndices.end(); ++it) {
		const auto& callback = callbacks[*it];

		if (!callback) {
			continue;
		}

		callback();
	}
};

void Indium::PrivateDevice::wakeupEventLoop() {
	std::unique_lock lock(_eventLoopMutex);

	auto oldVal = _eventLoopWaitValues[0]++;

	VkSemaphoreSignalInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
	info.semaphore = _eventLoopSemaphores[0];
	info.value = oldVal;
	DynamicVK::vkSignalSemaphore(_device, &info);
};

void Indium::PrivateDevice::waitForSemaphore(VkSemaphore semaphore, uint64_t targetValue, std::function<void()> callback) {
	{
		std::unique_lock lock(_eventLoopMutex);

		_eventLoopSemaphores.push_back(semaphore);
		_eventLoopWaitValues.push_back(targetValue);
		_eventLoopCallbacks.push_back(callback);
	}

	// now wakeup the event loop so it can start waiting on this new semaphore
	wakeupEventLoop();
};

// TODO: create a semaphore pool to avoid constantly creating and destroying semaphores

Indium::TimelineSemaphore Indium::PrivateDevice::getTimelineSemaphore() {
	VkSemaphoreTypeCreateInfo typeInfo {};
	typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &typeInfo;

	VkSemaphore semaphore;
	if (DynamicVK::vkCreateSemaphore(_device, &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
		// TODO
		abort();
	}

	return TimelineSemaphore {
		shared_from_this(),
		semaphore,
		0,
	};
};

void Indium::PrivateDevice::putTimelineSemaphore(const TimelineSemaphore& semaphore) {
	DynamicVK::vkDestroySemaphore(_device, semaphore.semaphore, nullptr);
};

std::shared_ptr<Indium::TimelineSemaphore> Indium::PrivateDevice::getWrappedTimelineSemaphore() {
	return std::shared_ptr<Indium::TimelineSemaphore>(new TimelineSemaphore(getTimelineSemaphore()), [](TimelineSemaphore* ptr) {
		ptr->device->putTimelineSemaphore(*ptr);
		delete ptr;
	});
};

Indium::BinarySemaphore Indium::PrivateDevice::getBinarySemaphore(bool exportable) {
	VkSemaphoreCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkExportSemaphoreCreateInfo exportInfo {};

	if (exportable) {
		exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
		exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

		createInfo.pNext = &exportInfo;
	}

	VkSemaphore semaphore;
	if (DynamicVK::vkCreateSemaphore(_device, &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
		// TODO
		abort();
	}

	return BinarySemaphore { shared_from_this(), semaphore };
};

void Indium::PrivateDevice::putBinarySemaphore(const BinarySemaphore& semaphore) {
	DynamicVK::vkDestroySemaphore(_device, semaphore.semaphore, nullptr);
};

std::shared_ptr<Indium::BinarySemaphore> Indium::PrivateDevice::getWrappedBinarySemaphore(bool exportable) {
	return std::shared_ptr<Indium::BinarySemaphore>(new BinarySemaphore(getBinarySemaphore(exportable)), [](BinarySemaphore* ptr) {
		ptr->device->putBinarySemaphore(*ptr);
		delete ptr;
	});
};
