#pragma once

#include <indium/render-command-encoder.hpp>
#include <indium/render-pass.hpp>
#include <indium/sampler.private.hpp>
#include <indium/device.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <variant>
#include <vector>

namespace Indium {
	class PrivateCommandBuffer;
	class PrivateRenderPipelineState;
	class PrivateDevice;
	class SamplerState;

	class PrivateRenderCommandEncoder: public RenderCommandEncoder {
	private:
		// we keep a copy because we need to keep some of the resources it contains alive
		RenderPassDescriptor _descriptor;
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;
		std::shared_ptr<PrivateRenderPipelineState> _privatePSO;
		VkFramebuffer _framebuffer = nullptr;
		VkRenderPass _renderPass = nullptr;
		VkDescriptorPool _pool = nullptr;

		struct FunctionResources {
			std::vector<std::pair<std::shared_ptr<Buffer>, size_t>> buffers;
			std::vector<std::shared_ptr<Texture>> textures;
			std::vector<std::shared_ptr<SamplerState>> samplers;

			void setBytes(std::shared_ptr<Device> device, const void* bytes, size_t length, size_t index) {
				// TODO: we can make this "Private" instead
				auto buf = device->newBuffer(bytes, length, ResourceOptions::StorageModeShared);

				if (buffers.size() <= index) {
					buffers.resize(index + 1);
				}

				buffers[index] = std::make_pair(buf, 0);
			};

			void setBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
				if (buffers.size() <= index) {
					buffers.resize(index + 1);
				}

				buffers[index] = std::make_pair(buffer, offset);
			};

			void setBufferOffset(size_t offset, size_t index) {
				buffers[index].second = offset;
			};

			void setSamplerState(std::shared_ptr<SamplerState> state, std::optional<std::pair<float, float>> lodClamps, size_t index) {
				if (samplers.size() <= index) {
					samplers.resize(index + 1);
				}

				if (lodClamps) {
					auto privateState = std::dynamic_pointer_cast<PrivateSamplerState>(state);
					samplers[index] = privateState->cloneWithClamps(lodClamps->first, lodClamps->second);
				} else {
					samplers[index] = state;
				}
			};

			void setTexture(std::shared_ptr<Texture> texture, size_t index) {
				if (textures.size() <= index) {
					textures.resize(index + 1);
				}

				textures[index] = texture;
			};
		};

		std::array<FunctionResources, 2> _functionResources {};
		std::vector<std::shared_ptr<Buffer>> _keepAliveBuffers;

		void updateBindings();

	public:
		PrivateRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor);
		~PrivateRenderCommandEncoder();

		virtual void setRenderPipelineState(std::shared_ptr<RenderPipelineState> renderPipelineState) override;
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
		virtual void setDepthStencilState(std::shared_ptr<DepthStencilState> state) override;
		virtual void setTriangleFillMode(TriangleFillMode triangleFillMode) override;
		virtual void setStencilReferenceValue(uint32_t value) override;
		virtual void setStencilReferenceValue(uint32_t front, uint32_t back) override;
		virtual void setVisibilityResultMode(VisibilityResultMode mode, size_t offset) override;

		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) override;

		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset) override;

		virtual void setVertexBytes(const void* bytes, size_t length, size_t index) override;
		virtual void setVertexBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) override;
		virtual void setVertexBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) override;
		virtual void setVertexBufferOffset(size_t offset, size_t index) override;
		virtual void setVertexSamplerState(std::shared_ptr<SamplerState> state, size_t index) override;
		virtual void setVertexSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) override;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) override;
		virtual void setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) override;
		virtual void setVertexTexture(std::shared_ptr<Texture> texture, size_t index) override;
		virtual void setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) override;

		virtual void setFragmentBytes(const void* bytes, size_t length, size_t index) override;
		virtual void setFragmentBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) override;
		virtual void setFragmentBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) override;
		virtual void setFragmentBufferOffset(size_t offset, size_t index) override;
		virtual void setFragmentSamplerState(std::shared_ptr<SamplerState> state, size_t index) override;
		virtual void setFragmentSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) override;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) override;
		virtual void setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) override;
		virtual void setFragmentTexture(std::shared_ptr<Texture> texture, size_t index) override;
		virtual void setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) override;

		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) override;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) override;
		virtual void useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) override;
		virtual void useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) override;

		virtual void endEncoding() override;

		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadOnlyTextures);
		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadWriteTextures);
	};
};
