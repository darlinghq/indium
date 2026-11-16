#pragma once

#include <indium/command-encoder.hpp>
#include <indium/base.hpp>
#include <indium/render-pipeline.hpp>

#include <memory>

namespace Indium {
	class RenderPassDescriptor;
	class CommandBuffer;
	class Buffer;
	class Texture;
	class DepthStencilState;
	class SamplerState;
	class Resource;

	class RenderCommandEncoder: public CommandEncoder {
	public:
		virtual ~RenderCommandEncoder() = 0;

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

		/**
		 * Makes the writes that commands encoded so far make in `after` stages to the resources in
		 * `scope` visible to the `before` stages of the commands encoded afterwards.
		 *
		 * Inside a Vulkan render pass, a later draw's vertex stage cannot wait for an earlier draw's
		 * fragment stage, so a `before` that includes Vertex is only supported when `after` is
		 * Vertex alone; other unsupported combinations throw `std::invalid_argument`.
		 */
		virtual void memoryBarrier(BarrierScope scope, RenderStages after, RenderStages before) = 0;
	};
};
