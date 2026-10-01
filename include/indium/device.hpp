#pragma once

#include <string>
#include <memory>
#include <cstdint>

#include <indium/init.hpp>
#include <indium/base.hpp>
#include <indium/types.hpp>
#include <indium/library.hpp>

namespace Indium {
	class CommandQueue;
	class RenderPipelineState;
	struct RenderPipelineDescriptor;
	class Buffer;
	class Library;
	class Texture;
	struct TextureDescriptor;
	class SamplerState;
	struct SamplerDescriptor;
	class DepthStencilState;
	struct DepthStencilDescriptor;
	class ComputePipelineState;
	struct ComputePipelineDescriptor;
	class ComputePipelineReflection;
	class Function;

	class Device {
	public:
		virtual ~Device() = 0;

		virtual std::string name() const = 0;

		/**
		 * An approximation of how much memory this device can use with good
		 * performance, in bytes. Keeping the total size of all resources and
		 * heaps below this threshold avoids overcommitting the device and the
		 * performance penalty that comes with it.
		 *
		 * This is a soft limit, not a hard one: exceeding it is allowed and
		 * merely costs performance. It is also a point-in-time figure. A driver
		 * that reports a memory budget will see it move as the rest of the
		 * system allocates, so two calls need not agree.
		 */
		virtual uint64_t recommendedMaxWorkingSetSize() const = 0;

		virtual std::shared_ptr<CommandQueue> newCommandQueue() = 0;
		virtual std::shared_ptr<RenderPipelineState> newRenderPipelineState(const RenderPipelineDescriptor& descriptor) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(const ComputePipelineDescriptor& descriptor, PipelineOption options, std::shared_ptr<ComputePipelineReflection> reflection) = 0;
		virtual std::shared_ptr<ComputePipelineState> newComputePipelineState(std::shared_ptr<Function> computeFunction, PipelineOption options = PipelineOption::None, std::shared_ptr<ComputePipelineReflection> reflection = nullptr) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(size_t length, ResourceOptions options) = 0;
		virtual std::shared_ptr<Buffer> newBuffer(const void* pointer, size_t length, ResourceOptions options) = 0;
		/**
		 * Translates Metal Shading Language source and creates a library from
		 * it.
		 *
		 * @returns `nullptr` if the library could not be translated.
		 */
		virtual std::shared_ptr<Library> newLibrary(const void* data, size_t length) = 0;
		/**
		 * Creates a library from a module some other producer already compiled
		 * to SPIR-V. The module must target Vulkan and must use the conventions
		 * indium binds resources with: buffer addresses arriving through one
		 * uniform buffer per stage at descriptor set 0, binding 0, and the
		 * image and sampler bindings named by `reflection`.
		 *
		 * Neither argument is owned. `spirv` is read only for the duration of
		 * this call, because `vkCreateShaderModule` copies the code it is
		 * given, and `reflection` is copied into the library. The caller may
		 * free, reuse or destroy both as soon as this returns. This is the
		 * opposite of the source overload above, which receives a buffer that
		 * Iridium allocated and therefore frees.
		 *
		 * @returns `nullptr`, with a description of the problem in
		 *          `errorMessage` when it is not null and empty when it is not,
		 *          if:
		 *          - `spirv` is null, shorter than a SPIR-V header, not a whole
		 *            number of words, or does not start with the SPIR-V magic
		 *            number;
		 *          - `reflection` describes no function, describes a function
		 *            with no name or a stage indium cannot bind resources for,
		 *            or names a function whose name does not occur in `spirv`;
		 *          - two bindings of one function claim the same descriptor
		 *            binding number;
		 *          - a binding or an embedded sampler field holds a value
		 *            outside its enumeration.
		 *
		 * A `nullptr` return means nothing was created, so a caller never has
		 * to cope with a library whose bindings were dropped or defaulted.
		 *
		 * @note A name that occurs in the module but is not an entry point of
		 *       it is not detected here; indium finds that out when the
		 *       pipeline is created. Producing reflection keys that match the
		 *       module's entry points is the producer's responsibility.
		 */
		virtual std::shared_ptr<Library> newLibrary(const void* spirv, size_t length, const LibraryReflection& reflection, std::string* errorMessage = nullptr) = 0;
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor) = 0;
		virtual std::shared_ptr<SamplerState> newSamplerState(const SamplerDescriptor& descriptor) = 0;
		virtual std::shared_ptr<DepthStencilState> newDepthStencilState(const DepthStencilDescriptor& descriptor) = 0;

		// --- support api ---

		/**
		 * @note This method should only be called from a single thread at a time.
		 *       It *can* be called by different threads, but not simultaneously.
		 *       If it is called by multiple threads simultaneously, only one will
		 *       poll at a time; all others will be left waiting to poll.
		 */
		virtual void pollEvents(uint64_t timeoutNanoseconds) = 0;
		virtual void wakeupEventLoop() = 0;
	};

	std::shared_ptr<Device> createSystemDefaultDevice();
};
