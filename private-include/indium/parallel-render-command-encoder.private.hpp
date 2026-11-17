// Copyright (C) 2026 Lubos Dolezel

#pragma once

#include <indium/parallel-render-command-encoder.hpp>
#include <indium/render-pass.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Indium {
	class PrivateCommandBuffer;
	class PrivateRenderCommandEncoder;

	/**
	 * A render command encoder that keeps the calls it gets and plays them on another encoder later, so that the
	 * encoders of a parallel pass can be used from any thread and in any order, then played in the order of creation.
	 * It holds on to the objects it was given, as an encoder that recorded them would.
	 */
	class RecordingRenderCommandEncoder: public RenderCommandEncoder {
	public:
		~RecordingRenderCommandEncoder() override;

		/** Plays every call recorded so far, in order, on `encoder`. */
		void replay(RenderCommandEncoder& encoder);

		virtual void endEncoding() override;

		virtual void setRenderPipelineState(const std::shared_ptr<RenderPipelineState>& renderPipelineState) override;
		virtual void setFrontFacingWinding(Winding frontFaceWinding) override;
		virtual void setCullMode(CullMode cullMode) override;
		virtual void setDepthBias(float depthBias, float slopeScale, float clamp) override;
		virtual void setDepthClipMode(DepthClipMode depthClipMode) override;
		virtual void setViewport(const Viewport& viewport) override;
		virtual void setViewports(const Viewport* viewports, size_t count) override;
		virtual void setViewports(const std::vector<Viewport>& viewports) override;
		virtual void setScissorRect(const ScissorRect& scissorRect) override;
		virtual void setScissorRects(const ScissorRect* scissorRects, size_t count) override;
		virtual void setScissorRects(const std::vector<ScissorRect>& scissorRects) override;
		virtual void setBlendColor(float red, float green, float blue, float alpha) override;
		virtual void setDepthStencilState(const std::shared_ptr<DepthStencilState>& state) override;
		virtual void setTriangleFillMode(TriangleFillMode triangleFillMode) override;
		virtual void setStencilReferenceValue(uint32_t value) override;
		virtual void setStencilReferenceValue(uint32_t front, uint32_t back) override;
		virtual void setVisibilityResultMode(VisibilityResultMode mode, size_t offset) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) override;
		virtual void setVertexBytes(const void* bytes, size_t length, size_t index) override;
		virtual void setVertexBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) override;
		virtual void setVertexBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) override;
		virtual void setVertexBufferOffset(size_t offset, size_t index) override;
		virtual void setVertexSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) override;
		virtual void setVertexSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) override;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) override;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) override;
		virtual void setVertexTexture(const std::shared_ptr<Texture>& texture, size_t index) override;
		virtual void setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) override;
		virtual void setFragmentBytes(const void* bytes, size_t length, size_t index) override;
		virtual void setFragmentBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) override;
		virtual void setFragmentBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) override;
		virtual void setFragmentBufferOffset(size_t offset, size_t index) override;
		virtual void setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) override;
		virtual void setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) override;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) override;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) override;
		virtual void setFragmentTexture(const std::shared_ptr<Texture>& texture, size_t index) override;
		virtual void setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) override;
		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) override;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) override;
		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) override;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) override;
		virtual void useHeap(const std::shared_ptr<Heap>& heap) override;
		virtual void useHeap(const std::shared_ptr<Heap>& heap, RenderStages stages) override;
		virtual void useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) override;
		virtual void useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps, RenderStages stages) override;
		virtual void memoryBarrier(BarrierScope scope, RenderStages after, RenderStages before) override;
		virtual void updateFence(const std::shared_ptr<Fence>& fence, RenderStages after) override;
		virtual void waitForFence(const std::shared_ptr<Fence>& fence, RenderStages before) override;

	private:
		void record(std::function<void(RenderCommandEncoder&)> call);

		std::vector<std::function<void(RenderCommandEncoder&)>> _calls;
	};

	class PrivateParallelRenderCommandEncoder: public ParallelRenderCommandEncoder {
	public:
		PrivateParallelRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor);
		~PrivateParallelRenderCommandEncoder() override;

		virtual std::shared_ptr<RenderCommandEncoder> renderCommandEncoder() override;
		virtual void setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) override;
		virtual void setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) override;
		virtual void setDepthStoreAction(StoreAction storeAction) override;
		virtual void setDepthStoreActionOptions(StoreActionOptions options) override;
		virtual void setStencilStoreAction(StoreAction storeAction) override;
		virtual void setStencilStoreActionOptions(StoreActionOptions options) override;
		virtual void endEncoding() override;

	private:
		std::weak_ptr<PrivateCommandBuffer> _commandBuffer;
		std::mutex _mutex;
		// the pass as it is when it ends: the store actions can change until then
		RenderPassDescriptor _descriptor;
		std::vector<std::shared_ptr<RecordingRenderCommandEncoder>> _encoders;
		bool _ended = false;
	};
};
