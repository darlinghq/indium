#include <indium/dynamic-vk.hpp>

#ifdef DARLING
	#include <elfcalls.h>
	extern "C" struct elf_calls* _elfcalls;
#endif

void* Indium::DynamicVK::libraryHandle = NULL;
PFN_vkGetInstanceProcAddr Indium::DynamicVK::vkGetInstanceProcAddr = NULL;
PFN_vkCreateInstance Indium::DynamicVK::vkCreateInstance = NULL;
PFN_vkEnumerateInstanceLayerProperties Indium::DynamicVK::vkEnumerateInstanceLayerProperties = NULL;

#define DYNAMICVK_FUNCTION_DEF(_name) \
	Indium::DynamicVK::DynamicFunction<PFN_ ## _name> Indium::DynamicVK::_name(#_name);

INDIUM_DYNAMICVK_FUNCTION_FOREACH(DYNAMICVK_FUNCTION_DEF)

// these functions may be invoked during exit (by destructors for global variables like `Indium::globalDeviceList`).
// trying to resolve them during exit causes segfaults, so let's resolve them eagerly at initialization-time instead.
static Indium::DynamicVK::DynamicFunctionBase* const eagerlyResolvedFunctions[] = {
	&Indium::DynamicVK::vkDestroyCommandPool,
	&Indium::DynamicVK::vkDestroySemaphore,
	&Indium::DynamicVK::vkDestroyDevice,
};

bool Indium::DynamicVK::init() {
#ifdef DARLING
	// check if the host system has a Vulkan library available
	// (since our wrapper will die if it fails to load the host library)
	void* tmp = _elfcalls->dlopen("libvulkan.so.1");
	if (!tmp) {
		// nope, host has no (compatible) Vulkan library
		return false;
	}
	// keep it open to avoid having to reopen it when we load the wrapper

	libraryHandle = dlopen("/usr/lib/native/libVulkan.dylib", RTLD_LAZY);

	// now we can close the native handle
	_elfcalls->dlclose(tmp);
#else
	libraryHandle = dlopen("libvulkan.so.1", RTLD_LAZY);
#endif

	if (!libraryHandle) {
		return false;
	}

	vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(libraryHandle, "vkGetInstanceProcAddr"));

	if (!vkGetInstanceProcAddr) {
		return false;
	}

	vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(NULL, "vkCreateInstance"));

	if (!vkCreateInstance) {
		return false;
	}

	vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceLayerProperties"));

	if (!vkEnumerateInstanceLayerProperties) {
		return false;
	}

	for (size_t i = 0; i < sizeof(eagerlyResolvedFunctions) / sizeof(*eagerlyResolvedFunctions); ++i) {
		if (!eagerlyResolvedFunctions[i]->resolve()) {
			// failed to resolve a required function
			return false;
		}
	}

	return true;
};

void Indium::DynamicVK::finit() {
	vkCreateInstance = NULL;
	vkGetInstanceProcAddr = NULL;
	if (libraryHandle) {
		dlclose(libraryHandle);
		libraryHandle = NULL;
	}
};
