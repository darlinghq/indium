#include <indium/blit-command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/texture.private.hpp>

Indium::BlitCommandEncoder::~BlitCommandEncoder() {};

Indium::PrivateBlitCommandEncoder::PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(commandBuffer->privateDevice())
	{};

Indium::PrivateBlitCommandEncoder::~PrivateBlitCommandEncoder() {
	// nothing for now
};

void Indium::PrivateBlitCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
	auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
	auto cmdbuf = _privateCommandBuffer.lock();

	auto aspect = pixelFormatToVkImageAspectFlags(privateTexture->pixelFormat());

	auto texType = texture->textureType();
	bool is1D = texType == TextureType::e1D || texType == TextureType::e1DArray;
	bool is2D = texType == TextureType::e2D || texType == TextureType::e2DArray || texType == TextureType::e2DMultisample || texType == TextureType::e2DMultisampleArray;
	bool is3D = texType == TextureType::e3D;

	//
	// setup the common barrier info for the transitions and perform the initial transition
	//
	// we transition the entire image into transfer_dst_optimal layout and then transition individual mip levels as necessary
	//

	VkImageMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	// TODO: relax srcAccessMask
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = privateTexture->imageLayout();
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = privateTexture->image();
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = privateTexture->mipmapLevelCount();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = privateTexture->arrayLength();

	vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	// now change the barrier info for the transitions we'll perform in the loop
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = privateTexture->width();
	int32_t mipHeight = privateTexture->height();
	int32_t mipDepth = privateTexture->depth();
	for (size_t i = 1; i < texture->mipmapLevelCount(); ++i) {
		//
		// first, transition the source layer into transfer_src_optimal
		//

		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		//
		// now blit it
		//

		VkImageBlit blit {};

		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, mipDepth };
		blit.srcSubresource.aspectMask = aspect;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = privateTexture->arrayLength();

		if (mipWidth > 1) {
			mipWidth /= 2;
		} else {
			mipWidth = 1;
		}

		if (is2D || is3D) {
			if (mipHeight > 1) {
				mipHeight /= 2;
			} else {
				mipHeight = 1;
			}
		}

		// XXX: not sure if this is how it's supposed to work for 3D textures, but i'm following the pattern
		if (is3D) {
			if (mipDepth > 1) {
				mipDepth /= 2;
			} else {
				mipDepth = 1;
			}
		}

		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth, mipHeight, mipDepth };
		blit.dstSubresource.aspectMask = aspect;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = privateTexture->arrayLength();

		// now blit the image
		// TODO: determine the appropriate filter somehow. Metal docs say that the filter is implementation-determined, so we're technically free to use whatever,
		//       but we want to match Metal's behavior exactly.
		vkCmdBlitImage(cmdbuf->commandBuffer(), privateTexture->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, privateTexture->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		// now transition the source level into the image's original layout
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		// TODO: relax dstAccessMask
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = privateTexture->imageLayout();

		vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	// finally, transition the last level into the image's original layout
	barrier.subresourceRange.baseMipLevel = privateTexture->mipmapLevelCount() - 1;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// TODO: relax dstAccessMask
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = privateTexture->imageLayout();

	vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
};

void Indium::PrivateBlitCommandEncoder::endEncoding() {
	// nothing for now
};
