#include <indium/render-command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/render-pass.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/render-pipeline.private.hpp>
#include <indium/types.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/library.private.hpp>
#include <indium/sampler.private.hpp>
#include <indium/depth-stencil.private.hpp>
#include <indium/command-encoder.private.hpp>

Indium::RenderCommandEncoder::~RenderCommandEncoder() {};

Indium::PrivateRenderCommandEncoder::PrivateRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_descriptor(descriptor),
	_privateDevice(commandBuffer->privateDevice())
{
	auto buf = _privateCommandBuffer.lock();

	auto vkDevice = _privateDevice->device();
	auto vkCmdBuf = buf->commandBuffer();

	VkDescriptorPoolCreateInfo poolCreateInfo {};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.poolSizeCount = poolSizes.size();
	poolCreateInfo.pPoolSizes = poolSizes.data();
	poolCreateInfo.maxSets = 64; // i guess

	if (vkCreateDescriptorPool(vkDevice, &poolCreateInfo, nullptr, &_pool) != VK_SUCCESS) {
		// TODO
		abort();
	}

	auto firstTexture = descriptor.colorAttachments.front().texture;
	std::vector<VkClearValue> clearValues;

	for (const auto& color: descriptor.colorAttachments) {
		auto clearColor = color.clearColor;
		VkClearValue clearValue {};
		clearValue.color.float32[0] = clearColor.red;
		clearValue.color.float32[1] = clearColor.green;
		clearValue.color.float32[2] = clearColor.blue;
		clearValue.color.float32[3] = clearColor.alpha;
		clearValues.push_back(clearValue);

		// TODO: distinguish between read-only and read-write textures
		_readWriteTextures.push_back(color.texture);
	}

	if (descriptor.depthAttachment || descriptor.stencilAttachment) {
		VkClearValue clearValue {};
		clearValue.depthStencil.depth = descriptor.depthAttachment ? descriptor.depthAttachment->clearDepth : 1.0;
		clearValue.depthStencil.stencil = descriptor.stencilAttachment ? descriptor.stencilAttachment->clearStencil : 0;
		clearValues.push_back(clearValue);
	}

	std::vector<VkAttachmentDescription> renderPassAttachments;
	std::vector<VkSubpassDescription> subpasses;
	std::vector<VkSubpassDependency> dependencies;

	for (const auto& color: descriptor.colorAttachments) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(color.texture);
		VkAttachmentDescription desc {};
		desc.format = pixelFormatToVkFormat(color.texture->pixelFormat());
		desc.samples = VK_SAMPLE_COUNT_1_BIT; // TODO: support multisampling
		desc.loadOp = loadActionToVkAttachmentLoadOp(color.loadAction, true);
		desc.storeOp = storeActionToVkAttachmentStoreOp(color.storeAction, true);
		desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout = (color.loadAction == LoadAction::Load) ? privateTexture->imageLayout() : VK_IMAGE_LAYOUT_UNDEFINED;
		desc.finalLayout = privateTexture->imageLayout();
		renderPassAttachments.push_back(desc);
	}

	if (descriptor.depthAttachment) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(descriptor.depthAttachment->texture);
		VkAttachmentDescription desc {};
		desc.format = pixelFormatToVkFormat(privateTexture->pixelFormat());
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.loadOp = loadActionToVkAttachmentLoadOp(descriptor.depthAttachment->loadAction, false);
		desc.storeOp = storeActionToVkAttachmentStoreOp(descriptor.depthAttachment->storeAction, false);
		desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout = (descriptor.depthAttachment->loadAction == LoadAction::Load) ? privateTexture->imageLayout() : VK_IMAGE_LAYOUT_UNDEFINED;
		desc.finalLayout = privateTexture->imageLayout();
		renderPassAttachments.push_back(desc);
	}

	if (descriptor.stencilAttachment) {
		throw std::runtime_error("TODO: support stencil attachments");
	}

	std::vector<VkAttachmentReference> colorAttachments;
	VkAttachmentReference depthStencilAttachment {};

	size_t index = 0;
	for (const auto& color: descriptor.colorAttachments) {
		VkAttachmentReference ref {};
		ref.attachment = index;
		ref.layout = VK_IMAGE_LAYOUT_GENERAL;
		colorAttachments.push_back(ref);
		++index;
	}

	if (descriptor.depthAttachment) {
		depthStencilAttachment.attachment = index;
		depthStencilAttachment.layout = VK_IMAGE_LAYOUT_GENERAL;
		++index;
	}

	auto& subpassDesc = subpasses.emplace_back();
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.colorAttachmentCount = colorAttachments.size();
	subpassDesc.pColorAttachments = colorAttachments.data();
	subpassDesc.pDepthStencilAttachment = (descriptor.depthAttachment || descriptor.stencilAttachment) ? &depthStencilAttachment : nullptr;

	VkRenderPassCreateInfo renderPassInfo {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = renderPassAttachments.size();
	renderPassInfo.pAttachments = renderPassAttachments.data();
	renderPassInfo.subpassCount = subpasses.size();
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = dependencies.size();
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(vkDevice, &renderPassInfo, nullptr, &_renderPass) != VK_SUCCESS) {
		// TODO
		abort();
	}

	std::vector<VkImageView> framebufferAttachments;

	for (const auto& colorAttachment: descriptor.colorAttachments) {
		framebufferAttachments.push_back(std::dynamic_pointer_cast<PrivateTexture>(colorAttachment.texture)->imageView());
	}

	if (descriptor.depthAttachment) {
		framebufferAttachments.push_back(std::dynamic_pointer_cast<PrivateTexture>(descriptor.depthAttachment->texture)->imageView());
	}

	VkFramebufferCreateInfo framebufferInfo {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = _renderPass;
	framebufferInfo.attachmentCount = framebufferAttachments.size();
	framebufferInfo.pAttachments = framebufferAttachments.data();
	framebufferInfo.width = firstTexture->width();
	framebufferInfo.height = firstTexture->height();
	framebufferInfo.layers = std::dynamic_pointer_cast<PrivateTexture>(firstTexture)->vulkanArrayLength();
	if (vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &_framebuffer) != VK_SUCCESS) {
		// TODO
		abort();
	}

	VkRenderPassBeginInfo renderPassBeginInfo {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = _renderPass;
	renderPassBeginInfo.framebuffer = _framebuffer;
	renderPassBeginInfo.renderArea.extent.width = firstTexture->width();
	renderPassBeginInfo.renderArea.extent.height = firstTexture->height();
	renderPassBeginInfo.clearValueCount = clearValues.size();
	renderPassBeginInfo.pClearValues = clearValues.data();
	vkCmdBeginRenderPass(vkCmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// set default values

	setViewport(Viewport { 0, 0, static_cast<double>(firstTexture->width()), static_cast<double>(firstTexture->height()), 0, 1 });
	setScissorRect(ScissorRect { firstTexture->height(), firstTexture->width(), 0, 0 });
	setCullMode(CullMode::None);
	setFrontFacingWinding(Winding::Clockwise);

	vkCmdSetDepthCompareOp(vkCmdBuf, VK_COMPARE_OP_ALWAYS);
	vkCmdSetDepthBiasEnable(vkCmdBuf, false);
	vkCmdSetDepthTestEnable(vkCmdBuf, false);
	vkCmdSetDepthWriteEnable(vkCmdBuf, false);
	vkCmdSetDepthBoundsTestEnable(vkCmdBuf, false);

	vkCmdSetStencilTestEnable(vkCmdBuf, false);

	setBlendColor(0, 0, 0, 0);
	vkCmdSetRasterizerDiscardEnable(vkCmdBuf, false);
};

Indium::PrivateRenderCommandEncoder::~PrivateRenderCommandEncoder() {
	if (_framebuffer) {
		vkDestroyFramebuffer(_privateDevice->device(), _framebuffer, nullptr);
	}
	if (_renderPass) {
		vkDestroyRenderPass(_privateDevice->device(), _renderPass, nullptr);
	}
	vkDestroyDescriptorPool(_privateDevice->device(), _pool, 0);
};

void Indium::PrivateRenderCommandEncoder::setRenderPipelineState(std::shared_ptr<RenderPipelineState> renderPipelineState) {
	auto buf = _privateCommandBuffer.lock();
	_privatePSO = std::dynamic_pointer_cast<PrivateRenderPipelineState>(renderPipelineState);
	_privatePSO->recreatePipeline(_renderPass, false);
};

void Indium::PrivateRenderCommandEncoder::setFrontFacingWinding(Winding frontFaceWinding) {
	auto buf = _privateCommandBuffer.lock();
	vkCmdSetFrontFace(buf->commandBuffer(), windingToVkFrontFace(frontFaceWinding));
};

void Indium::PrivateRenderCommandEncoder::setCullMode(CullMode cullMode) {
	auto buf = _privateCommandBuffer.lock();
	vkCmdSetCullMode(buf->commandBuffer(), cullModeToVkCullMode(cullMode));
};

void Indium::PrivateRenderCommandEncoder::setDepthBias(float depthBias, float slopeScale, float clamp) {
	auto buf = _privateCommandBuffer.lock();
	auto vkCmdBuf = buf->commandBuffer();
	vkCmdSetDepthBiasEnable(vkCmdBuf, true);
	vkCmdSetDepthBias(vkCmdBuf, depthBias, clamp, slopeScale);
};

void Indium::PrivateRenderCommandEncoder::setDepthClipMode(DepthClipMode depthClipMode) {
	if (depthClipMode != DepthClipMode::Clip) {
		throw std::runtime_error("TODO: support setting depth clip mode");
	}
};

void Indium::PrivateRenderCommandEncoder::setViewport(const Viewport& viewport) {
	setViewports(&viewport, 1);
};

void Indium::PrivateRenderCommandEncoder::setViewports(const Viewport* viewports, size_t count) {
	auto buf = _privateCommandBuffer.lock();
	std::vector<VkViewport> tmp;
	for (size_t i = 0; i < count; ++i) {
		const auto& viewport = viewports[i];
		VkViewport vkViewport {};
		// note: Metal's coordinates have the viewport flipped compared to Vulkan,
		//       so we use a Vulkan 1.1 feature here and just flip the Y-axis of the viewport
		//       (with a corresponding change in the origin).
		vkViewport.x = viewport.originX;
		vkViewport.y = viewport.height - viewport.originY;
		vkViewport.width = viewport.width;
		vkViewport.height = -viewport.height;
		vkViewport.minDepth = viewport.znear;
		vkViewport.maxDepth = viewport.zfar;
		tmp.push_back(vkViewport);
	}
	vkCmdSetViewportWithCount(buf->commandBuffer(), tmp.size(), tmp.data());
};

void Indium::PrivateRenderCommandEncoder::setViewports(const std::vector<Viewport>& viewports) {
	setViewports(viewports.data(), viewports.size());
};

void Indium::PrivateRenderCommandEncoder::setScissorRect(const ScissorRect& scissorRect) {
	setScissorRects(&scissorRect, 1);
};

void Indium::PrivateRenderCommandEncoder::setScissorRects(const ScissorRect* scissorRects, size_t count) {
	auto buf = _privateCommandBuffer.lock();
	std::vector<VkRect2D> tmp;
	for (size_t i = 0; i < count; ++i) {
		const auto& scissorRect = scissorRects[i];
		VkRect2D vkRect {};
		vkRect.offset.x = scissorRect.x;
		vkRect.offset.y = scissorRect.y;
		vkRect.extent.width = scissorRect.width;
		vkRect.extent.height = scissorRect.height;
		tmp.push_back(vkRect);
	}
	vkCmdSetScissorWithCount(buf->commandBuffer(), tmp.size(), tmp.data());
};

void Indium::PrivateRenderCommandEncoder::setScissorRects(const std::vector<ScissorRect>& scissorRects) {
	setScissorRects(scissorRects.data(), scissorRects.size());
};

void Indium::PrivateRenderCommandEncoder::setBlendColor(float red, float green, float blue, float alpha) {
	auto buf = _privateCommandBuffer.lock();
	const float tmp[4] = { red, green, blue, alpha };
	vkCmdSetBlendConstants(buf->commandBuffer(), tmp);
};

void Indium::PrivateRenderCommandEncoder::updateBindings() {
	// TODO: better descriptor set resource management

	auto buf = _privateCommandBuffer.lock();

	std::array<VkDescriptorSet, 2> descriptorSets = createDescriptorSets(_privatePSO->descriptorSetLayouts().layouts, _pool, _privateDevice, { _functionResources[0], _functionResources[1] }, { _privatePSO->vertexFunctionInfo(), _privatePSO->fragmentFunctionInfo() }, _keepAliveBuffers);

	vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _privatePSO->pipelineLayout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);

	const auto& vertexInputBindings = _privatePSO->vertexInputBindings();
	if (vertexInputBindings.size() > 0) {
		std::vector<VkBuffer> buffers;
		std::vector<VkDeviceSize> offsets;

		buffers.resize(vertexInputBindings.size());
		offsets.resize(vertexInputBindings.size());

		for (size_t vulkanIndex = 0; vulkanIndex < vertexInputBindings.size(); ++vulkanIndex) {
			const auto& metalIndex = vertexInputBindings[vulkanIndex];

			if (metalIndex >= _functionResources[0].buffers.size()) {
				// technically, this requires the `nullDescriptor` feature, but we should never run into this case anyways.
				buffers[vulkanIndex] = VK_NULL_HANDLE;
				offsets[vulkanIndex] = 0;
			} else {
				auto [buffer, offset] = _functionResources[0].buffers[metalIndex];
				auto privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(buffer);
				buffers[vulkanIndex] = privateBuffer->buffer();
				offsets[vulkanIndex] = offset;
			}
		}

		vkCmdBindVertexBuffers(buf->commandBuffer(), 0, vertexInputBindings.size(), buffers.data(), offsets.data());
	}
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) {
	auto buf = _privateCommandBuffer.lock();

	// bind the pipeline with the right topology class for this primitive
	VkPipeline pipeline = VK_NULL_HANDLE;
	switch (primitiveType) {
		case PrimitiveType::Point:
			pipeline = _privatePSO->pipelines()[0];
			break;
		case PrimitiveType::Line:
		case PrimitiveType::LineStrip:
			pipeline = _privatePSO->pipelines()[1];
			break;
		case PrimitiveType::Triangle:
		case PrimitiveType::TriangleStrip:
			pipeline = _privatePSO->pipelines()[2];
			break;
		default:
			throw BadEnumValue();
	}
	vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	vkCmdSetPrimitiveTopology(buf->commandBuffer(), primitiveTypeToVkPrimitiveTopology(primitiveType));

	// TODO: avoid re-binding descriptors on every draw.
	updateBindings();

	vkCmdDraw(buf->commandBuffer(), vertexCount, instanceCount, vertexStart, baseInstance);

	// when we emit a draw call, we save the current function resources so that they stay alive until the command buffer is done.
	// TODO: do this more efficiently by essentially doing COW: after a draw call, we only save additional references to resources
	//       if someone tries to overwrite them.
	_savedFunctionResources.push_back(_functionResources[0]);
	_savedFunctionResources.push_back(_functionResources[1]);
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) {
	drawPrimitives(primitiveType, vertexStart, vertexCount, instanceCount, 0);
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) {
	drawPrimitives(primitiveType, vertexStart, vertexCount, 1);
};

void Indium::PrivateRenderCommandEncoder::setVertexBytes(const void* bytes, size_t length, size_t index) {
	auto cmdBuf = _privateCommandBuffer.lock();
	_functionResources[0].setBytes(cmdBuf->device(), bytes, length, index);
};

void Indium::PrivateRenderCommandEncoder::endEncoding() {
	auto buf = _privateCommandBuffer.lock();
	vkCmdEndRenderPass(buf->commandBuffer());
};

void Indium::PrivateRenderCommandEncoder::setVertexBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
	_functionResources[0].setBuffer(buffer, offset, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setVertexBuffer(buffers[i], offsets[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setVertexBufferOffset(size_t offset, size_t index) {
	_functionResources[0].setBufferOffset(offset, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexSamplerState(std::shared_ptr<SamplerState> state, size_t index) {
	_functionResources[0].setSamplerState(state, std::nullopt, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) {
	_functionResources[0].setSamplerState(state, std::make_pair(lodMinClamp, lodMaxClamp), index);
};

void Indium::PrivateRenderCommandEncoder::setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setVertexSamplerState(states[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setVertexSamplerState(states[i], lodMinClamps[i], lodMaxClamps[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setVertexTexture(std::shared_ptr<Texture> texture, size_t index) {
	_functionResources[0].setTexture(texture, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setVertexTexture(textures[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setFragmentBytes(const void* bytes, size_t length, size_t index) {
	auto cmdBuf = _privateCommandBuffer.lock();
	_functionResources[1].setBytes(cmdBuf->device(), bytes, length, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentBuffer(std::shared_ptr<Buffer> buffer, size_t offset, size_t index) {
	_functionResources[1].setBuffer(buffer, offset, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setFragmentBuffer(buffers[i], offsets[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setFragmentBufferOffset(size_t offset, size_t index) {
	_functionResources[1].setBufferOffset(offset, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerState(std::shared_ptr<SamplerState> state, size_t index) {
	_functionResources[1].setSamplerState(state, std::nullopt, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerState(std::shared_ptr<SamplerState> state, float lodMinClamp, float lodMaxClamp, size_t index) {
	_functionResources[1].setSamplerState(state, std::make_pair(lodMinClamp, lodMaxClamp), index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setFragmentSamplerState(states[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setFragmentSamplerState(states[i], lodMinClamps[i], lodMaxClamps[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setFragmentTexture(std::shared_ptr<Texture> texture, size_t index) {
	_functionResources[1].setTexture(texture, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setFragmentTexture(textures[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) {
	auto buf = _privateCommandBuffer.lock();

	// bind the pipeline with the right topology class for this primitive
	VkPipeline pipeline = VK_NULL_HANDLE;
	switch (primitiveType) {
		case PrimitiveType::Point:
			pipeline = _privatePSO->pipelines()[0];
			break;
		case PrimitiveType::Line:
		case PrimitiveType::LineStrip:
			pipeline = _privatePSO->pipelines()[1];
			break;
		case PrimitiveType::Triangle:
		case PrimitiveType::TriangleStrip:
			pipeline = _privatePSO->pipelines()[2];
			break;
		default:
			throw BadEnumValue();
	}
	vkCmdBindPipeline(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	vkCmdSetPrimitiveTopology(buf->commandBuffer(), primitiveTypeToVkPrimitiveTopology(primitiveType));

	// TODO: avoid re-binding descriptors on every draw.
	updateBindings();

	// we need to keep this buffer alive until we complete the render
	_keepAliveBuffers.push_back(indexBuffer);

	auto privateIndexBuffer = std::dynamic_pointer_cast<PrivateBuffer>(indexBuffer);

	vkCmdBindIndexBuffer(buf->commandBuffer(), privateIndexBuffer->buffer(), indexBufferOffset, indexTypeToVkIndexType(indexType));
	vkCmdDrawIndexed(buf->commandBuffer(), indexCount, instanceCount, 0, baseVertex, baseInstance);

	// see drawPrimitives() to know why we do this
	_savedFunctionResources.push_back(_functionResources[0]);
	_savedFunctionResources.push_back(_functionResources[1]);
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset, size_t instanceCount) {
	drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount, 0, 0);
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, std::shared_ptr<Buffer> indexBuffer, size_t indexBufferOffset) {
	drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, 1);
};

void Indium::PrivateRenderCommandEncoder::setDepthStencilState(std::shared_ptr<DepthStencilState> state) {
	auto buf = _privateCommandBuffer.lock();
	auto privateState = std::dynamic_pointer_cast<PrivateDepthStencilState>(state);
	auto& desc = privateState->descriptor();

	vkCmdSetDepthWriteEnable(buf->commandBuffer(), desc.depthWriteEnabled ? VK_TRUE : VK_FALSE);
	vkCmdSetDepthCompareOp(buf->commandBuffer(), compareFunctionToVkCompareOp(desc.depthCompareFunction));
	vkCmdSetDepthTestEnable(buf->commandBuffer(), VK_TRUE);

	vkCmdSetStencilTestEnable(buf->commandBuffer(), (desc.frontFaceStencil || desc.backFaceStencil) ? VK_TRUE : VK_FALSE);

	if (desc.frontFaceStencil || desc.backFaceStencil) {
		if (desc.frontFaceStencil) {
			vkCmdSetStencilCompareMask(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, desc.frontFaceStencil->readMask);
			vkCmdSetStencilWriteMask(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, desc.frontFaceStencil->writeMask);

			vkCmdSetStencilOp(
				buf->commandBuffer(),
				VK_STENCIL_FACE_FRONT_BIT,
				stencilOperationToVkStencilOp(desc.frontFaceStencil->stencilFailureOperation),
				stencilOperationToVkStencilOp(desc.frontFaceStencil->depthStencilPassOperation),
				stencilOperationToVkStencilOp(desc.frontFaceStencil->depthFailureOperation),
				compareFunctionToVkCompareOp(desc.frontFaceStencil->stencilCompareFunction)
			);
		} else {
			vkCmdSetStencilOp(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
		}
		if (desc.backFaceStencil) {
			vkCmdSetStencilCompareMask(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, desc.backFaceStencil->readMask);
			vkCmdSetStencilWriteMask(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, desc.backFaceStencil->writeMask);

			vkCmdSetStencilOp(
				buf->commandBuffer(),
				VK_STENCIL_FACE_BACK_BIT,
				stencilOperationToVkStencilOp(desc.backFaceStencil->stencilFailureOperation),
				stencilOperationToVkStencilOp(desc.backFaceStencil->depthStencilPassOperation),
				stencilOperationToVkStencilOp(desc.backFaceStencil->depthFailureOperation),
				compareFunctionToVkCompareOp(desc.backFaceStencil->stencilCompareFunction)
			);
		} else {
			vkCmdSetStencilOp(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
		}
	}
};

void Indium::PrivateRenderCommandEncoder::setTriangleFillMode(TriangleFillMode triangleFillMode) {
	if (triangleFillMode != TriangleFillMode::Fill) {
		// we might have to just create duplicate pipelines for each possible fill mode;
		// there's only 2 at the moment, but this has to multiplied by the number of pipelines required
		// for other combinations. for example, we currently have to create a pipeline for each topology class
		// and there's 3 of those, so we would need 6 pipelines total. yikes.
		throw std::runtime_error("TODO: support changing fill mode");
	}
};

void Indium::PrivateRenderCommandEncoder::setStencilReferenceValue(uint32_t value) {
	auto buf = _privateCommandBuffer.lock();
	vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_AND_BACK, value);
};

void Indium::PrivateRenderCommandEncoder::setStencilReferenceValue(uint32_t front, uint32_t back) {
	auto buf = _privateCommandBuffer.lock();
	vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, front);
	vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, back);
};

void Indium::PrivateRenderCommandEncoder::setVisibilityResultMode(VisibilityResultMode mode, size_t offset) {
	auto buf = _privateCommandBuffer.lock();

	// TODO: this can be implemented using Vulkan's occlusion queries
	throw std::runtime_error("TODO: support visibility results");
};

void Indium::PrivateRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) {
	useResources({ resource }, usage, stages);
};

void Indium::PrivateRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) {
	// TODO: image layout transitions. maybe.
	//       right now, we always keep images in the layout described by their imageLayout() method.
	//       we only briefly transition them away for an operation and then transition them back.
	//       however, these transitions are probably unnecessary in most cases, so we could optimize
	//       performance by getting rid of them.

	auto buf = _privateCommandBuffer.lock();

	std::vector<VkBufferMemoryBarrier> bufferBarriers;
	std::vector<VkImageMemoryBarrier> imageBarriers;

	// TODO: relax this mask, maybe; it depends on what Metal does here.
	VkAccessFlags source = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	VkAccessFlags dest = VK_ACCESS_NONE;

	if (!!(usage & (ResourceUsage::Read | ResourceUsage::Sample))) {
		dest |= VK_ACCESS_SHADER_READ_BIT;
	}

	if (!!(usage & ResourceUsage::Write)) {
		dest |= VK_ACCESS_SHADER_WRITE_BIT;
	}

	for (const auto& resource: resources) {
		if (auto buffer = std::dynamic_pointer_cast<PrivateBuffer>(resource)) {
			VkBufferMemoryBarrier barrier {};

			barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			barrier.srcAccessMask = source;
			barrier.dstAccessMask = dest;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = buffer->buffer();
			barrier.offset = 0;
			barrier.size = VK_WHOLE_SIZE;

			bufferBarriers.push_back(barrier);
		} else if (auto texture = std::dynamic_pointer_cast<PrivateTexture>(resource)) {
			VkImageMemoryBarrier barrier {};

			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcAccessMask = source;
			barrier.dstAccessMask = dest;
			barrier.oldLayout = texture->imageLayout();
			barrier.newLayout = barrier.oldLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = texture->image();
			barrier.subresourceRange.aspectMask = pixelFormatToVkImageAspectFlags(texture->pixelFormat());
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = texture->mipmapLevelCount();
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = texture->vulkanArrayLength();

			imageBarriers.push_back(barrier);
		} else {
			throw std::runtime_error("Unsupported resource");
		}
	}

	// TODO: relax this, maybe, depending on what Metal does.
	VkPipelineStageFlags srcStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkPipelineStageFlags dstStages = VK_PIPELINE_STAGE_NONE;

	if (!!(stages & RenderStages::Vertex)) {
		dstStages |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
	}

	if (!!(stages & RenderStages::Fragment)) {
		dstStages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	if (!!(stages & RenderStages::Tile)) {
		// XXX: not sure about this one
		dstStages |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
	}

	if (!!(stages & RenderStages::Object)) {
		// ???
	}

	if (!!(stages & RenderStages::Mesh)) {
		dstStages |= VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT;
	}

	vkCmdPipelineBarrier(buf->commandBuffer(), srcStages, dstStages, 0, 0, nullptr, bufferBarriers.size(), bufferBarriers.data(), imageBarriers.size(), imageBarriers.data());
};

void Indium::PrivateRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) {
	useResources({ resource }, usage);
};

void Indium::PrivateRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) {
	// TODO: check what Metal does in this case. this is just an educated guess
	useResources(resources, usage, RenderStages::Vertex | RenderStages::Fragment | RenderStages::Tile | RenderStages::Object | RenderStages::Mesh);
};
