#include <indium/blit-command-encoder.private.hpp>
#include <indium/counter.private.hpp>
#include <indium/event.private.hpp>
#include <indium/command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <algorithm>

namespace {
	// copies, fills and blits are transfer operations
	constexpr VkPipelineStageFlags2 transferStage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;

	bool isTracked(const std::shared_ptr<Indium::Resource>& resource) {
		return resource && resource->hazardTrackingMode() == Indium::HazardTrackingMode::HazardTrackingModeTracked;
	}

	/** The next command reads or writes `length` bytes of the buffer from `offset`. */
	void useBuffer(Indium::PrivateCommandBuffer& commandBuffer, const std::shared_ptr<Indium::PrivateBuffer>& buffer, size_t offset, size_t length, bool write) {
		if (commandBuffer.hazardsTracked() && isTracked(buffer)) {
			commandBuffer.hazards().use(buffer->hazardRange(offset, length), transferStage, write ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT);
		}
	}

	/** The next command reads or writes the texture (all of it: its subresources are not told apart). */
	void useTexture(Indium::PrivateCommandBuffer& commandBuffer, const std::shared_ptr<Indium::PrivateTexture>& texture, bool write) {
		if (commandBuffer.hazardsTracked() && isTracked(texture)) {
			commandBuffer.hazards().use(texture->hazardRange(), transferStage, write ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT);
		}
	}

	/** Records what the uses before it need. */
	void flushUses(Indium::PrivateCommandBuffer& commandBuffer) {
		if (commandBuffer.hazardsTracked()) {
			commandBuffer.hazards().flush(commandBuffer.commandBuffer());
		}
	}

	/**
	 * The transition back to the texture's own layout ends with a barrier to every later command (see the callers), which
	 * orders everything that touched the texture before them: nothing is outstanding for it.
	 */
	void releaseTexture(Indium::PrivateCommandBuffer& commandBuffer, const std::shared_ptr<Indium::PrivateTexture>& texture) {
		if (commandBuffer.hazardsTracked() && isTracked(texture)) {
			commandBuffer.hazards().release(texture->hazardRange());
		}
	}
}

Indium::BlitCommandEncoder::~BlitCommandEncoder() {};

Indium::PrivateBlitCommandEncoder::PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(commandBuffer->privateDevice())
	{};

Indium::PrivateBlitCommandEncoder::PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const BlitPassDescriptor& descriptor):
	_privateCommandBuffer(commandBuffer),
	_privateDevice(commandBuffer->privateDevice()),
	_descriptor(descriptor)
{
	for (const auto& [index, attachment]: _descriptor.sampleBufferAttachments) {
		PrivateCounterSampleBuffer::record(attachment.sampleBuffer, commandBuffer->commandBuffer(), attachment.startOfEncoderSampleIndex, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	}
};

Indium::PrivateBlitCommandEncoder::~PrivateBlitCommandEncoder() {
	// nothing for now
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Buffer>& source, size_t sourceOffset, const std::shared_ptr<Buffer>& destination, size_t destinationOffset, size_t size) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source);
	cmdbuf->retainResource(privateSource);
	auto privateDest = std::dynamic_pointer_cast<PrivateBuffer>(destination);
	cmdbuf->retainResource(privateDest);

	VkBufferCopy info {};

	info.srcOffset = sourceOffset + privateSource->bufferOffset();
	info.dstOffset = destinationOffset + privateDest->bufferOffset();
	info.size = size;

	useBuffer(*cmdbuf, privateSource, sourceOffset, size, false);
	useBuffer(*cmdbuf, privateDest, destinationOffset, size, true);
	flushUses(*cmdbuf);

	DynamicVK::vkCmdCopyBuffer(cmdbuf->commandBuffer(), privateSource->buffer(), privateDest->buffer(), 1, &info);
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Buffer>& source, size_t sourceOffset, size_t sourceBytesPerRow, size_t sourceBytesPerImage, Size sourceSize, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, BlitOption options) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateBuffer>(source);
	cmdbuf->retainResource(privateSource);
	auto privateDest = std::dynamic_pointer_cast<PrivateTexture>(destination);

	if (std::find(_readWriteTextures.begin(), _readWriteTextures.end(), destination) == _readWriteTextures.end()) {
		_readWriteTextures.push_back(destination);
	}

	if (options != BlitOption::None) {
		throw std::runtime_error("TODO: support blit options");
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateDest->pixelFormat());

	useBuffer(*cmdbuf, privateSource, sourceOffset, SIZE_MAX, false);
	useTexture(*cmdbuf, privateDest, true);
	flushUses(*cmdbuf);

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
	auto pitch = bufferTexelPitch(privateDest->pixelFormat(), sourceBytesPerRow, sourceBytesPerImage);
	copyInfo.bufferOffset = sourceOffset + privateSource->bufferOffset();
	copyInfo.bufferRowLength = pitch.rowLength;
	copyInfo.bufferImageHeight = pitch.imageHeight;
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
	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	releaseTexture(*cmdbuf, privateDest);
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, const std::shared_ptr<Buffer>& destination, size_t destinationOffset, size_t destinationBytesPerRow, size_t destinationBytesPerImage, BlitOption options) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateTexture>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateBuffer>(destination);
	cmdbuf->retainResource(privateDest);

	if (std::find(_readOnlyTextures.begin(), _readOnlyTextures.end(), source) == _readOnlyTextures.end()) {
		_readOnlyTextures.push_back(source);
	}

	if (options != BlitOption::None) {
		throw std::runtime_error("TODO: support blit options");
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateSource->pixelFormat());

	useTexture(*cmdbuf, privateSource, false);
	useBuffer(*cmdbuf, privateDest, destinationOffset, SIZE_MAX, true);
	flushUses(*cmdbuf);

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
	auto pitch = bufferTexelPitch(privateSource->pixelFormat(), destinationBytesPerRow, destinationBytesPerImage);
	copyInfo.bufferOffset = destinationOffset + privateDest->bufferOffset();
	copyInfo.bufferRowLength = pitch.rowLength;
	copyInfo.bufferImageHeight = pitch.imageHeight;
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
	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	releaseTexture(*cmdbuf, privateSource);
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, size_t sliceCount, size_t levelCount, Size size) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateSource = std::dynamic_pointer_cast<PrivateTexture>(source);
	auto privateDest = std::dynamic_pointer_cast<PrivateTexture>(destination);

	if (std::find(_readOnlyTextures.begin(), _readOnlyTextures.end(), source) == _readOnlyTextures.end()) {
		_readOnlyTextures.push_back(source);
	}
	if (std::find(_readWriteTextures.begin(), _readWriteTextures.end(), destination) == _readWriteTextures.end()) {
		_readWriteTextures.push_back(destination);
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateSource->pixelFormat());

	useTexture(*cmdbuf, privateSource, false);
	useTexture(*cmdbuf, privateDest, true);
	flushUses(*cmdbuf);

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

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, sizeof(barriers) / sizeof(*barriers), barriers);
	releaseTexture(*cmdbuf, privateSource);
	releaseTexture(*cmdbuf, privateDest);
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Texture>& source, const std::shared_ptr<Texture>& destination) {
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

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, size_t sliceCount, size_t levelCount) {
	size_t divisor = static_cast<size_t>(1) << sourceLevel;
	copy(source, sourceSlice, sourceLevel, Origin { 0, 0, 0 }, destination, destinationSlice, destinationLevel, Origin { 0, 0, 0 }, sliceCount, levelCount, Size { source->width() / divisor, source->height() / divisor, source->depth() / divisor });
};

void Indium::PrivateBlitCommandEncoder::copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin) {
	copy(source, sourceSlice, sourceLevel, sourceOrigin, destination, destinationSlice, destinationLevel, destinationOrigin, 1, 1, sourceSize);
};

void Indium::PrivateBlitCommandEncoder::fillBuffer(const std::shared_ptr<Buffer>& buffer, Range<size_t> range, uint8_t value) {
	auto cmdbuf = _privateCommandBuffer.lock();
	auto privateBuffer = std::dynamic_pointer_cast<PrivateBuffer>(buffer);
	cmdbuf->retainResource(privateBuffer);
	uint32_t value32 =
		((uint32_t)value << 24) |
		((uint32_t)value << 16) |
		((uint32_t)value <<  8) |
		((uint32_t)value <<  0)
		;

	useBuffer(*cmdbuf, privateBuffer, range.start, range.length, true);
	flushUses(*cmdbuf);

	// Vulkan fills whole words at word offsets of the VkBuffer: fill the words the range covers, and copy the bytes
	// before and after them (at most 3 each) from a host-filled buffer (the host's writes are visible to the submission).
	const VkDeviceSize start = range.start + privateBuffer->bufferOffset();
	const VkDeviceSize end = start + range.length;
	const VkDeviceSize wordsStart = std::min((start + 3) & ~VkDeviceSize(3), end);
	const VkDeviceSize wordsEnd = std::max(end & ~VkDeviceSize(3), wordsStart);

	if (wordsEnd > wordsStart) {
		DynamicVK::vkCmdFillBuffer(cmdbuf->commandBuffer(), privateBuffer->buffer(), wordsStart, wordsEnd - wordsStart, value32);
	}

	VkBufferCopy edges[2] {};
	uint32_t edgeCount = 0;
	if (wordsStart > start) {
		edges[edgeCount].dstOffset = start;
		edges[edgeCount++].size = wordsStart - start;
	}
	if (end > wordsEnd) {
		edges[edgeCount].dstOffset = wordsEnd;
		edges[edgeCount++].size = end - wordsEnd;
	}
	if (edgeCount > 0) {
		auto scratch = std::dynamic_pointer_cast<PrivateBuffer>(_privateDevice->newBuffer(3, ResourceOptions::StorageModeShared));
		memset(scratch->contents(), value, 3);
		cmdbuf->retainResource(scratch);
		DynamicVK::vkCmdCopyBuffer(cmdbuf->commandBuffer(), scratch->buffer(), privateBuffer->buffer(), edgeCount, edges);
	}
};

void Indium::PrivateBlitCommandEncoder::generateMipmapsForTexture(const std::shared_ptr<Texture>& texture) {
	auto privateTexture = std::dynamic_pointer_cast<PrivateTexture>(texture);
	auto cmdbuf = _privateCommandBuffer.lock();

	if (std::find(_readWriteTextures.begin(), _readWriteTextures.end(), texture) == _readWriteTextures.end()) {
		_readWriteTextures.push_back(texture);
	}

	auto aspect = pixelFormatToVkImageAspectFlags(privateTexture->pixelFormat());

	auto texType = texture->textureType();
	bool is1D = texType == TextureType::e1D || texType == TextureType::e1DArray;
	bool is2D = texType == TextureType::e2D || texType == TextureType::e2DArray || texType == TextureType::e2DMultisample || texType == TextureType::e2DMultisampleArray || texType == TextureType::eCube || texType == TextureType::eCubeArray;
	bool is3D = texType == TextureType::e3D;

	useTexture(*cmdbuf, privateTexture, true);
	flushUses(*cmdbuf);

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

		DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	// finally, transition the last level into the image's original layout
	barrier.subresourceRange.baseMipLevel = privateTexture->mipmapLevelCount() - 1;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	// TODO: relax dstAccessMask
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = privateTexture->imageLayout();

	DynamicVK::vkCmdPipelineBarrier(cmdbuf->commandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	releaseTexture(*cmdbuf, privateTexture);
};

void Indium::PrivateBlitCommandEncoder::updateFence(const std::shared_ptr<Fence>& fence) {
	// see checkFence(): nothing to record
	checkFence(fence, _privateDevice);
};

void Indium::PrivateBlitCommandEncoder::waitForFence(const std::shared_ptr<Fence>& fence) {
	checkFence(fence, _privateDevice);
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		// everything the fence's encoders did, tracked resources or not, is ordered before what follows
		commandBuffer->hazards().fullBarrier(commandBuffer->commandBuffer());
	}
};

void Indium::PrivateBlitCommandEncoder::endEncoding() {
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		for (const auto& [index, attachment]: _descriptor.sampleBufferAttachments) {
			PrivateCounterSampleBuffer::record(attachment.sampleBuffer, commandBuffer->commandBuffer(), attachment.endOfEncoderSampleIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		}
		// a tracked encoder leaves nothing to record: what follows waits for what it wrote when it uses it
		if (!commandBuffer->hazardsTracked()) {
			commandBuffer->endUntrackedEncoder();
		}
		commandBuffer->endEncoderSegment(_readOnlyTextures, _readWriteTextures);
	}
};

void Indium::PrivateBlitCommandEncoder::sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) {
	if (auto commandBuffer = _privateCommandBuffer.lock()) {
		// the sample is taken once the commands before it have finished, whether or not a barrier was asked for
		PrivateCounterSampleBuffer::record(sampleBuffer, commandBuffer->commandBuffer(), sampleIndex, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	}
};
