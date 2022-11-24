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

#include <forward_list>

static const std::vector<VkDescriptorPoolSize> poolSizes {
	VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
};

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
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.loadOp = loadActionToVkAttachmentLoadOp(color.loadAction, true);
		desc.storeOp = storeActionToVkAttachmentStoreOp(color.storeAction, true);
		desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout = (color.loadAction == LoadAction::Load) ? privateTexture->imageLayout() : VK_IMAGE_LAYOUT_UNDEFINED;
		desc.finalLayout = privateTexture->imageLayout();
		renderPassAttachments.push_back(desc);
	}

	std::vector<VkAttachmentReference> colorAttachments;

	size_t index = 0;
	for (const auto& color: descriptor.colorAttachments) {
		VkAttachmentReference ref {};
		ref.attachment = index;
		ref.layout = VK_IMAGE_LAYOUT_GENERAL;
		colorAttachments.push_back(ref);
		++index;
	}

	auto& subpassDesc = subpasses.emplace_back();
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.colorAttachmentCount = colorAttachments.size();
	subpassDesc.pColorAttachments = colorAttachments.data();

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

	VkFramebufferCreateInfo framebufferInfo {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = _renderPass;
	framebufferInfo.attachmentCount = framebufferAttachments.size();
	framebufferInfo.pAttachments = framebufferAttachments.data();
	framebufferInfo.width = firstTexture->width();
	framebufferInfo.height = firstTexture->height();
	framebufferInfo.layers = firstTexture->arrayLength();
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
	// TODO
	abort();
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

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) {
	auto buf = _privateCommandBuffer.lock();

	// bind the pipeline with the right topology class for this primitive
	VkPipeline pipeline = nullptr;
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

	// TODO: better descriptor set resource management

	// TODO: avoid re-binding descriptors on every draw.

	std::array<VkDescriptorSet, 2> descriptorSets {};

	auto setLayouts = _privatePSO->descriptorSetLayouts();
	VkDescriptorSetAllocateInfo setAllocateInfo {};
	setAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAllocateInfo.descriptorPool = _pool;
	setAllocateInfo.descriptorSetCount = descriptorSets.size();
	setAllocateInfo.pSetLayouts = setLayouts.data();

	if (vkAllocateDescriptorSets(_privateDevice->device(), &setAllocateInfo, descriptorSets.data()) != VK_SUCCESS) {
		// TODO
		abort();
	}

	for (size_t i = 0; i < descriptorSets.size(); ++i) {
		std::vector<VkWriteDescriptorSet> writeDescSet;
		std::forward_list<VkDescriptorBufferInfo> bufInfos;

		uint32_t bufferCount = 0;

		for (size_t j = 0; j < _functionResources[i].size(); ++j) {
			const auto& entry = _functionResources[i][j];

			if (const auto buf = std::get_if<std::shared_ptr<Buffer>>(&entry)) {
				++bufferCount;
			}
		}

		if (bufferCount > 0) {
			auto& funcInfo = (i == 0) ? _privatePSO->vertexFunctionInfo() : _privatePSO->fragmentFunctionInfo();

			std::vector<uint64_t> addresses;

			// find the right buffer for each binding (using the binding index)
			for (size_t j = 0; j < funcInfo.bindings.size(); ++j) {
				auto& bindingInfo = funcInfo.bindings[j];

				if (bindingInfo.type != BindingType::Buffer) {
					continue;
				}

				if (bindingInfo.index >= _functionResources[i].size()) {
					addresses.push_back(0);
					continue;
				}

				const auto& entry = _functionResources[i][bindingInfo.index];

				if (const auto buf = std::get_if<std::shared_ptr<Buffer>>(&entry)) {
					auto privateBuf = std::dynamic_pointer_cast<PrivateBuffer>(*buf);
					addresses.push_back(privateBuf->gpuAddress());
				} else {
					addresses.push_back(0);
					continue;
				}
			}

			auto addressBuffer = _privateDevice->newBuffer(addresses.data(), bufferCount * 8, ResourceOptions::StorageModeShared);
			auto privateAddrBuf = std::dynamic_pointer_cast<PrivateBuffer>(addressBuffer);

			// we need to keep this buffer alive until the operation is completed
			_addressBuffers.push_back(addressBuffer);

			auto& info = bufInfos.emplace_front();
			info.buffer = privateAddrBuf->buffer();
			info.offset = 0;
			info.range = VK_WHOLE_SIZE;

			auto& descSet = writeDescSet.emplace_back();
			descSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descSet.dstSet = descriptorSets[i];
			descSet.dstBinding = 0;
			descSet.dstArrayElement = 0;
			descSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descSet.descriptorCount = 1;
			descSet.pBufferInfo = &info;
		}

		vkUpdateDescriptorSets(_privateDevice->device(), writeDescSet.size(), writeDescSet.data(), 0, nullptr);
	}

	vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _privatePSO->pipelineLayout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);

	vkCmdDraw(buf->commandBuffer(), vertexCount, instanceCount, vertexStart, baseInstance);
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) {
	drawPrimitives(primitiveType, vertexStart, vertexCount, instanceCount, 0);
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) {
	drawPrimitives(primitiveType, vertexStart, vertexCount, 1);
};

void Indium::PrivateRenderCommandEncoder::setVertexBytes(const void* bytes, size_t length, size_t index) {
	auto cmdBuf = _privateCommandBuffer.lock();

	// TODO: we can make this "Private" instead
	auto buf = cmdBuf->device()->newBuffer(bytes, length, ResourceOptions::StorageModeShared);

	if (_functionResources[0].size() <= index) {
		_functionResources[0].resize(index + 1);
	}

	_functionResources[0][index] = buf;
};

void Indium::PrivateRenderCommandEncoder::endEncoding() {
	auto buf = _privateCommandBuffer.lock();
	vkCmdEndRenderPass(buf->commandBuffer());
};
