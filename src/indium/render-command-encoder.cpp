#include <indium/render-command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/render-pass.hpp>
#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/event.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/render-pipeline.private.hpp>
#include <indium/types.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/library.private.hpp>
#include <indium/sampler.private.hpp>
#include <indium/depth-stencil.private.hpp>
#include <indium/command-encoder.private.hpp>
#include <indium/visibility-results.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <vulkan/vulkan_core.h>

Indium::RenderCommandEncoder::~RenderCommandEncoder() {};

Indium::PrivateRenderCommandEncoder::PrivateRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_descriptor(descriptor),
	_privateDevice(commandBuffer->privateDevice()),
	_descriptorPools(_privateDevice->device())
{
	auto buf = _privateCommandBuffer.lock();

	auto vkDevice = _privateDevice->device();
	auto vkCmdBuf = buf->commandBuffer();
	_tracked = buf->hazardsTracked();

	// A pass may have no color attachments (a depth-only pass) and gaps between them; unused color
	// attachments stay in the subpass as VK_ATTACHMENT_UNUSED so the pipeline's indices still match.
	// Depth and stencil share one Vulkan attachment, so when both are given they must be the same image.
	auto depthStencilTexture = descriptor.depthAttachment ? descriptor.depthAttachment->texture :
		(descriptor.stencilAttachment ? descriptor.stencilAttachment->texture : nullptr);
	if (descriptor.depthAttachment && descriptor.stencilAttachment && descriptor.depthAttachment->texture != descriptor.stencilAttachment->texture) {
		auto depthImage = std::dynamic_pointer_cast<PrivateTexture>(descriptor.depthAttachment->texture);
		auto stencilImage = std::dynamic_pointer_cast<PrivateTexture>(descriptor.stencilAttachment->texture);
		if (!depthImage || !stencilImage || depthImage->image() != stencilImage->image()) {
			throw std::runtime_error("Separate depth and stencil textures are unsupported");
		}
	}

	std::shared_ptr<Texture> firstTexture;
	for (const auto& color: descriptor.colorAttachments) {
		if (color.texture) {
			firstTexture = color.texture;
			break;
		}
	}
	if (!firstTexture) {
		firstTexture = depthStencilTexture;
	}
	if (!firstTexture) {
		throw std::runtime_error("Render pass requires an attachment");
	}
	auto resolvesColor = [](const auto& color) {
		return color.texture && (color.storeAction == StoreAction::MultisampleResolve || color.storeAction == StoreAction::StoreAndMultisampleResolve);
	};
	for (const auto& color : descriptor.colorAttachments) {
		if (!color.texture) {
			continue;
		}
		if (color.texture->sampleCount() != firstTexture->sampleCount()) {
			throw std::runtime_error("Render pass color sample counts must match");
		}
		if (resolvesColor(color)) {
			if (!color.resolveTexture || color.texture->sampleCount() == 1 || color.resolveTexture->sampleCount() != 1 ||
				color.resolveTexture->pixelFormat() != color.texture->pixelFormat() ||
				color.resolveTexture->width() != color.texture->width() || color.resolveTexture->height() != color.texture->height()) {
				throw std::runtime_error("Color resolve requires matching multisample source and single-sample destination");
			}
			if (color.level || color.slice || color.depthPlane || color.resolveLevel || color.resolveSlice || color.resolveDepthPlane) {
				throw std::runtime_error("Color resolve subresources are not supported");
			}
		}
	}
	if (depthStencilTexture && depthStencilTexture->sampleCount() != firstTexture->sampleCount()) {
		throw std::runtime_error("Render pass depth and color sample counts must match");
	}
	std::vector<VkClearValue> clearValues;

	for (const auto& color: descriptor.colorAttachments) {
		if (!color.texture) {
			continue;
		}
		auto clearColor = color.clearColor;
		VkClearValue clearValue {};
		clearValue.color.float32[0] = clearColor.red;
		clearValue.color.float32[1] = clearColor.green;
		clearValue.color.float32[2] = clearColor.blue;
		clearValue.color.float32[3] = clearColor.alpha;
		clearValues.push_back(clearValue);

		// TODO: distinguish between read-only and read-write textures
		_readWriteTextures.push_back(color.texture);
		if (resolvesColor(color)) _readWriteTextures.push_back(color.resolveTexture);
	}

	if (depthStencilTexture) {
		VkClearValue clearValue {};
		clearValue.depthStencil.depth = descriptor.depthAttachment ? descriptor.depthAttachment->clearDepth : 1.0;
		clearValue.depthStencil.stencil = descriptor.stencilAttachment ? descriptor.stencilAttachment->clearStencil : 0;
		clearValues.push_back(clearValue);
		_readWriteTextures.push_back(depthStencilTexture);
	}

	std::vector<VkAttachmentDescription> renderPassAttachments;
	std::vector<VkSubpassDescription> subpasses;
	std::vector<VkSubpassDependency> dependencies;

	// Only an explicit (or default) Clear may discard an attachment's contents. Metal leaves the
	// contents undefined after DontCare, so preserving them is conforming, and it is what code that
	// resumes a pass with Load after a DontCare pass in between sees on macOS. Vulkan's DONT_CARE with
	// an UNDEFINED initial layout lets the driver throw the contents away instead. For the same
	// reason every attachment is stored.
	for (const auto& color: descriptor.colorAttachments) {
		if (!color.texture) {
			continue;
		}
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(color.texture);
		bool clear = loadActionToVkAttachmentLoadOp(color.loadAction, true) == VK_ATTACHMENT_LOAD_OP_CLEAR;
		VkAttachmentDescription desc {};
		desc.format = _privateDevice->textureVkFormat(color.texture->pixelFormat());
		desc.samples = sampleCountToVk(color.texture->sampleCount());
		desc.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		desc.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : privateTexture->imageLayout();
		desc.finalLayout = privateTexture->imageLayout();
		renderPassAttachments.push_back(desc);
	}

	if (depthStencilTexture) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(depthStencilTexture);
		auto format = privateTexture->pixelFormat();
		bool hasDepth = format != PixelFormat::Stencil8 && format != PixelFormat::X32_Stencil8 && format != PixelFormat::X24_Stencil8;
		bool hasStencil = format == PixelFormat::Stencil8 || format == PixelFormat::X32_Stencil8 || format == PixelFormat::X24_Stencil8 ||
			format == PixelFormat::Depth24Unorm_Stencil8 || format == PixelFormat::Depth32Float_Stencil8;
		// an aspect the pass does not attach is preserved
		auto clears = [](const auto& attachment) {
			return attachment && loadActionToVkAttachmentLoadOp(attachment->loadAction, false) == VK_ATTACHMENT_LOAD_OP_CLEAR;
		};
		bool clearDepth = clears(descriptor.depthAttachment);
		bool clearStencil = clears(descriptor.stencilAttachment);

		VkAttachmentDescription desc {};
		desc.format = pixelFormatToVkFormat(format);
		desc.samples = sampleCountToVk(privateTexture->sampleCount());
		desc.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		desc.stencilLoadOp = clearStencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		desc.initialLayout = ((clearDepth || !hasDepth) && (clearStencil || !hasStencil)) ? VK_IMAGE_LAYOUT_UNDEFINED : privateTexture->imageLayout();
		desc.finalLayout = privateTexture->imageLayout();
		renderPassAttachments.push_back(desc);
	}

	std::vector<VkAttachmentReference> colorAttachments;
	VkAttachmentReference depthStencilAttachment {};

	size_t index = 0;
	for (const auto& color: descriptor.colorAttachments) {
		VkAttachmentReference ref {};
		ref.attachment = color.texture ? index++ : VK_ATTACHMENT_UNUSED;
		ref.layout = VK_IMAGE_LAYOUT_GENERAL;
		colorAttachments.push_back(ref);
	}

	if (depthStencilTexture) {
		depthStencilAttachment.attachment = index;
		depthStencilAttachment.layout = VK_IMAGE_LAYOUT_GENERAL;
		++index;
	}

	std::vector<VkAttachmentReference> resolveAttachments;
	for (const auto& color : descriptor.colorAttachments) {
		VkAttachmentReference ref { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_GENERAL };
		if (resolvesColor(color)) {
			auto texture = std::dynamic_pointer_cast<PrivateTexture>(color.resolveTexture);
			ref.attachment = renderPassAttachments.size();
			VkAttachmentDescription desc {};
			desc.format = _privateDevice->textureVkFormat(texture->pixelFormat());
			desc.samples = VK_SAMPLE_COUNT_1_BIT;
			desc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			desc.finalLayout = texture->imageLayout();
			renderPassAttachments.push_back(desc);
		}
		resolveAttachments.push_back(ref);
	}

	// memoryBarrier() records pipeline barriers inside the pass, which Vulkan only allows when they
	// match a self-dependency of the subpass: one between fragment work (framebuffer-local) and one
	// from vertex work to anything after it
	{
		auto& fragment = dependencies.emplace_back();
		fragment.srcSubpass = 0;
		fragment.dstSubpass = 0;
		fragment.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		fragment.dstStageMask = fragment.srcStageMask;
		fragment.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		fragment.dstAccessMask = fragment.srcAccessMask;
		fragment.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		auto& vertex = dependencies.emplace_back();
		vertex.srcSubpass = 0;
		vertex.dstSubpass = 0;
		vertex.srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
		vertex.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		vertex.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		vertex.dstAccessMask = vertex.srcAccessMask;
	}

	auto& subpassDesc = subpasses.emplace_back();
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.colorAttachmentCount = colorAttachments.size();
	subpassDesc.pColorAttachments = colorAttachments.data();
	subpassDesc.pResolveAttachments = resolveAttachments.data();
	subpassDesc.pDepthStencilAttachment = depthStencilTexture ? &depthStencilAttachment : nullptr;

	VkRenderPassCreateInfo renderPassInfo {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = renderPassAttachments.size();
	renderPassInfo.pAttachments = renderPassAttachments.data();
	renderPassInfo.subpassCount = subpasses.size();
	renderPassInfo.pSubpasses = subpasses.data();
	renderPassInfo.dependencyCount = dependencies.size();
	renderPassInfo.pDependencies = dependencies.data();

	_renderPass = _privateDevice->acquireRenderPass(renderPassInfo, _renderPassDeviceOwned);

	std::vector<VkImageView> framebufferAttachments;

	for (const auto& colorAttachment: descriptor.colorAttachments) {
		if (colorAttachment.texture) {
			framebufferAttachments.push_back(std::dynamic_pointer_cast<PrivateTexture>(colorAttachment.texture)->imageView());
		}
	}

	if (depthStencilTexture) {
		framebufferAttachments.push_back(std::dynamic_pointer_cast<PrivateTexture>(depthStencilTexture)->imageView());
	}

	for (const auto& color : descriptor.colorAttachments) {
		if (resolvesColor(color)) {
			framebufferAttachments.push_back(std::dynamic_pointer_cast<PrivateTexture>(color.resolveTexture)->imageView());
		}
	}

	VkFramebufferCreateInfo framebufferInfo {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = _renderPass;
	framebufferInfo.attachmentCount = framebufferAttachments.size();
	framebufferInfo.pAttachments = framebufferAttachments.data();
	framebufferInfo.width = firstTexture->width();
	framebufferInfo.height = firstTexture->height();
	framebufferInfo.layers = std::dynamic_pointer_cast<PrivateTexture>(firstTexture)->vulkanArrayLength();
	if (DynamicVK::vkCreateFramebuffer(vkDevice, &framebufferInfo, nullptr, &_framebuffer) != VK_SUCCESS) {
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
	DynamicVK::vkCmdBeginRenderPass(vkCmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	if (auto visibilityBuffer = std::dynamic_pointer_cast<PrivateBuffer>(descriptor.visibilityResultBuffer); visibilityBuffer && VisibilityResults::supported(_privateDevice)) {
		_visibilityResults = std::make_unique<VisibilityResults>(_privateDevice, visibilityBuffer);
	}

	_defaultWidth = firstTexture->width();
	_defaultHeight = firstTexture->height();
	applyDefaultState();
};

void Indium::PrivateRenderCommandEncoder::applyDefaultState() {
	auto buf = _privateCommandBuffer.lock();
	auto vkCmdBuf = buf->commandBuffer();

	setViewport(Viewport { 0, 0, static_cast<double>(_defaultWidth), static_cast<double>(_defaultHeight), 0, 1 });
	setScissorRect(ScissorRect { _defaultHeight, _defaultWidth, 0, 0 });
	setCullMode(CullMode::None);
	setFrontFacingWinding(Winding::Clockwise);

	DynamicVK::vkCmdSetDepthCompareOp(vkCmdBuf, VK_COMPARE_OP_ALWAYS);
	DynamicVK::vkCmdSetDepthBiasEnable(vkCmdBuf, false);
	DynamicVK::vkCmdSetDepthTestEnable(vkCmdBuf, false);
	DynamicVK::vkCmdSetDepthWriteEnable(vkCmdBuf, false);
	DynamicVK::vkCmdSetDepthBoundsTestEnable(vkCmdBuf, false);

	DynamicVK::vkCmdSetStencilTestEnable(vkCmdBuf, false);

	setBlendColor(0, 0, 0, 0);
	DynamicVK::vkCmdSetRasterizerDiscardEnable(vkCmdBuf, false);
};

void Indium::PrivateRenderCommandEncoder::resetState() {
	_privatePSO.reset();
	_functionResources = {};
	_triangleFillMode = TriangleFillMode::Fill;
	_depthClipMode = DepthClipMode::Clip;
	applyDefaultState();
	setStencilReferenceValue(0);
	setVisibilityResultMode(VisibilityResultMode::Disabled, 0);
};

Indium::PrivateRenderCommandEncoder::~PrivateRenderCommandEncoder() {
	if (_framebuffer) {
		DynamicVK::vkDestroyFramebuffer(_privateDevice->device(), _framebuffer, nullptr);
	}
	if (_renderPass && !_renderPassDeviceOwned) {
		DynamicVK::vkDestroyRenderPass(_privateDevice->device(), _renderPass, nullptr);
	}
};

void Indium::PrivateRenderCommandEncoder::setRenderPipelineState(const std::shared_ptr<RenderPipelineState>& renderPipelineState) {
	auto buf = _privateCommandBuffer.lock();
	_privatePSO = std::dynamic_pointer_cast<PrivateRenderPipelineState>(renderPipelineState);
	if (_privatePSO) {
		_privatePSO->recreatePipeline(_renderPass, false);
	}
};

void Indium::PrivateRenderCommandEncoder::setFrontFacingWinding(Winding frontFaceWinding) {
	auto buf = _privateCommandBuffer.lock();
	DynamicVK::vkCmdSetFrontFace(buf->commandBuffer(), windingToVkFrontFace(frontFaceWinding));
};

void Indium::PrivateRenderCommandEncoder::setCullMode(CullMode cullMode) {
	auto buf = _privateCommandBuffer.lock();
	DynamicVK::vkCmdSetCullMode(buf->commandBuffer(), cullModeToVkCullMode(cullMode));
};

void Indium::PrivateRenderCommandEncoder::setDepthBias(float depthBias, float slopeScale, float clamp) {
	auto buf = _privateCommandBuffer.lock();
	auto vkCmdBuf = buf->commandBuffer();
	DynamicVK::vkCmdSetDepthBiasEnable(vkCmdBuf, true);
	DynamicVK::vkCmdSetDepthBias(vkCmdBuf, depthBias, clamp, slopeScale);
};

void Indium::PrivateRenderCommandEncoder::setDepthClipMode(DepthClipMode depthClipMode) {
	// takes effect with the pipeline bound by the next draw
	_depthClipMode = depthClipMode;
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
		// the documentation for setViewport and setViewports states that znear and zfar must be between
		// 0 and 1 (inclusive). however, one of Apple's examples ("Creating and Sampling Textures")
		// uses a znear of -1. i haven't tested what the actual behavior is in this case, but i'm assuming
		// that the values are simply clamped to [0, 1]. the sample in question seems to work identically
		// with clamping or without clamping (which is only possible with VK_EXT_depth_range_unrestricted enabled).
		vkViewport.minDepth = std::clamp(viewport.znear, 0., 1.);
		vkViewport.maxDepth = std::clamp(viewport.zfar, 0., 1.);
		tmp.push_back(vkViewport);
	}
	DynamicVK::vkCmdSetViewportWithCount(buf->commandBuffer(), tmp.size(), tmp.data());
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
	DynamicVK::vkCmdSetScissorWithCount(buf->commandBuffer(), tmp.size(), tmp.data());
};

void Indium::PrivateRenderCommandEncoder::setScissorRects(const std::vector<ScissorRect>& scissorRects) {
	setScissorRects(scissorRects.data(), scissorRects.size());
};

void Indium::PrivateRenderCommandEncoder::setBlendColor(float red, float green, float blue, float alpha) {
	auto buf = _privateCommandBuffer.lock();
	const float tmp[4] = { red, green, blue, alpha };
	DynamicVK::vkCmdSetBlendConstants(buf->commandBuffer(), tmp);
};

bool Indium::PrivateRenderCommandEncoder::updateBindings() {
	// TODO: better descriptor set resource management

	auto buf = _privateCommandBuffer.lock();

	std::array<VkDescriptorSet, 2> descriptorSets = createDescriptorSets(_privatePSO->descriptorSetLayouts().layouts, _descriptorPools, _privateDevice, { _functionResources[0], _functionResources[1] }, { _privatePSO->vertexFunctionInfo(), _privatePSO->fragmentFunctionInfo() }, _keepAliveBuffers, _scratch);

	DynamicVK::vkCmdBindDescriptorSets(buf->commandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, _privatePSO->pipelineLayout(), 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);

	const auto& vertexInputBindings = _privatePSO->vertexInputBindings();
	const auto& fallbackSafe = _privatePSO->vertexInputFallbackSafe();
	if (vertexInputBindings.size() > 0) {
		std::vector<VkBuffer> buffers;
		std::vector<VkDeviceSize> offsets;

		buffers.resize(vertexInputBindings.size());
		offsets.resize(vertexInputBindings.size());

		for (size_t vulkanIndex = 0; vulkanIndex < vertexInputBindings.size(); ++vulkanIndex) {
			const auto& metalIndex = vertexInputBindings[vulkanIndex];

			std::shared_ptr<PrivateBuffer> privateBuffer;
			size_t offset = 0;
			if (metalIndex < _functionResources[0].buffers.size()) {
				privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(_functionResources[0].buffers[metalIndex].first);
				offset = _functionResources[0].buffers[metalIndex].second;
			}

			if (privateBuffer) {
				buffers[vulkanIndex] = privateBuffer->buffer();
				offsets[vulkanIndex] = offset + privateBuffer->bufferOffset();
			} else if (_privateDevice->nullDescriptorEnabled()) {
				// The pipeline fetches vertices from a buffer index that was never bound. A null buffer
				// reads as zero at any index, so nullDescriptor makes this safe for every layout.
				buffers[vulkanIndex] = VK_NULL_HANDLE;
				offsets[vulkanIndex] = 0;
			} else if (fallbackSafe[vulkanIndex]) {
				// Without nullDescriptor the zero-filled fallback buffer stands in, which is
				// only correct when every fetch reads the same bytes inside it.
				buffers[vulkanIndex] = std::dynamic_pointer_cast<PrivateBuffer>(_privateDevice->fallbackBuffer())->buffer();
				offsets[vulkanIndex] = 0;
			} else {
				// A fetch that advances through a buffer nobody bound would read past the end of the
				// zero buffer, which the GPU may fault on. Nothing valid can be drawn: skip the draw.
				return false;
			}
		}

		DynamicVK::vkCmdBindVertexBuffers(buf->commandBuffer(), 0, vertexInputBindings.size(), buffers.data(), offsets.data());
	}

	noteBindings();
	return true;
};

void Indium::PrivateRenderCommandEncoder::noteBindings() {
	if (!_tracked) {
		return;
	}

	forEachBoundResource(_functionResources[0], _privatePSO->vertexFunctionInfo(), [&](const HazardRange& range, VkAccessFlags2 access) {
		_accesses.add(range, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, access);
	});
	forEachBoundResource(_functionResources[1], _privatePSO->fragmentFunctionInfo(), [&](const HazardRange& range, VkAccessFlags2 access) {
		_accesses.add(range, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, access);
	});

	for (auto metalIndex: _privatePSO->vertexInputBindings()) {
		if (metalIndex < _functionResources[0].buffers.size()) {
			noteBuffer(_functionResources[0].buffers[metalIndex].first, _functionResources[0].buffers[metalIndex].second, SIZE_MAX,
				VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
		}
	}
};

void Indium::PrivateRenderCommandEncoder::noteBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t length, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
	auto privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(buffer);
	if (_tracked && privateBuffer && privateBuffer->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeTracked) {
		_accesses.add(privateBuffer->hazardRange(offset, length), stages, access);
	}
};

Indium::HazardTracker::Barriers Indium::PrivateRenderCommandEncoder::orderPass() {
	auto buf = _privateCommandBuffer.lock();
	auto& tracker = buf->hazards();
	HazardTracker::Barriers barriers;

	if (_waitsForFence) {
		// the pass cannot take a barrier once it has begun, so everything it waits for is ordered before all of it
		VkMemoryBarrier2 barrier {};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		barriers.push_back(barrier);
		tracker.noteFullBarrier();
	}

	// the attachments: loaded, blended, tested and stored by the pass
	const auto attachment = [&](const std::shared_ptr<Texture>& texture, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
		auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
		if (privateTexture && privateTexture->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeTracked) {
			_accesses.add(privateTexture->hazardRange(), stages, access);
		}
	};
	constexpr VkPipelineStageFlags2 colorStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	constexpr VkAccessFlags2 colorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	for (const auto& color: _descriptor.colorAttachments) {
		attachment(color.texture, colorStage, colorAccess);
		attachment(color.resolveTexture, colorStage, colorAccess);
	}
	constexpr VkPipelineStageFlags2 depthStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	constexpr VkAccessFlags2 depthAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	if (_descriptor.depthAttachment) {
		attachment(_descriptor.depthAttachment->texture, depthStage, depthAccess);
	}
	if (_descriptor.stencilAttachment) {
		attachment(_descriptor.stencilAttachment->texture, depthStage, depthAccess);
	}

	_accesses.applyTo(tracker);
	auto taken = tracker.take();
	barriers.insert(barriers.end(), taken.begin(), taken.end());
	return barriers;
};

void Indium::PrivateRenderCommandEncoder::bindPipeline(VkCommandBuffer commandBuffer, PrimitiveType primitiveType) {
	// bind the pipeline with the right topology class for this primitive; the fill mode only applies to
	// triangles, the depth clip mode to every primitive
	size_t topologyClass;
	switch (primitiveType) {
		case PrimitiveType::Point:
			topologyClass = 0;
			break;
		case PrimitiveType::Line:
		case PrimitiveType::LineStrip:
			topologyClass = 1;
			break;
		case PrimitiveType::Triangle:
		case PrimitiveType::TriangleStrip:
			topologyClass = 2;
			break;
		default:
			throw BadEnumValue();
	}
	VkPipeline pipeline = _privatePSO->pipelineFor(topologyClass, _triangleFillMode == TriangleFillMode::Lines, _depthClipMode == DepthClipMode::Clamp);
	DynamicVK::vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	DynamicVK::vkCmdSetPrimitiveTopology(commandBuffer, primitiveTypeToVkPrimitiveTopology(primitiveType));
};

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) {
	if (!_privatePSO) {
		return;
	}
	auto buf = _privateCommandBuffer.lock();

	bindPipeline(buf->commandBuffer(), primitiveType);

	// TODO: avoid re-binding descriptors on every draw.
	bool drawable = updateBindings();

	if (drawable) {
		DynamicVK::vkCmdDraw(buf->commandBuffer(), vertexCount, instanceCount, vertexStart, baseInstance);
	}

	// when we emit a draw call, we save the current function resources so that they stay alive until the command buffer is done.
	// TODO: do this more efficiently by essentially doing COW: after a draw call, we only save additional references to resources
	//       if someone tries to overwrite them.
	_savedFunctionResources.push_back(_functionResources[0]);
	_savedFunctionResources.push_back(_functionResources[1]);
};

// Metal's MTLDrawPrimitivesIndirectArguments and MTLDrawIndexedPrimitivesIndirectArguments have the field order
// and widths of VkDrawIndirectCommand and VkDrawIndexedIndirectCommand, so the buffer is handed to Vulkan as is.
static_assert(sizeof(VkDrawIndirectCommand) == 4 * sizeof(uint32_t), "indirect draw arguments are four 32-bit values");
static_assert(sizeof(VkDrawIndexedIndirectCommand) == 5 * sizeof(uint32_t), "indexed indirect draw arguments are five 32-bit values");

void Indium::PrivateRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) {
	if (!_privatePSO || !indirectArgumentsFit(indirectBuffer, indirectBufferOffset, sizeof(VkDrawIndirectCommand))) {
		return;
	}
	auto buf = _privateCommandBuffer.lock();

	bindPipeline(buf->commandBuffer(), primitiveType);

	bool drawable = updateBindings();

	// the buffer has to stay alive until the command buffer has completed
	_keepAliveBuffers.push_back(indirectBuffer);

	if (drawable) {
		noteBuffer(indirectBuffer, indirectBufferOffset, sizeof(VkDrawIndirectCommand), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
		DynamicVK::vkCmdDrawIndirect(buf->commandBuffer(), std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->buffer(), indirectBufferOffset + std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->bufferOffset(), 1, sizeof(VkDrawIndirectCommand));
	}

	// see drawPrimitives() above for why we do this
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
	_functionResources[0].setBytes(cmdBuf->device(), _scratch, _keepAliveBuffers, bytes, length, index);
};

void Indium::PrivateRenderCommandEncoder::endEncoding() {
	auto buf = _privateCommandBuffer.lock();
	if (_visibilityResults) {
		_visibilityResults->end(buf->commandBuffer());
	}
	DynamicVK::vkCmdEndRenderPass(buf->commandBuffer());
	// What the pass reads has to be ordered after what came before, and a pass cannot take barriers once begun, so
	// they are known only now and recorded in front of it.
	HazardTracker::Barriers barriers;
	if (_tracked) {
		barriers = orderPass();
	} else {
		buf->endUntrackedEncoder();
	}
	if (_visibilityResults) {
		_visibilityResults->resolve(buf);
	}

	auto used = _functionResources[0].usedTextures;
	used.insert(used.end(), _functionResources[1].usedTextures.begin(), _functionResources[1].usedTextures.end());
	used.insert(used.end(), _readOnlyTextures.begin(), _readOnlyTextures.end());
	buf->endEncoderSegment(used, _readWriteTextures, barriers);
};

void Indium::PrivateRenderCommandEncoder::setVertexBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) {
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

void Indium::PrivateRenderCommandEncoder::setVertexSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) {
	_functionResources[0].setSamplerState(state, std::nullopt, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) {
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

void Indium::PrivateRenderCommandEncoder::setVertexTexture(const std::shared_ptr<Texture>& texture, size_t index) {
	_functionResources[0].setTexture(texture, index);
};

void Indium::PrivateRenderCommandEncoder::setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setVertexTexture(textures[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::setFragmentBytes(const void* bytes, size_t length, size_t index) {
	auto cmdBuf = _privateCommandBuffer.lock();
	_functionResources[1].setBytes(cmdBuf->device(), _scratch, _keepAliveBuffers, bytes, length, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) {
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

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) {
	_functionResources[1].setSamplerState(state, std::nullopt, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) {
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

void Indium::PrivateRenderCommandEncoder::setFragmentTexture(const std::shared_ptr<Texture>& texture, size_t index) {
	_functionResources[1].setTexture(texture, index);
};

void Indium::PrivateRenderCommandEncoder::setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	for (size_t i = 0; i < range.length; ++i) {
		setFragmentTexture(textures[i], range.start + i);
	}
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) {
	if (!_privatePSO) {
		return;
	}
	auto buf = _privateCommandBuffer.lock();

	bindPipeline(buf->commandBuffer(), primitiveType);

	// TODO: avoid re-binding descriptors on every draw.
	bool drawable = updateBindings();

	// we need to keep this buffer alive until we complete the render
	_keepAliveBuffers.push_back(indexBuffer);

	auto privateIndexBuffer = std::dynamic_pointer_cast<PrivateBuffer>(indexBuffer);

	if (drawable) {
		noteBuffer(indexBuffer, indexBufferOffset, SIZE_MAX, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
		DynamicVK::vkCmdBindIndexBuffer(buf->commandBuffer(), privateIndexBuffer->buffer(), indexBufferOffset + privateIndexBuffer->bufferOffset(), indexTypeToVkIndexType(indexType));
		DynamicVK::vkCmdDrawIndexed(buf->commandBuffer(), indexCount, instanceCount, 0, baseVertex, baseInstance);
	}

	// see drawPrimitives() to know why we do this
	_savedFunctionResources.push_back(_functionResources[0]);
	_savedFunctionResources.push_back(_functionResources[1]);
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount) {
	drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount, 0, 0);
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset) {
	drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, 1);
};

void Indium::PrivateRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) {
	if (!_privatePSO || !indexBuffer || !indirectArgumentsFit(indirectBuffer, indirectBufferOffset, sizeof(VkDrawIndexedIndirectCommand))) {
		return;
	}
	auto buf = _privateCommandBuffer.lock();

	bindPipeline(buf->commandBuffer(), primitiveType);

	bool drawable = updateBindings();

	_keepAliveBuffers.push_back(indexBuffer);
	_keepAliveBuffers.push_back(indirectBuffer);

	if (drawable) {
		noteBuffer(indexBuffer, indexBufferOffset, SIZE_MAX, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
		noteBuffer(indirectBuffer, indirectBufferOffset, sizeof(VkDrawIndexedIndirectCommand), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
		DynamicVK::vkCmdBindIndexBuffer(buf->commandBuffer(), std::dynamic_pointer_cast<PrivateBuffer>(indexBuffer)->buffer(), indexBufferOffset + std::dynamic_pointer_cast<PrivateBuffer>(indexBuffer)->bufferOffset(), indexTypeToVkIndexType(indexType));
		DynamicVK::vkCmdDrawIndexedIndirect(buf->commandBuffer(), std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->buffer(), indirectBufferOffset + std::dynamic_pointer_cast<PrivateBuffer>(indirectBuffer)->bufferOffset(), 1, sizeof(VkDrawIndexedIndirectCommand));
	}

	// see drawPrimitives() above for why we do this
	_savedFunctionResources.push_back(_functionResources[0]);
	_savedFunctionResources.push_back(_functionResources[1]);
};

void Indium::PrivateRenderCommandEncoder::setDepthStencilState(const std::shared_ptr<DepthStencilState>& state) {
	auto buf = _privateCommandBuffer.lock();
	auto privateState = std::dynamic_pointer_cast<PrivateDepthStencilState>(state);
	auto& desc = privateState->descriptor();

	DynamicVK::vkCmdSetDepthWriteEnable(buf->commandBuffer(), desc.depthWriteEnabled ? VK_TRUE : VK_FALSE);
	DynamicVK::vkCmdSetDepthCompareOp(buf->commandBuffer(), compareFunctionToVkCompareOp(desc.depthCompareFunction));
	DynamicVK::vkCmdSetDepthTestEnable(buf->commandBuffer(), VK_TRUE);

	DynamicVK::vkCmdSetStencilTestEnable(buf->commandBuffer(), (desc.frontFaceStencil || desc.backFaceStencil) ? VK_TRUE : VK_FALSE);

	if (desc.frontFaceStencil || desc.backFaceStencil) {
		if (desc.frontFaceStencil) {
			DynamicVK::vkCmdSetStencilCompareMask(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, desc.frontFaceStencil->readMask);
			DynamicVK::vkCmdSetStencilWriteMask(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, desc.frontFaceStencil->writeMask);

			DynamicVK::vkCmdSetStencilOp(
				buf->commandBuffer(),
				VK_STENCIL_FACE_FRONT_BIT,
				stencilOperationToVkStencilOp(desc.frontFaceStencil->stencilFailureOperation),
				stencilOperationToVkStencilOp(desc.frontFaceStencil->depthStencilPassOperation),
				stencilOperationToVkStencilOp(desc.frontFaceStencil->depthFailureOperation),
				compareFunctionToVkCompareOp(desc.frontFaceStencil->stencilCompareFunction)
			);
		} else {
			DynamicVK::vkCmdSetStencilOp(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
		}
		if (desc.backFaceStencil) {
			DynamicVK::vkCmdSetStencilCompareMask(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, desc.backFaceStencil->readMask);
			DynamicVK::vkCmdSetStencilWriteMask(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, desc.backFaceStencil->writeMask);

			DynamicVK::vkCmdSetStencilOp(
				buf->commandBuffer(),
				VK_STENCIL_FACE_BACK_BIT,
				stencilOperationToVkStencilOp(desc.backFaceStencil->stencilFailureOperation),
				stencilOperationToVkStencilOp(desc.backFaceStencil->depthStencilPassOperation),
				stencilOperationToVkStencilOp(desc.backFaceStencil->depthFailureOperation),
				compareFunctionToVkCompareOp(desc.backFaceStencil->stencilCompareFunction)
			);
		} else {
			DynamicVK::vkCmdSetStencilOp(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
		}
	}
};

void Indium::PrivateRenderCommandEncoder::setTriangleFillMode(TriangleFillMode triangleFillMode) {
	// takes effect with the pipeline bound by the next draw
	_triangleFillMode = triangleFillMode;
};

void Indium::PrivateRenderCommandEncoder::setStencilReferenceValue(uint32_t value) {
	auto buf = _privateCommandBuffer.lock();
	DynamicVK::vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_AND_BACK, value);
};

void Indium::PrivateRenderCommandEncoder::setStencilReferenceValue(uint32_t front, uint32_t back) {
	auto buf = _privateCommandBuffer.lock();
	DynamicVK::vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_FRONT_BIT, front);
	DynamicVK::vkCmdSetStencilReference(buf->commandBuffer(), VK_STENCIL_FACE_BACK_BIT, back);
};

void Indium::PrivateRenderCommandEncoder::setVisibilityResultMode(VisibilityResultMode mode, size_t offset) {
	// without a visibility result buffer there is nowhere to write the results to
	if (_visibilityResults) {
		_visibilityResults->setMode(_privateCommandBuffer.lock()->commandBuffer(), mode, offset);
	}
};

void Indium::PrivateRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) {
	useResources({ resource }, usage, stages);
};

static VkPipelineStageFlags2 renderStagesToVk(Indium::RenderStages stages) {
	VkPipelineStageFlags2 result = 0;
	if (!!(stages & Indium::RenderStages::Vertex)) {
		result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	}
	if (!!(stages & Indium::RenderStages::Fragment)) {
		result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	}
	if (!!(stages & (Indium::RenderStages::Tile | Indium::RenderStages::Object | Indium::RenderStages::Mesh))) {
		// stages indium has no shaders for: they are whatever graphics work there is
		result |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	}
	return result;
}

void Indium::PrivateRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) {
	// The resources are reached through memory the draws' bindings do not show, and the pass cannot take a
	// barrier once it has begun: they are ordered in front of the pass, as the draws' own resources are.
	if (!_tracked) {
		return;
	}
	auto vkStages = renderStagesToVk(stages);
	for (const auto& resource: resources) {
		addDeclaredResource(_accesses, resource, usage, vkStages);
	}
};

void Indium::PrivateRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) {
	useResources({ resource }, usage);
};

void Indium::PrivateRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) {
	// TODO: check what Metal does in this case. this is just an educated guess
	useResources(resources, usage, RenderStages::Vertex | RenderStages::Fragment | RenderStages::Tile | RenderStages::Object | RenderStages::Mesh);
};

void Indium::PrivateRenderCommandEncoder::updateFence(const std::shared_ptr<Fence>& fence, RenderStages after) {
	// see checkFence(): nothing to record, and the stages cannot make it cheaper
	checkFence(fence, _privateDevice);
};

void Indium::PrivateRenderCommandEncoder::waitForFence(const std::shared_ptr<Fence>& fence, RenderStages before) {
	checkFence(fence, _privateDevice);
	// the barrier cannot be recorded inside the pass: see orderPass()
	_waitsForFence = true;
};

void Indium::PrivateRenderCommandEncoder::memoryBarrier(BarrierScope scope, RenderStages after, RenderStages before) {
	constexpr size_t knownScope = static_cast<size_t>(BarrierScope::Buffers | BarrierScope::Textures | BarrierScope::RenderTargets);
	constexpr size_t supportedStages = static_cast<size_t>(RenderStages::Vertex | RenderStages::Fragment);
	if (!scope || (static_cast<size_t>(scope) & ~knownScope)) {
		throw std::invalid_argument("Invalid memory barrier scope");
	}
	if (!after || !before || (static_cast<size_t>(after) & ~supportedStages) || (static_cast<size_t>(before) & ~supportedStages)) {
		throw std::invalid_argument("Unsupported memory barrier stages");
	}

	VkMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	VkPipelineStageFlags srcStages = 0;
	VkPipelineStageFlags dstStages = 0;
	VkDependencyFlags flags = 0;

	if (!!(scope & (BarrierScope::Buffers | BarrierScope::Textures))) {
		barrier.srcAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	}

	if (after == RenderStages::Vertex) {
		srcStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
		dstStages = (!!(before & RenderStages::Vertex) ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : 0) |
			(!!(before & RenderStages::Fragment) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0);
		// vertex work does not write render targets
		if (!barrier.srcAccessMask) {
			return;
		}
	} else if (before == RenderStages::Fragment) {
		// the fragment stage of earlier draws (with the vertex stage, which precedes it) before
		// the fragment stage of later ones
		srcStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (!!(scope & BarrierScope::RenderTargets)) {
			srcStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dstStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			barrier.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		}
		flags = VK_DEPENDENCY_BY_REGION_BIT;
	} else {
		throw std::invalid_argument("A render pass cannot order later vertex work after earlier fragment work");
	}

	auto buf = _privateCommandBuffer.lock();
	DynamicVK::vkCmdPipelineBarrier(buf->commandBuffer(), srcStages, dstStages, flags, 1, &barrier, 0, nullptr, 0, nullptr);
};

void Indium::PrivateRenderCommandEncoder::useHeap(const std::shared_ptr<Heap>& heap) {
	useHeap(heap, RenderStages::Vertex | RenderStages::Fragment | RenderStages::Tile | RenderStages::Object | RenderStages::Mesh);
};

void Indium::PrivateRenderCommandEncoder::useHeap(const std::shared_ptr<Heap>& heap, RenderStages stages) {
	if (_tracked) {
		addDeclaredHeap(_accesses, heap, renderStagesToVk(stages));
	}
};

void Indium::PrivateRenderCommandEncoder::useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) {
	for (const auto& heap: heaps) {
		useHeap(heap);
	}
};

void Indium::PrivateRenderCommandEncoder::useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps, RenderStages stages) {
	for (const auto& heap: heaps) {
		useHeap(heap, stages);
	}
};
