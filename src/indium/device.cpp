#include <indium/device.private.hpp>
#include <indium/memory-type.private.hpp>
#include <indium/device-order.private.hpp>

#include <cctype>
#include <iostream>
#include <iterator>
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
#include <stdexcept>
#include <thread>
#include <unordered_set>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
	/**
	 * One of the fields of an EmbeddedSamplerDescriptor that holds an
	 * enumeration, with the last value that enumeration allows.
	 */
	struct SamplerEnum {
		const char* field;
		size_t value;
		size_t maximum;
	};

	Indium::ArgumentInfo argumentInfoFromIridium(const Iridium::ArgumentInfo& argument) {
		Indium::ArgumentInfo result;
		switch (argument.type) {
			case Iridium::BindingType::Texture: result.kind = Indium::ArgumentKind::Texture; break;
			case Iridium::BindingType::Sampler: result.kind = Indium::ArgumentKind::Sampler; break;
			default: result.kind = Indium::ArgumentKind::Buffer; break;
		}
		result.index = argument.index;
		result.name = argument.name;
		result.typeName = argument.typeName;
		result.size = argument.size;
		result.alignment = argument.alignment;
		for (const auto& member: argument.members) {
			result.members.push_back(Indium::ArgumentMemberInfo { member.offset, member.typeName, member.name });
		}
		return result;
	}

	/**
	 * Iridium's reflection, in the terms every other producer's reflection
	 * arrives in, so that the two entry points to newLibrary() agree on what a
	 * module's functions are described with and only one of them is Iridium's.
	 *
	 * Not checked. Iridium is the translator indium is built against, and it has
	 * always been trusted, so this stays a translation and gives the path that
	 * is verified on hardware no new way to fail.
	 */
	Indium::PrivateLibrary::FunctionInfoMap functionInfosFromIridium(const Iridium::OutputInfo& outputInfo) {
		Indium::PrivateLibrary::FunctionInfoMap functionInfos;

		for (const auto& [name, info]: outputInfo.functionInfos) {
			auto& functionInfo = functionInfos[name];

			switch (info.type) {
				case Iridium::FunctionType::Fragment:
					functionInfo.functionType = Indium::FunctionType::Fragment;
					break;
				case Iridium::FunctionType::Vertex:
					functionInfo.functionType = Indium::FunctionType::Vertex;
					break;
				case Iridium::FunctionType::Kernel:
					functionInfo.functionType = Indium::FunctionType::Kernel;
					break;
			}

			for (const auto& binding: info.bindings) {
				Indium::BindingDescriptor descriptor;
				descriptor.type = static_cast<Indium::BindingType>(binding.type);
				descriptor.index = binding.index;
				descriptor.internalIndex = binding.internalIndex;
				descriptor.textureAccessType = static_cast<Indium::TextureAccessType>(binding.textureAccessType);
				descriptor.embeddedSamplerIndex = binding.embeddedSamplerIndex;

				functionInfo.bindings.push_back(descriptor);
			}

			for (const auto& embeddedSampler: info.embeddedSamplers) {
				Indium::EmbeddedSamplerDescriptor descriptor;
				descriptor.widthAddressMode = static_cast<Indium::EmbeddedSamplerDescriptor::AddressMode>(embeddedSampler.widthAddressMode);
				descriptor.heightAddressMode = static_cast<Indium::EmbeddedSamplerDescriptor::AddressMode>(embeddedSampler.heightAddressMode);
				descriptor.depthAddressMode = static_cast<Indium::EmbeddedSamplerDescriptor::AddressMode>(embeddedSampler.depthAddressMode);
				descriptor.magnificationFilter = static_cast<Indium::EmbeddedSamplerDescriptor::Filter>(embeddedSampler.magnificationFilter);
				descriptor.minificationFilter = static_cast<Indium::EmbeddedSamplerDescriptor::Filter>(embeddedSampler.minificationFilter);
				descriptor.mipmapFilter = static_cast<Indium::EmbeddedSamplerDescriptor::MipFilter>(embeddedSampler.mipmapFilter);
				descriptor.usesNormalizedCoordinates = embeddedSampler.usesNormalizedCoordinates;
				descriptor.compareFunction = static_cast<Indium::EmbeddedSamplerDescriptor::CompareFunction>(embeddedSampler.compareFunction);
				descriptor.anisotropyLevel = embeddedSampler.anisotropyLevel;
				descriptor.borderColor = static_cast<Indium::EmbeddedSamplerDescriptor::BorderColor>(embeddedSampler.borderColor);
				descriptor.lodMin = embeddedSampler.lodMin;
				descriptor.lodMax = embeddedSampler.lodMax;

				functionInfo.embeddedSamplers.push_back(descriptor);
			}

			for (const auto& argument: info.arguments) {
				functionInfo.arguments.push_back(argumentInfoFromIridium(argument));
			}
		}

		return functionInfos;
	}
}

std::vector<std::shared_ptr<Indium::PrivateDevice>> Indium::globalDeviceList;

void Indium::initGlobalDeviceList() {
	std::vector<VkPhysicalDevice> physicalDevices;
	uint32_t count = 0;

	// The devices that meet Indium's requirements, and their types, in the loader's order.
	std::vector<VkPhysicalDevice> accepted;
	std::vector<VkPhysicalDeviceType> acceptedTypes;

auto result = DynamicVK::vkEnumeratePhysicalDevices(globalInstance, &count, nullptr);
		if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
			std::cerr << "vkEnumeratePhysicalDevices failed with VkResult " << result << std::endl;
			return;
		}

		physicalDevices.resize(count);
		result = DynamicVK::vkEnumeratePhysicalDevices(globalInstance, &count, physicalDevices.data());
		if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
			std::cerr << "vkEnumeratePhysicalDevices failed with VkResult " << result << std::endl;
			return;
		}

		if (count == 0) {
			std::cerr << "vkEnumeratePhysicalDevices reported no physical devices" << std::endl;
			return;
		}

	for (auto&& device: physicalDevices) {
		VkPhysicalDeviceProperties props;
		DynamicVK::vkGetPhysicalDeviceProperties(device, &props);

		if (VK_API_VERSION_VARIANT(props.apiVersion) != 0 || props.apiVersion < VK_API_VERSION_1_3) {
			std::cerr << "Skipping physical device " << props.deviceName
			          << ": reports Vulkan "
			          << VK_API_VERSION_VARIANT(props.apiVersion) << '.'
			          << VK_API_VERSION_MAJOR(props.apiVersion) << '.'
			          << VK_API_VERSION_MINOR(props.apiVersion)
			          << ", and 1.3 is required" << std::endl;
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
			std::cerr << "Skipping physical device " << props.deviceName
			          << ": no timelineSemaphore" << std::endl;
			continue;
		}

		accepted.push_back(device);
		acceptedTypes.push_back(props.deviceType);
	}

	for (size_t index: orderDevices(acceptedTypes)) {
		globalDeviceList.push_back(std::make_shared<PrivateDevice>(accepted[index]));
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

	if (_graphicsQueueFamilyIndex) {
		_queueFamilyIndices.insert(*_graphicsQueueFamilyIndex);
	}

	if (_computeQueueFamilyIndex) {
		_queueFamilyIndices.insert(*_computeQueueFamilyIndex);
	}

	if (_transferQueueFamilyIndex) {
		_queueFamilyIndices.insert(*_transferQueueFamilyIndex);
	}

	std::vector<VkExtensionProperties> extProps;
	DynamicVK::vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &count, nullptr);
	extProps.resize(count);
	DynamicVK::vkEnumerateDeviceExtensionProperties(_physicalDevice, nullptr, &count, extProps.data());

	// put required extensions in _enabledExtensions here
	auto indiumFeatures = static_cast<Feature>(0);

	std::vector<std::pair<const char*, Feature>> optionalExtensions {
		// put optional extensions here
		{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, Feature::Swapchain },
		{ VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, Feature::ExternalMemoryFD },
		{ VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, Feature::ExternalSemaphoreFD },
		{ VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME, Feature::NonSemanticInfo },
		{ VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, Feature::MemoryBudget },
	};

	for (const auto& prop: extProps) {
		for (const auto& [name, feature]: optionalExtensions) {
			if (strcmp(prop.extensionName, name) == 0) {
				_enabledExtensions.push_back(name);
				indiumFeatures = indiumFeatures | feature;
			}
		}
	}

	// Enable only nullDescriptor from VK_EXT_robustness2, not its costlier robust access features: it
	// lets an unbound vertex buffer or texture be bound as VK_NULL_HANDLE, which reads as zero.
	if (std::any_of(extProps.begin(), extProps.end(), [](const VkExtensionProperties& prop) {
		return strcmp(prop.extensionName, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) == 0;
	})) {
		VkPhysicalDeviceRobustness2FeaturesEXT robustness2 {};
		robustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
		VkPhysicalDeviceFeatures2 query {};
		query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		query.pNext = &robustness2;
		DynamicVK::vkGetPhysicalDeviceFeatures2(_physicalDevice, &query);
		if (robustness2.nullDescriptor) {
			_enabledExtensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
			_nullDescriptor = true;
		}
	}

	_features = indiumFeatures;
};

void Indium::PrivateDevice::createLogicalDevice() {
	std::unique_lock creationLock(_logicalDeviceMutex);

	if (_logicalDeviceCreated.load(std::memory_order_relaxed)) {
		return;
	}

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::vector<float> queuePriorities = { 1.0f };

	for (const auto& index: _queueFamilyIndices) {
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

	VkPhysicalDeviceRobustness2FeaturesEXT robustness2 {};
	if (_nullDescriptor) {
		robustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
		robustness2.nullDescriptor = VK_TRUE;
		features13.pNext = &robustness2;
	}

	VkDeviceCreateInfo deviceCreateInfo {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
	deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
	deviceCreateInfo.pNext = &features; // enable all features
	deviceCreateInfo.enabledExtensionCount = _enabledExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = _enabledExtensions.data();
	if (DynamicVK::vkCreateDevice(_physicalDevice, &deviceCreateInfo, nullptr, &_device) != VK_SUCCESS) {
		// TODO
		abort();
	}

	for (const auto& index: _queueFamilyIndices) {
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

	{
		std::unique_lock lock(_eventLoopMutex);
		_eventLoopSemaphores.push_back(wakeupSemaphore);
		_eventLoopWaitValues.push_back(1);
		_eventLoopCallbacks.push_back(nullptr);
	}

	if (_graphicsQueueFamilyIndex || _computeQueueFamilyIndex) {
		VkCommandPoolCreateInfo createInfo {};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.queueFamilyIndex = _graphicsQueueFamilyIndex ? *_graphicsQueueFamilyIndex : *_computeQueueFamilyIndex;

		if (DynamicVK::vkCreateCommandPool(_device, &createInfo, nullptr, &_oneshotCommandPool) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}

	_logicalDeviceCreated.store(true, std::memory_order_release);

	// a poller that started before the device existed is waiting for it
	_logicalDeviceCondition.notify_all();
};

Indium::PrivateDevice::~PrivateDevice() {
	if (!_logicalDeviceCreated.load(std::memory_order_acquire)) {
		return;
	}
	if (_oneshotCommandPool) {
		DynamicVK::vkDestroyCommandPool(_device, _oneshotCommandPool, nullptr);
	}
	DynamicVK::vkDestroySemaphore(_device, _eventLoopSemaphores[0], nullptr);
	DynamicVK::vkDestroyDevice(_device, nullptr);
};

std::string Indium::PrivateDevice::name() const {
	return _properties.deviceName;
};

/*
	Metal's `recommendedMaxWorkingSetSize` is "an approximation of how much memory
	this device can use with good performance", beyond which "the device is likely
	to be overcommitted and incur a performance penalty". It is a soft byte
	budget on the total of all resources, so the honest Vulkan analogue is a memory
	budget, not a limit: `heapBudget` is defined as how much the process can
	allocate from a heap before allocations fail or degrade, which is the same
	quantity.

	`maxMemoryAllocationCount` is deliberately not used. It counts VkDeviceMemory
	objects, not bytes, so it cannot bound a working set: an app holding one 8 GB
	buffer is one allocation and a single 1 MB buffer is also one, and the two
	differ by three orders of magnitude in the only unit Metal's property is
	measured in.

	A heap budget is only reported for heaps the device actually lets the app use,
	and it excludes what other processes already hold, which is precisely the
	overcommitment Metal warns about. Without the extension there is no budget, so
	the heap size is reported instead: the physical capacity of the device's own
	memory, which is an upper bound on what can be used and the only figure the
	device states about itself. It overstates availability where another process
	is already resident, which is why a driver that can do better should say so.

	Device-local heaps are the ones Metal resources are backed by; a non-local heap
	is host RAM reachable over a slower path, and counting it would report a
	working set the device cannot actually sustain at that speed. A device with no
	device-local heap at all still has memory, so every heap counts in that case.
*/
/*
	A buffer is one VkBuffer backed by one VkDeviceMemory, so a request can only
	succeed if it passes all three limits: the largest VkBuffer
	(`maxBufferSize`, Vulkan 1.3), the largest single allocation
	(`maxMemoryAllocationSize`, Vulkan 1.1), and the physical size of the heap the
	allocation would come from. Drivers may advertise the first two far above what
	the device holds (an 8 GB heap behind a 128 GB limit), so the largest
	device-local heap bounds the result too. Without a device-local heap, every
	heap counts, as in recommendedMaxWorkingSetSize().
*/
bool Indium::PrivateDevice::supportsDepth24Stencil8PixelFormat() const {
	VkFormatProperties properties {};
	DynamicVK::vkGetPhysicalDeviceFormatProperties(_physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &properties);
	return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
};

bool Indium::PrivateDevice::supportsTextureSampleCount(uint32_t sampleCount) const {
	// a VkSampleCountFlagBits value is its sample count, a power of two
	if (sampleCount == 0 || (sampleCount & (sampleCount - 1)) != 0) {
		return false;
	}
	const auto& limits = _properties.limits;
	return (limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts & sampleCount) != 0;
};

uint32_t Indium::PrivateDevice::vendorID() const {
	return _properties.vendorID;
};

uint32_t Indium::PrivateDevice::deviceID() const {
	return _properties.deviceID;
};

Indium::DeviceType Indium::PrivateDevice::deviceType() const {
	switch (_properties.deviceType) {
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return DeviceType::IntegratedGPU;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return DeviceType::DiscreteGPU;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return DeviceType::VirtualGPU;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:            return DeviceType::CPU;
		default:                                     return DeviceType::Other;
	}
};

std::vector<Indium::MemoryHeap> Indium::PrivateDevice::memoryHeaps() const {
	std::vector<MemoryHeap> heaps;
	for (uint32_t i = 0; i < _memoryProperties.memoryHeapCount; ++i) {
		const auto& heap = _memoryProperties.memoryHeaps[i];
		heaps.push_back(MemoryHeap { heap.size, (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 });
	}
	return heaps;
};

std::array<uint8_t, 16> Indium::PrivateDevice::deviceUUID() const {
	VkPhysicalDeviceIDProperties idProperties {};
	idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
	VkPhysicalDeviceProperties2 properties {};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &idProperties;
	DynamicVK::vkGetPhysicalDeviceProperties2(_physicalDevice, &properties);

	std::array<uint8_t, 16> uuid {};
	static_assert(sizeof(idProperties.deviceUUID) == 16);
	std::copy(std::begin(idProperties.deviceUUID), std::end(idProperties.deviceUUID), uuid.begin());
	return uuid;
};

std::optional<uint32_t> Indium::PrivateDevice::memoryTypeIndex(uint32_t memoryTypeBits, StorageMode storageMode) const {
	auto index = findMemoryType(memoryTypeBits, storageMode);
	if (index == SIZE_MAX) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(index);
};

uint64_t Indium::PrivateDevice::maxBufferLength() const {
	VkPhysicalDeviceVulkan11Properties vk11Props {};
	vk11Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
	VkPhysicalDeviceVulkan13Properties vk13Props {};
	vk13Props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
	vk11Props.pNext = &vk13Props;
	VkPhysicalDeviceProperties2 props {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &vk11Props;
	DynamicVK::vkGetPhysicalDeviceProperties2(_physicalDevice, &props);

	uint64_t deviceLocalHeap = 0;
	uint64_t anyHeap = 0;
	for (uint32_t i = 0; i < _memoryProperties.memoryHeapCount; ++i) {
		const auto& heap = _memoryProperties.memoryHeaps[i];
		anyHeap = std::max<uint64_t>(anyHeap, heap.size);
		if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
			deviceLocalHeap = std::max<uint64_t>(deviceLocalHeap, heap.size);
		}
	}

	return std::min({
		static_cast<uint64_t>(vk13Props.maxBufferSize),
		static_cast<uint64_t>(vk11Props.maxMemoryAllocationSize),
		deviceLocalHeap != 0 ? deviceLocalHeap : anyHeap,
	});
};

uint64_t Indium::PrivateDevice::recommendedMaxWorkingSetSize() const {
	uint64_t deviceLocal = 0;
	uint64_t all = 0;
	bool hasDeviceLocal = false;

	for (uint32_t i = 0; i < _memoryProperties.memoryHeapCount; ++i) {
		const auto& heap = _memoryProperties.memoryHeaps[i];

		all += heap.size;

		if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
			hasDeviceLocal = true;
			deviceLocal += heap.size;
		}
	}

	if (!(_features & Feature::MemoryBudget)) {
		return hasDeviceLocal ? deviceLocal : all;
	}

	VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties {};
	budgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

	VkPhysicalDeviceMemoryProperties2 properties2 {};
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
	properties2.pNext = &budgetProperties;

	DynamicVK::vkGetPhysicalDeviceMemoryProperties2(_physicalDevice, &properties2);

	uint64_t budget = 0;

	for (uint32_t i = 0; i < _memoryProperties.memoryHeapCount; ++i) {
		// A budget is required to be non-zero for every heap the device reports,
		// but a driver that leaves one at zero must not be allowed to shrink the
		// answer, so only non-zero budgets contribute.
		if (budgetProperties.heapBudget[i] == 0) {
			continue;
		}

		if (hasDeviceLocal && (_memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
			continue;
		}

		budget += budgetProperties.heapBudget[i];
	}

	// A driver that advertised the extension but reported no budget at all must
	// not make the answer zero, which an application would read as "allocate
	// nothing".
	return (budget != 0) ? budget : (hasDeviceLocal ? deviceLocal : all);
}

std::shared_ptr<Indium::CommandQueue> Indium::PrivateDevice::newCommandQueue() {
	ensureLogicalDevice();
	return std::make_shared<PrivateCommandQueue>(shared_from_this());
};

std::shared_ptr<Indium::RenderPipelineState> Indium::PrivateDevice::newRenderPipelineState(const RenderPipelineDescriptor& descriptor) {
	ensureLogicalDevice();
	return std::make_shared<PrivateRenderPipelineState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateDevice::newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) {
	ensureLogicalDevice();
	// The reflection options only ask for argument information, which every function carries
	// (Function::argumentInfo); they do not change the pipeline.
	constexpr auto reflectionOptions = static_cast<size_t>(PipelineOption::ArgumentInfo | PipelineOption::BufferTypeInfo);
	if ((static_cast<size_t>(options) & ~reflectionOptions) != 0) {
		throw std::invalid_argument("unsupported compute pipeline option");
	}
	return std::make_shared<PrivateComputePipelineState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::ComputePipelineState> Indium::PrivateDevice::newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) {
	return newComputePipelineState(ComputePipelineDescriptor { computeFunction }, options, reflection);
};

std::shared_ptr<Indium::Buffer> Indium::PrivateDevice::newBuffer(size_t length, ResourceOptions options) {
	ensureLogicalDevice();
	return std::make_shared<PrivateBuffer>(shared_from_this(), length, options);
};

std::shared_ptr<Indium::Buffer> Indium::PrivateDevice::newBuffer(const void* pointer, size_t length, ResourceOptions options) {
	ensureLogicalDevice();
	return std::make_shared<PrivateBuffer>(shared_from_this(), pointer, length, options);
};

size_t Indium::PrivateDevice::findMemoryType(uint32_t memoryTypeBits, StorageMode storageMode) const {
	return chooseMemoryType(memoryProperties(), memoryTypeBits, storageMode);
};

std::shared_ptr<Indium::Buffer> Indium::PrivateDevice::fallbackBuffer() {
	std::scoped_lock lock(_fallbackMutex);
	if (!_fallbackBuffer) {
		std::vector<char> zeros(fallbackBufferSize, 0);
		_fallbackBuffer = newBuffer(zeros.data(), zeros.size(), ResourceOptions::StorageModeShared);
	}
	return _fallbackBuffer;
};

std::shared_ptr<Indium::SamplerState> Indium::PrivateDevice::fallbackSampler() {
	std::scoped_lock lock(_fallbackMutex);
	if (!_fallbackSampler) {
		_fallbackSampler = newSamplerState(SamplerDescriptor {});
	}
	return _fallbackSampler;
};

std::shared_ptr<Indium::Library> Indium::PrivateDevice::newLibrary(const void* data, size_t length) {
	ensureLogicalDevice();
	// TODO: cache translated libraries
	size_t translatedSize = 0;
	Iridium::OutputInfo outputInfo;
	auto translatedData = Iridium::translate(data, length, translatedSize, outputInfo);

	// translate() returns nullptr on failure, and a failed translation reports a
	// zero output size, so there is no module to hand PrivateLibrary: it would
	// call vkCreateShaderModule with a null pointer and abort() there, a crash a
	// long way from the translation that failed. Report the failure here instead,
	// the way newFunction reports an unknown name.
	if (!translatedData) {
		return nullptr;
	}

	auto lib = std::make_shared<PrivateLibrary>(shared_from_this(), static_cast<const char*>(translatedData), translatedSize, functionInfosFromIridium(outputInfo));
	free(translatedData);
	return lib;
};

std::shared_ptr<Indium::Library> Indium::PrivateDevice::newLibrary(const void* spirv, size_t length, const LibraryReflection& reflection, std::string* errorMessage) {
	ensureLogicalDevice();
	// TODO: cache translated libraries
	std::string ignored;

	if (!errorMessage) {
		errorMessage = &ignored;
	}

	auto fail = [errorMessage](const std::string& reason) -> std::shared_ptr<Library> {
		*errorMessage = reason;
		return nullptr;
	};

	// PrivateLibrary abort()s if vkCreateShaderModule rejects the module, so
	// everything that would make it do so gets rejected here instead, where the
	// reason can still name the module. A producer that hands over something
	// that is not a module at all is the common case; a module that is valid
	// but incompatible is Vulkan's to report, and its message is better than
	// one invented here.
	if (!spirv) {
		return fail("no SPIR-V data");
	}

	if (length < sizeof(uint32_t) * 5) {
		return fail("SPIR-V module is " + std::to_string(length) + " bytes, shorter than the 5-word header");
	}

	if (length % sizeof(uint32_t) != 0) {
		return fail("SPIR-V module is " + std::to_string(length) + " bytes, which is not a whole number of words");
	}

	uint32_t magic;
	memcpy(&magic, spirv, sizeof(magic));

	if (magic != 0x07230203) {
		// 0x03022307 is this number with the bytes the other way round, which is
		// a module this host cannot consume.
		char magicText[16];
		snprintf(magicText, sizeof(magicText), "0x%08x", magic);
		return fail(std::string("SPIR-V magic number is ") + magicText + ", expected 0x07230203");
	}

	PrivateLibrary::FunctionInfoMap functionInfos;

	if (reflection.functions.empty()) {
		return fail("reflection describes no functions");
	}

	for (const auto& [name, functionReflection]: reflection.functions) {
		if (name.empty()) {
			return fail("reflection describes a function with no name");
		}

		// The stage decides the descriptor set layout, the shader stage flags
		// and whether a compute pipeline can be made at all, and a kernel
		// described as a vertex function dispatches against a layout that does
		// not exist. FunctionType::Invalid is what a producer that left the
		// field alone gets, so it is the one to catch.
		if (functionReflection.functionType != FunctionType::Vertex &&
			functionReflection.functionType != FunctionType::Fragment &&
			functionReflection.functionType != FunctionType::Kernel) {
			return fail("function '" + name + "' has a stage indium cannot bind resources for");
		}

		// A name that is not in the module cannot be a pipeline's pName, and
		// pipeline creation abort()s when Vulkan refuses it. A name in a
		// well-formed module is a NUL-terminated literal, so looking for the
		// bytes cannot miss one; it does not prove the name is an entry point,
		// which is why the header still calls that the producer's job.
		std::string nameWithTerminator = name;
		nameWithTerminator += '\0';

		auto moduleBegin = static_cast<const char*>(spirv);
		auto moduleEnd = moduleBegin + length;

		if (std::search(moduleBegin, moduleEnd, nameWithTerminator.begin(), nameWithTerminator.end()) == moduleEnd) {
			return fail("function '" + name + "' does not occur in the module");
		}

		auto& functionInfo = functionInfos[name];
		functionInfo.functionType = functionReflection.functionType;

		// One descriptor binding number per resource in the set, so a producer
		// that numbers two of them the same gets a set layout Vulkan rejects.
		std::unordered_set<size_t> usedInternalIndices;

		for (const auto& binding: functionReflection.bindings) {
			if (static_cast<size_t>(binding.type) > static_cast<size_t>(BindingType::VertexInput)) {
				return fail("function '" + name + "' has a binding of an unknown type");
			}

			if (static_cast<size_t>(binding.textureAccessType) > static_cast<size_t>(TextureAccessType::ReadWrite)) {
				return fail("function '" + name + "' has a binding with an unknown texture access type");
			}

			if (binding.type == BindingType::Sampler && binding.index == std::numeric_limits<size_t>::max()) {
				// The address-buffer walk indexes embeddedSamplerStates with this
				// and nothing checks it, so an out-of-range value is a read past
				// the end of the vector.
				if (binding.embeddedSamplerIndex >= functionReflection.embeddedSamplers.size()) {
					return fail("function '" + name + "' has a binding using embedded sampler " + std::to_string(binding.embeddedSamplerIndex) + ", of " + std::to_string(functionReflection.embeddedSamplers.size()));
				}
			}

			if (binding.type == BindingType::Texture || binding.type == BindingType::Sampler) {
				auto inserted = usedInternalIndices.insert(binding.internalIndex);

				if (!inserted.second) {
					return fail("function '" + name + "' has two bindings at descriptor binding " + std::to_string(binding.internalIndex));
				}
			}

			functionInfo.bindings.push_back(binding);
		}

		functionInfo.embeddedSamplers = functionReflection.embeddedSamplers;

		// The translation in library.cpp turns an unrecognised enum value into a
		// sampler state that works but is not the one that was asked for, so
		// catch out-of-range values while the entry they came from is known.
		for (size_t i = 0; i < functionReflection.embeddedSamplers.size(); ++i) {
			const auto& sampler = functionReflection.embeddedSamplers[i];

			const SamplerEnum enums[] = {
				{ "widthAddressMode", static_cast<size_t>(sampler.widthAddressMode), static_cast<size_t>(EmbeddedSamplerDescriptor::AddressMode::ClampToBorderColor) },
				{ "heightAddressMode", static_cast<size_t>(sampler.heightAddressMode), static_cast<size_t>(EmbeddedSamplerDescriptor::AddressMode::ClampToBorderColor) },
				{ "depthAddressMode", static_cast<size_t>(sampler.depthAddressMode), static_cast<size_t>(EmbeddedSamplerDescriptor::AddressMode::ClampToBorderColor) },
				{ "magnificationFilter", static_cast<size_t>(sampler.magnificationFilter), static_cast<size_t>(EmbeddedSamplerDescriptor::Filter::Linear) },
				{ "minificationFilter", static_cast<size_t>(sampler.minificationFilter), static_cast<size_t>(EmbeddedSamplerDescriptor::Filter::Linear) },
				{ "mipmapFilter", static_cast<size_t>(sampler.mipmapFilter), static_cast<size_t>(EmbeddedSamplerDescriptor::MipFilter::Linear) },
				{ "compareFunction", static_cast<size_t>(sampler.compareFunction), static_cast<size_t>(EmbeddedSamplerDescriptor::CompareFunction::Never) },
				{ "borderColor", static_cast<size_t>(sampler.borderColor), static_cast<size_t>(EmbeddedSamplerDescriptor::BorderColor::OpaqueWhite) },
			};

			for (const auto& samplerEnum: enums) {
				if (samplerEnum.value > samplerEnum.maximum) {
					return fail("function '" + name + "' embedded sampler " + std::to_string(i) + " has an out-of-range " + samplerEnum.field);
				}
			}
		}
	}

	*errorMessage = "";

	// The module bytes are only needed for the vkCreateShaderModule inside
	// this constructor, which copies them, so the caller's buffer is not ours
	// to free and does not have to outlive the call.
	return std::make_shared<PrivateLibrary>(shared_from_this(), static_cast<const char*>(spirv), length, functionInfos);
};

std::shared_ptr<Indium::Texture> Indium::PrivateDevice::newTexture(const TextureDescriptor& descriptor) {
	ensureLogicalDevice();
	return std::make_shared<ConcreteTexture>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::SamplerState> Indium::PrivateDevice::newSamplerState(const SamplerDescriptor& descriptor) {
	ensureLogicalDevice();
	return std::make_shared<PrivateSamplerState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::DepthStencilState> Indium::PrivateDevice::newDepthStencilState(const DepthStencilDescriptor& descriptor) {
	ensureLogicalDevice();
	return std::make_shared<PrivateDepthStencilState>(shared_from_this(), descriptor);
};

std::shared_ptr<Indium::Device> Indium::createSystemDefaultDevice() {
	return globalDeviceList.empty() ? nullptr : globalDeviceList.front();
};

std::shared_ptr<Indium::Device> Indium::createSystemDefaultDevice(const std::string& preferredDevice) {
	if (!preferredDevice.empty()) {
		bool isIndex = std::all_of(preferredDevice.begin(), preferredDevice.end(), [](unsigned char c) { return std::isdigit(c); });
		if (isIndex) {
			size_t index = 0;
			try {
				index = std::stoul(preferredDevice);
			} catch (const std::out_of_range&) {
				index = SIZE_MAX;
			}
			if (index < globalDeviceList.size()) {
				return globalDeviceList[index];
			}
		} else {
			const auto lowercase = [](std::string string) {
				std::transform(string.begin(), string.end(), string.begin(), [](unsigned char c) { return std::tolower(c); });
				return string;
			};
			auto wanted = lowercase(preferredDevice);
			for (const auto& device: globalDeviceList) {
				if (lowercase(device->name()).find(wanted) != std::string::npos) {
					return device;
				}
			}
		}
	}
	return createSystemDefaultDevice();
};

std::vector<std::shared_ptr<Indium::Device>> Indium::copyAllDevices() {
	return std::vector<std::shared_ptr<Device>>(globalDeviceList.begin(), globalDeviceList.end());
};

void Indium::PrivateDevice::pollEvents(uint64_t timeoutNanoseconds) {
	// this is held for the entire duration of the poll so we can
	// ensure we're the only one polling, which allows us to avoid
	// some extra logic to handle the case of multiple thread polling simultaneously
	std::unique_lock pollingLock(_pollingMutex);

	if (!_logicalDeviceCreated.load(std::memory_order_acquire)) {
		// nothing can complete before the Vulkan device exists, so wait for it to be created (by
		// someone else: polling alone does not create it), for wakeupEventLoop() or for the timeout
		std::unique_lock creationLock(_logicalDeviceMutex);
		auto ready = [this]() {
			return _logicalDeviceCreated.load(std::memory_order_acquire) || _wakeupBeforeCreation;
		};
		// a timeout too long to add to the current time is waited out like an infinite one
		if (timeoutNanoseconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / 2)) {
			_logicalDeviceCondition.wait(creationLock, ready);
		} else {
			_logicalDeviceCondition.wait_for(creationLock, std::chrono::nanoseconds(timeoutNanoseconds), ready);
		}
		if (_wakeupBeforeCreation) {
			_wakeupBeforeCreation = false;
			return;
		}
		if (!_logicalDeviceCreated.load(std::memory_order_acquire)) {
			return;
		}
	}

	std::unique_lock lock(_eventLoopMutex);

	const std::vector<VkSemaphore> semaphores = _eventLoopSemaphores;
	const std::vector<uint64_t> values = _eventLoopWaitValues;
	const std::vector<std::function<void()>> callbacks = _eventLoopCallbacks;
	const uint64_t registrations = _eventLoopRegistrations.load(std::memory_order_seq_cst);

	lock.unlock();

	VkSemaphoreWaitInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	info.semaphoreCount = semaphores.size();
	info.pSemaphores = semaphores.data();
	info.pValues = values.data();
	info.flags = VK_SEMAPHORE_WAIT_ANY_BIT;

	// Pairs with waitForSemaphore(), which counts its registration before it checks for blocked
	// pollers: with both sides sequentially consistent, either this poller sees the registration
	// and does not block on the stale list, or the registrant sees this poller and wakes it.
	const bool mayBlock = timeoutNanoseconds > 0;
	if (mayBlock) {
		_eventLoopBlockedPollers.fetch_add(1, std::memory_order_seq_cst);
		if (_eventLoopRegistrations.load(std::memory_order_seq_cst) != registrations) {
			timeoutNanoseconds = 0;
		}
	}

	auto result = DynamicVK::vkWaitSemaphores(_device, &info, timeoutNanoseconds);

	if (mayBlock) {
		_eventLoopBlockedPollers.fetch_sub(1, std::memory_order_seq_cst);
	}

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
	if (!_logicalDeviceCreated.load(std::memory_order_acquire)) {
		std::unique_lock creationLock(_logicalDeviceMutex);
		if (!_logicalDeviceCreated.load(std::memory_order_relaxed)) {
			// there is no wakeup semaphore yet; wake a poller waiting for the device instead
			_wakeupBeforeCreation = true;
			_logicalDeviceCondition.notify_all();
			return;
		}
	}

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
		_eventLoopRegistrations.fetch_add(1, std::memory_order_seq_cst);
	}

	// a poller that is blocked waits on a list without this semaphore; wake it so it starts
	// waiting on the new list (one that is not blocked yet will pick the semaphore up itself)
	if (_eventLoopBlockedPollers.load(std::memory_order_seq_cst) > 0) {
		wakeupEventLoop();
	}
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
	if (DynamicVK::vkCreateSemaphore(device(), &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
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
	DynamicVK::vkDestroySemaphore(device(), semaphore.semaphore, nullptr);
};

std::shared_ptr<Indium::TimelineSemaphore> Indium::PrivateDevice::getWrappedTimelineSemaphore() {
	return std::shared_ptr<Indium::TimelineSemaphore>(new TimelineSemaphore(getTimelineSemaphore()), [](TimelineSemaphore* ptr) {
		ptr->device->putTimelineSemaphore(*ptr);
		delete ptr;
	});
};

Indium::BinarySemaphore Indium::PrivateDevice::getBinarySemaphore(bool exportable) {
	if (exportable && !(_features & Feature::ExternalSemaphoreFD)) {
		throw std::runtime_error("Device does not support exportable semaphores");
	}

	VkSemaphoreCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkExportSemaphoreCreateInfo exportInfo {};

	if (exportable) {
		exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
		exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

		createInfo.pNext = &exportInfo;
	}

	VkSemaphore semaphore;
	if (DynamicVK::vkCreateSemaphore(device(), &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
		// TODO
		abort();
	}

	return BinarySemaphore { shared_from_this(), semaphore };
};

void Indium::PrivateDevice::putBinarySemaphore(const BinarySemaphore& semaphore) {
	DynamicVK::vkDestroySemaphore(device(), semaphore.semaphore, nullptr);
};

std::shared_ptr<Indium::BinarySemaphore> Indium::PrivateDevice::getWrappedBinarySemaphore(bool exportable) {
	return std::shared_ptr<Indium::BinarySemaphore>(new BinarySemaphore(getBinarySemaphore(exportable)), [](BinarySemaphore* ptr) {
		ptr->device->putBinarySemaphore(*ptr);
		delete ptr;
	});
};
