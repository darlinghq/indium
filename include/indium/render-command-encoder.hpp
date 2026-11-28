#pragma once

#include <indium/command-encoder.hpp>
#include <indium/event.hpp>
#include <indium/counter.hpp>
#include <indium/base.hpp>
#include <indium/render-pipeline.hpp>

#include <memory>

namespace Indium {
	class RenderPassDescriptor;
	class CommandBuffer;
	class Buffer;
	class Heap;
	class Texture;
	class DepthStencilState;
	class SamplerState;
	class Resource;

	class RenderCommandEncoder: public CommandEncoder {
	public:
		virtual ~RenderCommandEncoder() = 0;

		/**
		 * Samples at a draw boundary. This device does not offer draw boundary sampling
		 * (Device::supportsCounterSampling()), so this records nothing; the pass's own start and end
		 * samples come from the render pass descriptor.
		 */
		virtual void sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) = 0;

		/**
		 * Sets the pipeline the following draws use.
		 *
		 * A pipeline whose fragment function reads color attachments (framebuffer fetch, `[[color(n)]]`; reflection
		 * `BindingType::InputAttachment`) reads the attachments of this pass. The first time such a pipeline is set,
		 * the pass continues in one whose subpass can read them, keeping everything drawn so far. Every attachment
		 * the function reads has to be a single-sample color render target of the pass, or std::runtime_error is
		 * thrown and the pipeline is not set. Draws of one pass see each other's writes; primitives of a single
		 * draw that overlap read each other's writes in no defined order.
		 */
		virtual void setRenderPipelineState(const std::shared_ptr<RenderPipelineState>& renderPipelineState) = 0;
		virtual void setFrontFacingWinding(Winding frontFaceWinding) = 0;
		virtual void setCullMode(CullMode cullMode) = 0;
		virtual void setDepthBias(float depthBias, float slopeScale, float clamp) = 0;
		virtual void setDepthClipMode(DepthClipMode depthClipMode) = 0;
		virtual void setViewport(const Viewport& viewport) = 0;
		virtual void setViewports(const Viewport* viewports, size_t count) = 0;
		virtual void setViewports(const std::vector<Viewport>& viewports) = 0;
		virtual void setScissorRect(const ScissorRect& scissorRect) = 0;
		virtual void setScissorRects(const ScissorRect* scissorRects, size_t count) = 0;
		virtual void setScissorRects(const std::vector<ScissorRect>& scissorRects) = 0;
		virtual void setBlendColor(float red, float green, float blue, float alpha) = 0;
		virtual void setDepthStencilState(const std::shared_ptr<DepthStencilState>& state) = 0;
		virtual void setTriangleFillMode(TriangleFillMode triangleFillMode) = 0;
		virtual void setStencilReferenceValue(uint32_t value) = 0;
		virtual void setStencilReferenceValue(uint32_t front, uint32_t back) = 0;

		/**
		 * Sets what happens to an attachment when the pass ends, for an attachment whose store action was left
		 * `StoreAction::Unknown` in the descriptor (or to change it). The last value set before `endEncoding()`
		 * applies. Contents are always kept, which Metal allows for `DontCare`; a multisample resolve that was not
		 * asked for by the descriptor is performed when the pass ends.
		 */
		virtual void setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) = 0;
		virtual void setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) = 0;
		virtual void setDepthStoreAction(StoreAction storeAction) = 0;
		virtual void setDepthStoreActionOptions(StoreActionOptions options) = 0;
		virtual void setStencilStoreAction(StoreAction storeAction) = 0;
		virtual void setStencilStoreActionOptions(StoreActionOptions options) = 0;
		virtual void setVisibilityResultMode(VisibilityResultMode mode, size_t offset) = 0;

		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) = 0;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) = 0;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) = 0;

		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) = 0;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount) = 0;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset) = 0;

		/**
		 * Draws with the arguments the GPU reads from `indirectBuffer` at `indirectBufferOffset`:
		 * four 32-bit unsigned integers (vertex count, instance count, first vertex, base instance).
		 *
		 * `indirectBufferOffset` must be a multiple of 4 and the arguments must lie inside the buffer; a
		 * call that does not satisfy that draws nothing. A base instance other than 0 needs a device that
		 * reports drawIndirectFirstInstance; it is up to the caller to know the device.
		 */
		virtual void drawPrimitives(PrimitiveType primitiveType, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) = 0;

		/**
		 * The indexed form: five 32-bit values (index count, instance count, first index, base vertex as a
		 * signed integer, base instance), the first index counted from `indexBufferOffset`. Same
		 * requirements as the other form.
		 */
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) = 0;

		virtual void setVertexBytes(const void* bytes, size_t length, size_t index) = 0;
		virtual void setVertexBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) = 0;
		virtual void setVertexBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) = 0;
		virtual void setVertexBufferOffset(size_t offset, size_t index) = 0;
		virtual void setVertexSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) = 0;
		virtual void setVertexSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) = 0;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) = 0;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) = 0;
		virtual void setVertexTexture(const std::shared_ptr<Texture>& texture, size_t index) = 0;
		virtual void setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) = 0;

		virtual void setFragmentBytes(const void* bytes, size_t length, size_t index) = 0;
		virtual void setFragmentBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) = 0;
		virtual void setFragmentBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) = 0;
		virtual void setFragmentBufferOffset(size_t offset, size_t index) = 0;
		virtual void setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) = 0;
		virtual void setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) = 0;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) = 0;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) = 0;
		virtual void setFragmentTexture(const std::shared_ptr<Texture>& texture, size_t index) = 0;
		virtual void setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) = 0;

		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) = 0;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) = 0;
		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) = 0;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) = 0;
		/** Declares that the draws may reach every resource of `heap`, for a heap that is tracked. */
		virtual void useHeap(const std::shared_ptr<Heap>& heap) = 0;
		virtual void useHeap(const std::shared_ptr<Heap>& heap, RenderStages stages) = 0;
		virtual void useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) = 0;
		virtual void useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps, RenderStages stages) = 0;

		/**
		 * Makes the writes that commands encoded so far make in `after` stages to the resources in
		 * `scope` visible to the `before` stages of the commands encoded afterwards.
		 *
		 * Inside a Vulkan render pass, a later draw's vertex stage cannot wait for an earlier draw's
		 * fragment stage, so a `before` that includes Vertex is only supported when `after` is
		 * Vertex alone; other unsupported combinations throw `std::invalid_argument`.
		 */
		virtual void memoryBarrier(BarrierScope scope, RenderStages after, RenderStages before) = 0;

		/**
		 * Updates `fence` once the work of the stages `after` of the commands encoded so far has finished. A fence
		 * from another device throws `std::invalid_argument`.
		 */
		virtual void updateFence(const std::shared_ptr<Fence>& fence, RenderStages after) = 0;

		/**
		 * Holds the `before` stages of the commands encoded after this call back until the work `fence` was
		 * updated after in earlier encoders has finished.
		 */
		virtual void waitForFence(const std::shared_ptr<Fence>& fence, RenderStages before) = 0;
	};
};
