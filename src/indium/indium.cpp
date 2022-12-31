#include <indium/instance.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <iridium/iridium.hpp>

#include <unordered_set>

#include <cstring>
#include <string_view>

#include <iostream>

VkInstance Indium::globalInstance = VK_NULL_HANDLE;

static VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

static volatile int noOptOut;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {
	std::cerr << "Validation message: " << callbackData->pMessage << std::endl;

	if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
		// convenient breakpoint
		noOptOut = 1;
	}

	return VK_FALSE;
};

void Indium::init(const char** additionalExtensions, size_t additionalExtensionCount, bool enableValidation) {
	if (!Iridium::init()) {
		// TODO: maybe warn?
		return;
	}

	if (!DynamicVK::init()) {
		// TODO: maybe warn?
		return;
	}

	std::unordered_set<const char*, std::hash<std::string_view>, std::equal_to<std::string_view>> extensionSet {
		VK_KHR_SURFACE_EXTENSION_NAME,
	};

	uint32_t count;

	std::vector<VkLayerProperties> layerProps;
	DynamicVK::vkEnumerateInstanceLayerProperties(&count, nullptr);
	layerProps.resize(count);
	DynamicVK::vkEnumerateInstanceLayerProperties(&count, layerProps.data());

	bool foundValidationLayer = false;

	for (const auto& prop: layerProps) {
		if (strcmp(prop.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
			foundValidationLayer = true;
			break;
		}
	}

	if (!foundValidationLayer) {
		std::cerr << "Validation layer requested but not available. Ignoring..." << std::endl;
		enableValidation = false;
	}

	if (enableValidation) {
		extensionSet.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	for (size_t i = 0; i < additionalExtensionCount; ++i) {
		extensionSet.insert(additionalExtensions[i]);
	}

	std::vector<const char*> extensions(extensionSet.begin(), extensionSet.end());
	std::vector<const char*> layers;

	if (enableValidation) {
		layers.push_back("VK_LAYER_KHRONOS_validation");
	}

	VkApplicationInfo appInfo {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Indium";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Indium";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.enabledExtensionCount = extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledLayerCount = layers.size();
	createInfo.ppEnabledLayerNames = layers.data();

	VkValidationFeaturesEXT valFeat {};
	std::vector<VkValidationFeatureEnableEXT> valFeatures {
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	};
	valFeat.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	valFeat.enabledValidationFeatureCount = valFeatures.size();
	valFeat.pEnabledValidationFeatures = valFeatures.data();

	if (enableValidation) {
		createInfo.pNext = &valFeat;
	}

	auto result = DynamicVK::vkCreateInstance(&createInfo, nullptr, &globalInstance);
	if (result != VK_SUCCESS) {
		// TODO: maybe warn?
		return;
	}

	if (enableValidation && DynamicVK::vkCreateDebugUtilsMessengerEXT) {
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {};
		debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugCreateInfo.messageSeverity = /* VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | */ VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugCreateInfo.pfnUserCallback = debugCallback;

		if (DynamicVK::vkCreateDebugUtilsMessengerEXT(globalInstance, &debugCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
			// TODO: maybe warn?
		}
	}

	initGlobalDeviceList();
};

void Indium::finit() {
	finitGlobalDeviceList();

	if (debugMessenger && DynamicVK::vkDestroyDebugUtilsMessengerEXT) {
		DynamicVK::vkDestroyDebugUtilsMessengerEXT(globalInstance, debugMessenger, nullptr);
	}

	DynamicVK::vkDestroyInstance(globalInstance, nullptr);
	globalInstance = VK_NULL_HANDLE;

	DynamicVK::finit();

	Iridium::finit();
};
