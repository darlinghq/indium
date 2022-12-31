#include <indium/blit-command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/dynamic-vk.hpp>

Indium::BlitCommandEncoder::~BlitCommandEncoder() {};

Indium::PrivateBlitCommandEncoder::PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(commandBuffer->privateDevice())
	{};

Indium::PrivateBlitCommandEncoder::PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const BlitPassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(commandBuffer->privateDevice()),
	_descriptor(descriptor)
	{};

Indium::PrivateBlitCommandEncoder::~PrivateBlitCommandEncoder() {
	// nothing for now
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Buffer> source, size_t sourceOffset, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t size) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateBuffer>(destination);

	VkBufferCopy info {};

	info.srcOffset = sourceOffset;
	info.dstOffset = destinationOffset;
	info.size = size;

	// TODO: maybe we need a pipeline barrier here?

	DynamicVK::vkCmdCopyBuffer(cmdbuf->commandBuffer(), privateSource->buffer(), privateDest->buffer(), 1, &info);
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Buffer> source, size_t sourceOffset, size_t sourceBytesPerRow, size_t sourceBytesPerImage, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, BlitOption options) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateTexture>(destination);

	if (options != BlitOption::None) {
		throw std::runtime_error("TODO: support blit options");
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateDest->pixelFormat());
	// FIXME: handle compressed formats
	size_t bytesPerPixel = pixelFormatToByteCount(privateDest->pixelFormat());

	// first, transition the image to the optimal layout for transfer destinations
	VkImageMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	// TODO: relax this mask if possible
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = privateDest->imageLayout();
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = privateDest->image();
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = destinationLevel;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = destinationSlice;
	barrier.subresourceRange.layerCount = 1;

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	// now encode the copy
	VkBufferImageCopy copyInfo {};
	copyInfo.bufferOffset = 0;
	copyInfo.bufferRowLength = sourceBytesPerRow / bytesPerPixel;
	copyInfo.bufferImageHeight = sourceBytesPerImage / sourceBytesPerRow;
	copyInfo.imageSubresource.aspectMask = aspect;
	copyInfo.imageSubresource.mipLevel = destinationLevel;
	copyInfo.imageSubresource.baseArrayLayer = destinationSlice;
	copyInfo.imageSubresource.layerCount = 1;
	copyInfo.imageOffset.x = destinationOrigin.x;
	copyInfo.imageOffset.y = destinationOrigin.y;
	copyInfo.imageOffset.z = destinationOrigin.z;
	copyInfo.imageExtent.width = sourceSize.width;
	copyInfo.imageExtent.height = sourceSize.height;
	copyInfo.imageExtent.depth = sourceSize.depth;
	DynamicVK::vkCmdCopyBufferToImage(cmdbuf->commandBuffer(), privateSource->buffer(), privateDest->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyInfo);

	// finally, transition the image back to the original layout
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = privateDest->imageLayout();
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	// TODO: relax this mask if possible
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t destinationBytesPerRow, size_t destinationBytesPerImage, BlitOption options) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateTexture>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateBuffer>(destination);

	if (options != BlitOption::None) {
		throw std::runtime_error("TODO: support blit options");
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateSource->pixelFormat());
	// FIXME: handle compressed formats
	size_t bytesPerPixel = pixelFormatToByteCount(privateSource->pixelFormat());

	// first, transition the image to the optimal layout for transfer sources
	VkImageMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	// TODO: relax this mask if possible
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = privateSource->imageLayout();
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = privateSource->image();
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = sourceLevel;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = sourceSlice;
	barrier.subresourceRange.layerCount = 1;

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	// now encode the copy
	VkBufferImageCopy copyInfo {};
	copyInfo.bufferOffset = 0;
	copyInfo.bufferRowLength = destinationBytesPerRow / bytesPerPixel;
	copyInfo.bufferImageHeight = destinationBytesPerImage / destinationBytesPerRow;
	copyInfo.imageSubresource.aspectMask = aspect;
	copyInfo.imageSubresource.mipLevel = sourceLevel;
	copyInfo.imageSubresource.baseArrayLayer = sourceSlice;
	copyInfo.imageSubresource.layerCount = 1;
	copyInfo.imageOffset.x = sourceOrigin.x;
	copyInfo.imageOffset.y = sourceOrigin.y;
	copyInfo.imageOffset.z = sourceOrigin.z;
	copyInfo.imageExtent.width = sourceSize.width;
	copyInfo.imageExtent.height = sourceSize.height;
	copyInfo.imageExtent.depth = sourceSize.depth;
	DynamicVK::vkCmdCopyImageToBuffer(cmdbuf->commandBuffer(), privateSource->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, privateDest->buffer(), 1, &copyInfo);

	// finally, transition the image back to the original layout
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = privateSource->imageLayout();
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// TODO: relax this mask if possible
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, size_t sliceCount, size_t levelCount, Size size) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateTexture>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateTexture>(destination);

	auto aspect = pixelFormatToVkImageAspectFlags(privateSource->pixelFormat());

	// first, transition the images to the optimal layouts for transfers (one as the source, the other as the destination)

	VkImageMemoryBarrier barriers[2] = {};

	barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	// TODO: relax this mask if possible
	barriers[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barriers[0].oldLayout = privateSource->imageLayout();
	barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barriers[0].image = privateSource->image();
	barriers[0].subresourceRange.aspectMask = aspect;
	barriers[0].subresourceRange.baseMipLevel = sourceLevel;
	barriers[0].subresourceRange.levelCount = levelCount;
	barriers[0].subresourceRange.baseArrayLayer = sourceSlice;
	barriers[0].subresourceRange.layerCount = sliceCount;

	barriers[1] = barriers[0];
	barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barriers[1].oldLayout = privateDest->imageLayout();
	barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].image = privateDest->image();

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, sizeof(barriers) / sizeof(*barriers), barriers);

	// now encode the copy

	std::vector<VkImageCopy> copyInfos(levelCount);

	for (size_t i = 0; i < levelCount; ++i) {
		auto& copyInfo = copyInfos[i];
		copyInfo.srcSubresource.aspectMask = aspect;
		copyInfo.srcSubresource.mipLevel = sourceLevel + i;
		copyInfo.srcSubresource.baseArrayLayer = sourceSlice;
		copyInfo.srcSubresource.layerCount = sliceCount;
		copyInfo.srcOffset.x = sourceOrigin.x;
		copyInfo.srcOffset.y = sourceOrigin.y;
		copyInfo.srcOffset.z = sourceOrigin.z;
		copyInfo.dstSubresource.aspectMask = aspect;
		copyInfo.dstSubresource.mipLevel = destinationLevel + i;
		copyInfo.dstSubresource.baseArrayLayer = destinationSlice;
		copyInfo.dstSubresource.layerCount = sliceCount;
		copyInfo.dstOffset.x = destinationOrigin.x;
		copyInfo.dstOffset.y = destinationOrigin.y;
		copyInfo.dstOffset.z = destinationOrigin.z;
		copyInfo.extent.width = size.width / (1ull << i);
		copyInfo.extent.height = size.height / (1ull << i);
		copyInfo.extent.depth = size.depth / (1ull << i);
	}

	DynamicVK::vkCmdCopyImage(cmdbuf->commandBuffer(), privateSource->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, privateDest->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copyInfos.size(), copyInfos.data());

	// finally, transition the images back to their original layouts

	barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barriers[0].newLayout = privateSource->imageLayout();
	barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// TODO: relax this mask if possible
	barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

	barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barriers[1].newLayout = privateDest->imageLayout();
	barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	// TODO: relax this mask if possible
	barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, sizeof(barriers) / sizeof(*barriers), barriers);
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateTexture>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateTexture>(destination);
	size_t sourceMipLevel = SIZE_MAX;
	size_t destMipLevel = SIZE_MAX;

	// find the first identical mip levels
	for (size_t i = 0; i < source->mipmapLevelCount(); ++i) {
		size_t mipFactor = 1ull << i;
		size_t sourceWidth = source->width() / mipFactor;
		size_t sourceHeight = source->height() / mipFactor;
		size_t sourceDepth = source->depth() / mipFactor;

		if (sourceWidth == destination->width() && sourceHeight == destination->height() && sourceDepth == destination->depth()) {
			sourceMipLevel = i;
			destMipLevel = 0;
			break;
		}
	}

	if (sourceMipLevel == SIZE_MAX && destMipLevel == SIZE_MAX) {
		for (size_t i = 0; i < destination->mipmapLevelCount(); ++i) {
			size_t mipFactor = 1ull << i;
			size_t destWidth = destination->width() / mipFactor;
			size_t destHeight = destination->height() / mipFactor;
			size_t destDepth = destination->depth() / mipFactor;

			if (destWidth == source->width() && destHeight == source->height() && destDepth == source->depth()) {
				sourceMipLevel = 0;
				destMipLevel = i;
				break;
			}
		}
	}

	if (sourceMipLevel == SIZE_MAX && destMipLevel == SIZE_MAX) {
		// not sure what's supposed to happen in this case, so just throw an error for now.
		throw std::runtime_error("Invalid copy operation: source and destination textures have no mip levels with identical sizes");
	}

	size_t sourceMipCount = source->mipmapLevelCount() - sourceMipLevel;
	size_t destMipCount = destination->mipmapLevelCount() - destMipLevel;
	size_t mipCount = std::min(sourceMipCount, destMipCount);
	size_t sliceCount = std::min(privateSource->vulkanArrayLength(), privateDest->vulkanArrayLength());

	size_t divisor = static_cast<size_t>(1) << sourceMipLevel;

	copy(source, 0, sourceMipLevel, Origin { 0, 0, 0 }, destination, 0, destMipLevel, Origin { 0, 0, 0 }, sliceCount, mipCount, Size { source->width() / divisor, source->height() / divisor, source->depth() / divisor });
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, size_t sliceCount, size_t levelCount) {
	size_t divisor = static_cast<size_t>(1) << sourceLevel;
	copy(source, sourceSlice, sourceLevel, Origin { 0, 0, 0 }, destination, destinationSlice, destinationLevel, Origin { 0, 0, 0 }, sliceCount, levelCount, Size { source->width() / divisor, source->height() / divisor, source->depth() / divisor });
};

void Indium::PrivateBlitCommandEncoder::copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin) {
	copy(source, sourceSlice, sourceLevel, sourceOrigin, destination, destinationSlice, destinationLevel, destinationOrigin, 1, 1, sourceSize);
};

void Indium::PrivateBlitCommandEncoder::fillBuffer(std::shared_ptr<Buffer> buffer, Range<size_t> range, uint8_t value) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(buffer);
	uint32_t value32 =
		((uint32_t)value << 24) |
		((uint32_t)value << 16) |
		((uint32_t)value <<  8) |
		((uint32_t)value <<  0)
		;

	if ((range.start & 3) != 0) {
		throw std::runtime_error("Misaligned fillBuffer offset");
	}

	if ((range.length & 3) != 0) {
		throw std::runtime_error("Misaligned fillBuffer length");
	}

	// TODO: maybe we need a pipeline barrier here?

	DynamicVK::vkCmdFillBuffer(cmdbuf->commandBuffer(), privateBuffer->buffer(), range.start, range.length, value32);
};

void Indium::PrivateBlitCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
	auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
	auto cmdbuf = _privateCommandBuffer.lock();

	auto aspect = pixelFormatToVkImageAspectFlags(privateTexture->pixelFormat());

	auto texType = texture->textureType();
	bool is1D = texType == TextureType::e1D || texType == TextureType::e1DArray;
	bool is2D = texType == TextureType::e2D || texType == TextureType::e2DArray || texType == TextureType::e2DMultisample || texType == TextureType::e2DMultisampleArray || texType == TextureType::eCube || texType == TextureType::eCubeArray;
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
	barrier.subresourceRange.layerCount = privateTexture->vulkanArrayLength();

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

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

		DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		//
		// now blit it
		//

		VkImageBlit blit {};

		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, mipDepth };
		blit.srcSubresource.aspectMask = aspect;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = privateTexture->vulkanArrayLength();

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
		blit.dstSubresource.layerCount = privateTexture->vulkanArrayLength();

		// now blit the image
		// TODO: determine the appropriate filter somehow. Metal docs say that the filter is implementation-determined, so we're technically free to use whatever,
		//       but we want to match Metal's behavior exactly.
		DynamicVK::vkCmdBlitImage(cmdbuf->commandBuffer(), privateTexture->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, privateTexture->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		// now transition the source level into the image's original layout
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		// TODO: relax dstAccessMask
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = privateTexture->imageLayout();

		DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	// finally, transition the last level into the image's original layout
	barrier.subresourceRange.baseMipLevel = privateTexture->mipmapLevelCount() - 1;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// TODO: relax dstAccessMask
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = privateTexture->imageLayout();

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
};

void Indium::PrivateBlitCommandEncoder::endEncoding() {
	// nothing for now
};
