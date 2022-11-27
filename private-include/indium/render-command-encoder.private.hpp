#pragma once

#include <indium/render-command-encoder.hpp>
#include <indium/render-pass.hpp>

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
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) override;
		virtual void drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount) override;
		virtual void drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset) override;
		virtual void setVertexBytes(const void* bytes, size_t length, size_t index) override;
		virtual void setVertexBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) override;
		virtual void setFragmentTexture(std::shared_ptr<Texture> texture, size_t index) override;
		virtual void setDepthStencilState(std::shared_ptr<DepthStencilState> state) override;

		virtual void endEncoding() override;

		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadOnlyTextures);
		INDIUM_PROPERTY_OBJECT_VECTOR(Texture, r, R,eadWriteTextures);
	};
};
