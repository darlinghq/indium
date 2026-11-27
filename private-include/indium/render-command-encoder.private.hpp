#pragma once

#include <indium/render-command-encoder.hpp>
#include <indium/render-pass.hpp>
#include <indium/sampler.private.hpp>
#include <indium/device.hpp>
#include <indium/command-encoder.private.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <variant>
#include <vector>

namespace Indium {
	class PrivateCommandBuffer;
	class PrivateRenderPipelineState;
	class PrivateDevice;
	class SamplerState;
	class VisibilityResults;
	class PipelineTimer;

	class PrivateRenderCommandEncoder: public RenderCommandEncoder {
	private:
		// we keep a copy because we need to keep some of the resources it contains alive
		RenderPassDescriptor _descriptor;
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;
		std::shared_ptr<PrivateRenderPipelineState> _privatePSO;
		// made when a pipeline is first set with pipeline timing on (Device::setPipelineTimingEnabled())
		std::shared_ptr<PipelineTimer> _timer;
		VkFramebuffer _framebuffer = VK_NULL_HANDLE;
		VkRenderPass _renderPass = VK_NULL_HANDLE;
		// whether _renderPass is shared through the device's render pass cache
		bool _renderPassDeviceOwned = false;
		DescriptorPoolChain _descriptorPools;

		// per-draw rasterization state that is part of the pipeline in Vulkan (see
		// PrivateRenderPipelineState::pipelineFor()); the values Metal starts an encoder with
		TriangleFillMode _triangleFillMode = TriangleFillMode::Fill;
		DepthClipMode _depthClipMode = DepthClipMode::Clip;

		// the occlusion queries behind the visibility results; null without a visibility result buffer
		std::unique_ptr<VisibilityResults> _visibilityResults;

		std::vector<FunctionResources> _savedFunctionResources;

		std::array<FunctionResources, 2> _functionResources {};
		std::vector<std::shared_ptr<Buffer>> _keepAliveBuffers;
		ScratchArena _scratch;

		// whether the pass is ordered against other commands by what it accesses (see HazardTracker)
		bool _tracked = true;
		// what the draws and the declarations of the pass access, ordered when the pass ends
		HazardAccessSet _accesses;
		// the pass waits for a fence: everything before it is ordered before the pass
		bool _waitsForFence = false;

		// color attachments whose multisample resolve is part of the pass itself; the others are resolved,
		// if their store action says so by the time the pass ends, after it (see endEncoding())
		std::vector<bool> _resolvedInPass;
		void resolveLateColorAttachments(VkCommandBuffer commandBuffer);
		void roundExtendedRangeAttachments(VkCommandBuffer commandBuffer);

		/** Notes the resources the next draw reaches through the bindings of the pipeline. */
		void noteBindings();
		/** Notes a buffer the draw reads outside the bindings: indices, indirect arguments. */
		void noteBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t length, VkPipelineStageFlags2 stages, VkAccessFlags2 access);
		/** Gives the tracker what the pass accessed, and returns the barriers that have to come before the pass. */
		HazardTracker::Barriers orderPass();

		/**
		 * Binds the descriptor sets and vertex buffers for the next draw.
		 *
		 * @returns false when the draw cannot be encoded: a vertex buffer the pipeline reads is not bound
		 *          and the device has no valid stand-in for it.
		 */
		bool updateBindings();

		/**
		 * Binds the pipeline of the topology class `primitiveType` belongs to and sets the topology.
		 * A pipeline state must be set.
		 */
		void bindPipeline(VkCommandBuffer commandBuffer, PrimitiveType primitiveType);

		// the viewport and scissor rectangle an encoder starts with
		size_t _defaultWidth = 0;
		size_t _defaultHeight = 0;

		/** Sets the state Metal gives a new encoder (all of it that this encoder tracks or sets in the command buffer). */
		void applyDefaultState();

	public:
		PrivateRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor);
		~PrivateRenderCommandEncoder();

		/**
		 * Puts the encoder back in the state it was made in, apart from the pass itself: no pipeline, bindings or
		 * depth-stencil state, and the default viewport, scissor, culling, blend color and so on. For playing the
		 * commands of one encoder of a parallel pass after those of another, each starting from the defaults.
		 */
		void resetState();

		virtual void sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) override;
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
		virtual void setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) override;
		virtual void setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) override;
		virtual void setDepthStoreAction(StoreAction storeAction) override;
		virtual void setDepthStoreActionOptions(StoreActionOptions options) override;
		virtual void setStencilStoreAction(StoreAction storeAction) override;
		virtual void setStencilStoreActionOptions(StoreActionOptions options) override;
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

		virtual void endEncoding() override;

		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadOnlyTextures);
		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadWriteTextures);
	};
};
