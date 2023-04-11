#include <indium/dynamic-vk.hpp>

#ifdef DARLING
	#include <elfcalls.h>
	extern "C" struct elf_calls* _elfcalls;
#endif

static void* libraryHandle = NULL;
PFN_vkGetInstanceProcAddr Indium::DynamicVK::vkGetInstanceProcAddr = NULL;
PFN_vkCreateInstance Indium::DynamicVK::vkCreateInstance = NULL;
PFN_vkEnumerateInstanceLayerProperties Indium::DynamicVK::vkEnumerateInstanceLayerProperties = NULL;

#define DYNAMICVK_FUNCTION_DEF(_name) \
	Indium::DynamicVK::DynamicFunction<PFN_ ## _name> Indium::DynamicVK::_name(#_name);

INDIUM_DYNAMICVK_FUNCTION_FOREACH(DYNAMICVK_FUNCTION_DEF)

// these functions may be invoked during exit (by destructors for global variables like `Indium::globalDeviceList`).
// trying to resolve them during exit may cause segfaults, so let's resolve them eagerly at initialization-time instead.
static Indium::DynamicVK::DynamicFunctionBase* const eagerlyResolvedFunctions[] = {
	&Indium::DynamicVK::vkDestroyCommandPool,
	&Indium::DynamicVK::vkDestroySemaphore,
	&Indium::DynamicVK::vkDestroyDevice,
};

#ifdef DARLING
	#define DLSYM _elfcalls->dlsym
	#define DLCLOSE _elfcalls->dlclose
#else
	#define DLSYM dlsym
	#define DLCLOSE dlclose
#endif

bool Indium::DynamicVK::init() {
#ifdef DARLING
	libraryHandle = _elfcalls->dlopen("libvulkan.so.1");
#else
	libraryHandle = dlopen("libvulkan.so.1", RTLD_LAZY);
#endif

	if (!libraryHandle) {
		return false;
	}

	vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(DLSYM(libraryHandle, "vkGetInstanceProcAddr"));

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

	return true;
};

void Indium::DynamicVK::finit() {
	vkCreateInstance = NULL;
	vkGetInstanceProcAddr = NULL;
	if (libraryHandle) {
		DLCLOSE(libraryHandle);
		libraryHandle = NULL;
	}
};

bool Indium::DynamicVK::eagerlyResolveRequired() {
	for (size_t i = 0; i < sizeof(eagerlyResolvedFunctions) / sizeof(*eagerlyResolvedFunctions); ++i) {
		if (!eagerlyResolvedFunctions[i]->resolve()) {
			// failed to resolve a required function
			return false;
		}
	}
	return true;
};
