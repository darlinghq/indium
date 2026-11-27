#pragma once

#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include <indium/instance.private.hpp>

#include <mutex>
#include <stdexcept>
#include <string>

namespace Indium {
	namespace DynamicVK {
		extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
		extern PFN_vkCreateInstance vkCreateInstance;
		extern PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
		extern PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;

		bool init();
		void finit();

		/**
		 * Eagerly resolves any and all required Vulkan functions.
		 *
		 * @note The global Vulkan instance (`globalInstance`) must be created prior to calling this function.
		 */
		bool eagerlyResolveRequired();

		struct DynamicFunctionBase {
			void* pointer;
			std::once_flag _resolveFlag;
			const char* name;

			DynamicFunctionBase(const char* _name):
				name(_name)
				{};

			bool resolve() {
				std::call_once(_resolveFlag, [&]() {
					// Both halves of this guard are load-bearing. `vkGetInstanceProcAddr` is NULL
					// before `init()` runs and again after `finit()`, and `finit()` is reached from
					// a destructor at process exit whether or not `init()` ever ran, so a function
					// first resolved at that point called through a null pointer. `globalInstance` is
					// the same story one step earlier: it is VK_NULL_HANDLE until vkCreateInstance
					// succeeds, and every entry point here is instance- or device-level, for which
					// vkGetInstanceProcAddr is only defined with a real instance (Vulkan spec,
					// "vkGetInstanceProcAddr"). Either way the answer is "not available", which is
					// what every caller of resolve() already handles for a missing entry point.
					pointer = (vkGetInstanceProcAddr && globalInstance != VK_NULL_HANDLE)
						? reinterpret_cast<void*>(vkGetInstanceProcAddr(globalInstance, name))
						: nullptr;
				});
				return !!pointer;
			};

			bool isAvailable() {
				return resolve();
			};

			operator bool() {
				return isAvailable();
			};
		};

		template<typename FuncPtr>
		struct DynamicFunction;

		template<typename Ret, typename... Arg>
		struct DynamicFunction<Ret(*)(Arg...)>: public DynamicFunctionBase {
			DynamicFunction(const char* _name):
				DynamicFunctionBase(_name)
				{};

			Ret operator ()(Arg... arg) {
				if (!resolve()) {
					throw std::runtime_error(std::string("Failed to resolve ") + name);
				}
				Ret (*func)(Arg...) = reinterpret_cast<decltype(func)>(pointer);
				return func(std::forward<Arg>(arg)...);
			};
		};

		#define INDIUM_DYNAMICVK_FUNCTION_FOREACH(_macro) \
			_macro(vkAcquireNextImageKHR) \
			_macro(vkAllocateCommandBuffers) \
			_macro(vkAllocateDescriptorSets) \
			_macro(vkAllocateMemory) \
			_macro(vkBeginCommandBuffer) \
			_macro(vkBindBufferMemory) \
			_macro(vkBindImageMemory) \
			_macro(vkCmdBeginQuery) \
			_macro(vkCmdBeginRenderPass) \
			_macro(vkCmdBindDescriptorSets) \
			_macro(vkCmdBindIndexBuffer) \
			_macro(vkCmdBindPipeline) \
			_macro(vkCmdBindVertexBuffers) \
			_macro(vkCmdBlitImage) \
			_macro(vkCmdCopyBuffer) \
			_macro(vkCmdCopyBufferToImage) \
			_macro(vkCmdCopyImage) \
			_macro(vkCmdResolveImage) \
			_macro(vkCmdCopyImageToBuffer) \
			_macro(vkCmdCopyQueryPoolResults) \
			_macro(vkCmdResetQueryPool) \
			_macro(vkCmdDispatch) \
			_macro(vkCmdDispatchIndirect) \
			_macro(vkCmdDraw) \
			_macro(vkCmdDrawIndexed) \
			_macro(vkCmdDrawIndexedIndirect) \
			_macro(vkCmdDrawIndirect) \
			_macro(vkCmdEndQuery) \
			_macro(vkCmdEndRenderPass) \
			_macro(vkCmdWriteTimestamp) \
			_macro(vkCmdFillBuffer) \
			_macro(vkCmdPipelineBarrier) \
			_macro(vkCmdPipelineBarrier2) \
			_macro(vkCmdSetBlendConstants) \
			_macro(vkCmdSetCullMode) \
			_macro(vkCmdSetDepthBias) \
			_macro(vkCmdSetDepthBiasEnable) \
			_macro(vkCmdSetDepthBoundsTestEnable) \
			_macro(vkCmdSetDepthCompareOp) \
			_macro(vkCmdSetDepthTestEnable) \
			_macro(vkCmdSetDepthWriteEnable) \
			_macro(vkCmdSetFrontFace) \
			_macro(vkCmdSetPrimitiveTopology) \
			_macro(vkCmdSetRasterizerDiscardEnable) \
			_macro(vkCmdSetScissorWithCount) \
			_macro(vkCmdSetStencilCompareMask) \
			_macro(vkCmdSetStencilOp) \
			_macro(vkCmdSetStencilReference) \
			_macro(vkCmdSetStencilTestEnable) \
			_macro(vkCmdSetStencilWriteMask) \
			_macro(vkCmdSetViewportWithCount) \
			_macro(vkCreateBuffer) \
			_macro(vkCreateCommandPool) \
			_macro(vkCreateComputePipelines) \
			_macro(vkCreateDebugUtilsMessengerEXT) \
			_macro(vkCreateDescriptorPool) \
			_macro(vkCreateDescriptorSetLayout) \
			_macro(vkCreateDevice) \
			_macro(vkCreateFence) \
			_macro(vkCreateFramebuffer) \
			_macro(vkCreateGraphicsPipelines) \
			_macro(vkCreateBufferView) \
			_macro(vkCreateImage) \
			_macro(vkCreateImageView) \
			_macro(vkCreatePipelineCache) \
			_macro(vkCreatePipelineLayout) \
			_macro(vkCreateQueryPool) \
			_macro(vkCreateRenderPass) \
			_macro(vkCreateSampler) \
			_macro(vkCreateSemaphore) \
			_macro(vkCreateShaderModule) \
			_macro(vkCreateSwapchainKHR) \
			_macro(vkDestroyBuffer) \
			_macro(vkDestroyCommandPool) \
			_macro(vkResetCommandPool) \
			_macro(vkDestroyDebugUtilsMessengerEXT) \
			_macro(vkDestroyDescriptorPool) \
			_macro(vkDestroyDescriptorSetLayout) \
			_macro(vkDestroyDevice) \
			_macro(vkDestroyFence) \
			_macro(vkDestroyFramebuffer) \
			_macro(vkDestroyBufferView) \
			_macro(vkDestroyImage) \
			_macro(vkDestroyImageView) \
			_macro(vkDestroyInstance) \
			_macro(vkDestroyPipeline) \
			_macro(vkDestroyPipelineCache) \
			_macro(vkDestroyPipelineLayout) \
			_macro(vkDestroyQueryPool) \
			_macro(vkDestroyRenderPass) \
			_macro(vkDestroySampler) \
			_macro(vkDestroySemaphore) \
			_macro(vkDestroyShaderModule) \
			_macro(vkDestroySurfaceKHR) \
			_macro(vkDestroySwapchainKHR) \
			_macro(vkEndCommandBuffer) \
			_macro(vkEnumerateDeviceExtensionProperties) \
			_macro(vkEnumeratePhysicalDevices) \
			_macro(vkFlushMappedMemoryRanges) \
			_macro(vkInvalidateMappedMemoryRanges) \
			_macro(vkFreeCommandBuffers) \
			_macro(vkFreeMemory) \
			_macro(vkGetBufferDeviceAddress) \
			_macro(vkGetCalibratedTimestampsEXT) \
			_macro(vkGetBufferMemoryRequirements) \
			_macro(vkGetDeviceQueue) \
			_macro(vkGetImageMemoryRequirements) \
			_macro(vkGetImageSubresourceLayout) \
			_macro(vkGetMemoryFdPropertiesKHR) \
			_macro(vkGetMemoryHostPointerPropertiesEXT) \
			_macro(vkGetPipelineCacheData) \
			_macro(vkGetPhysicalDeviceFeatures2) \
			_macro(vkGetPhysicalDeviceFormatProperties) \
			_macro(vkGetPhysicalDeviceImageFormatProperties) \
			_macro(vkGetPhysicalDeviceImageFormatProperties2) \
			_macro(vkGetPhysicalDeviceMemoryProperties) \
			_macro(vkGetPhysicalDeviceMemoryProperties2) \
			_macro(vkGetPhysicalDeviceProperties) \
			_macro(vkGetPhysicalDeviceProperties2) \
			_macro(vkGetPhysicalDeviceQueueFamilyProperties) \
			_macro(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
			_macro(vkGetPhysicalDeviceSurfaceFormatsKHR) \
			_macro(vkGetPhysicalDeviceSurfaceSupportKHR) \
			_macro(vkGetPhysicalDeviceSurfacePresentModesKHR) \
			_macro(vkGetSemaphoreCounterValue) \
			_macro(vkGetSwapchainImagesKHR) \
			_macro(vkMapMemory) \
			_macro(vkMergePipelineCaches) \
			_macro(vkDeviceWaitIdle) \
			_macro(vkQueuePresentKHR) \
			_macro(vkQueueSubmit) \
			_macro(vkQueueSubmit2) \
			_macro(vkGetQueryPoolResults) \
			_macro(vkResetQueryPool) \
			_macro(vkSignalSemaphore) \
			_macro(vkUnmapMemory) \
			_macro(vkUpdateDescriptorSets) \
			_macro(vkWaitForFences) \
			_macro(vkWaitForPresentKHR) \
			_macro(vkResetFences) \
			_macro(vkWaitSemaphores)

		#define INDIUM_DYNAMICVK_FUNCTION_DECL(_name) \
			extern DynamicFunction<PFN_ ## _name> _name;

		INDIUM_DYNAMICVK_FUNCTION_FOREACH(INDIUM_DYNAMICVK_FUNCTION_DECL)

		// Window system surface entry points. They are declared here with plain pointer types
		// instead of the platform's (Xlib's Display and Window, Wayland's wl_display and wl_surface)
		// so that no window system header is needed; the arguments are laid out as the
		// Vulkan specification defines them for VK_KHR_xlib_surface and VK_KHR_wayland_surface.
		// They resolve only when the instance was created with the matching extension.
		extern DynamicFunction<VkResult (*)(VkInstance, const void* /* Xlib or Wayland create info */, const VkAllocationCallbacks*, VkSurfaceKHR*)> vkCreateXlibSurfaceKHR;
		extern DynamicFunction<VkResult (*)(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*)> vkCreateWaylandSurfaceKHR;
		extern DynamicFunction<VkBool32 (*)(VkPhysicalDevice, uint32_t, void* /* Display* */, unsigned long /* VisualID */)> vkGetPhysicalDeviceXlibPresentationSupportKHR;
		extern DynamicFunction<VkBool32 (*)(VkPhysicalDevice, uint32_t, void* /* wl_display* */)> vkGetPhysicalDeviceWaylandPresentationSupportKHR;
	};
};
