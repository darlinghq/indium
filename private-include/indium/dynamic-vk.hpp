#pragma once

#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include <indium/instance.private.hpp>

#include <mutex>

namespace Indium {
	namespace DynamicVK {
		extern void* libraryHandle;
		extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
		extern PFN_vkCreateInstance vkCreateInstance;
		extern PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;

		bool init();
		void finit();

		struct DynamicFunctionBase {
			void* pointer;
			std::once_flag _resolveFlag;
			const char* name;

			DynamicFunctionBase(const char* _name):
				name(_name)
				{};

			bool resolve() {
				std::call_once(_resolveFlag, [&]() {
					pointer = reinterpret_cast<void*>(vkGetInstanceProcAddr(globalInstance, name));
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
			_macro(vkCmdBeginRenderPass) \
			_macro(vkCmdBindDescriptorSets) \
			_macro(vkCmdBindIndexBuffer) \
			_macro(vkCmdBindPipeline) \
			_macro(vkCmdBindVertexBuffers) \
			_macro(vkCmdBlitImage) \
			_macro(vkCmdCopyBuffer) \
			_macro(vkCmdCopyBufferToImage) \
			_macro(vkCmdCopyImage) \
			_macro(vkCmdCopyImageToBuffer) \
			_macro(vkCmdDispatch) \
			_macro(vkCmdDraw) \
			_macro(vkCmdDrawIndexed) \
			_macro(vkCmdEndRenderPass) \
			_macro(vkCmdFillBuffer) \
			_macro(vkCmdPipelineBarrier) \
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
			_macro(vkCreateImage) \
			_macro(vkCreateImageView) \
			_macro(vkCreatePipelineLayout) \
			_macro(vkCreateRenderPass) \
			_macro(vkCreateSampler) \
			_macro(vkCreateSemaphore) \
			_macro(vkCreateShaderModule) \
			_macro(vkCreateSwapchainKHR) \
			_macro(vkDestroyBuffer) \
			_macro(vkDestroyCommandPool) \
			_macro(vkDestroyDebugUtilsMessengerEXT) \
			_macro(vkDestroyDescriptorPool) \
			_macro(vkDestroyDescriptorSetLayout) \
			_macro(vkDestroyDevice) \
			_macro(vkDestroyFence) \
			_macro(vkDestroyFramebuffer) \
			_macro(vkDestroyImage) \
			_macro(vkDestroyImageView) \
			_macro(vkDestroyInstance) \
			_macro(vkDestroyPipeline) \
			_macro(vkDestroyPipelineLayout) \
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
			_macro(vkFreeCommandBuffers) \
			_macro(vkFreeMemory) \
			_macro(vkGetBufferDeviceAddress) \
			_macro(vkGetBufferMemoryRequirements) \
			_macro(vkGetDeviceQueue) \
			_macro(vkGetImageMemoryRequirements) \
			_macro(vkGetPhysicalDeviceFeatures2) \
			_macro(vkGetPhysicalDeviceMemoryProperties) \
			_macro(vkGetPhysicalDeviceProperties) \
			_macro(vkGetPhysicalDeviceProperties2) \
			_macro(vkGetPhysicalDeviceQueueFamilyProperties) \
			_macro(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
			_macro(vkGetPhysicalDeviceSurfaceFormatsKHR) \
			_macro(vkGetPhysicalDeviceSurfacePresentModesKHR) \
			_macro(vkGetSemaphoreCounterValue) \
			_macro(vkGetSwapchainImagesKHR) \
			_macro(vkMapMemory) \
			_macro(vkQueuePresentKHR) \
			_macro(vkQueueSubmit) \
			_macro(vkQueueSubmit2) \
			_macro(vkSignalSemaphore) \
			_macro(vkUnmapMemory) \
			_macro(vkUpdateDescriptorSets) \
			_macro(vkWaitForFences) \
			_macro(vkWaitSemaphores)

		#define INDIUM_DYNAMICVK_FUNCTION_DECL(_name) \
			extern DynamicFunction<PFN_ ## _name> _name;

		INDIUM_DYNAMICVK_FUNCTION_FOREACH(INDIUM_DYNAMICVK_FUNCTION_DECL)
	};
};
