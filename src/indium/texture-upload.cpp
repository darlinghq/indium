// Copyright (C) 2026 Lubos Dolezel

#include <indium/texture-upload.private.hpp>
#include <indium/device.private.hpp>
#include <indium/buffer.private.hpp>
#include <indium/texture.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <cstdlib>

namespace {
	// what one staging buffer holds; a larger upload gets a buffer of its own, a multiple of this
	constexpr size_t chunkSize = 1 * 1024 * 1024;
	// buffers up to this size are kept for reuse once the GPU is done with them
	constexpr size_t maxReusableChunk = 16 * 1024 * 1024;
	// the alignment of every staged range: a multiple of every texel block size and of 4, as buffer-image copies need
	constexpr size_t stagingAlignment = 16;
	// spare staging buffers kept for reuse
	constexpr size_t maxFreeChunks = 4;
	// the staged bytes or copies waiting for a submission at which they are flushed without waiting for one
	constexpr size_t maxPendingBytes = 64 * 1024 * 1024;
	constexpr size_t maxPendingCopies = 8192;
	// copies of one image recorded as one command; the overlap check of a run is quadratic
	constexpr size_t maxRunLength = 256;

	bool copiesOverlap(const VkBufferImageCopy& a, const VkBufferImageCopy& b) {
		if (a.imageSubresource.mipLevel != b.imageSubresource.mipLevel || a.imageSubresource.baseArrayLayer != b.imageSubresource.baseArrayLayer) {
			return false;
		}
		auto disjoint = [](int32_t aStart, uint32_t aLength, int32_t bStart, uint32_t bLength) {
			return aStart + static_cast<int64_t>(aLength) <= bStart || bStart + static_cast<int64_t>(bLength) <= aStart;
		};
		return !(disjoint(a.imageOffset.x, a.imageExtent.width, b.imageOffset.x, b.imageExtent.width)
			|| disjoint(a.imageOffset.y, a.imageExtent.height, b.imageOffset.y, b.imageExtent.height)
			|| disjoint(a.imageOffset.z, a.imageExtent.depth, b.imageOffset.z, b.imageExtent.depth));
	}
};

Indium::TextureUploader::TextureUploader(PrivateDevice& device):
	_device(device)
{};

Indium::TextureUploader::~TextureUploader() {
	// releaseAll() has run (the device calls it before it destroys the Vulkan device)
};

std::shared_ptr<Indium::TextureUploader::Chunk> Indium::TextureUploader::newChunk(size_t size) {
	auto chunk = std::make_shared<Chunk>();
	chunk->buffer = std::dynamic_pointer_cast<PrivateBuffer>(_device.newBuffer(size, ResourceOptions::StorageModeShared));
	chunk->capacity = size;
	chunk->mapped = static_cast<uint8_t*>(chunk->buffer->contents());
	chunk->reusable = size <= maxReusableChunk;
	return chunk;
};

void Indium::TextureUploader::stage(const std::shared_ptr<PrivateTexture>& texture, size_t size, const VkBufferImageCopy& copy, const std::function<void(uint8_t*)>& fill) {
	size_t aligned = (size + stagingAlignment - 1) & ~(stagingAlignment - 1);
	if (aligned == 0) {
		aligned = stagingAlignment;
	}

	bool flushNow = false;
	std::vector<std::shared_ptr<void>> graveyard;
	{
		std::scoped_lock lock(_mutex);

		if (!_current || _current->capacity - _current->used < aligned) {
			// the chunk that is full stays referenced by the copies in it
			_current.reset();
			// the smallest spare buffer that fits
			auto best = _freeChunks.end();
			for (auto it = _freeChunks.begin(); it != _freeChunks.end(); ++it) {
				if ((*it)->capacity >= aligned && (best == _freeChunks.end() || (*it)->capacity < (*best)->capacity)) {
					best = it;
				}
			}
			if (best != _freeChunks.end()) {
				_current = std::move(*best);
				_freeChunks.erase(best);
			} else {
				_current = newChunk((aligned + chunkSize - 1) / chunkSize * chunkSize);
			}
		}

		Pending pending;
		pending.texture = texture;
		pending.chunk = _current;
		pending.copy = copy;
		pending.copy.bufferOffset = _current->used;
		// the bytes have to be in place before a flush can see the copy
		fill(_current->mapped + _current->used);
		_current->used += aligned;

		_pending.push_back(std::move(pending));
		_pendingBytes += aligned;
		_pendingCount.store(_pending.size(), std::memory_order_relaxed);
		flushNow = _pendingBytes >= maxPendingBytes || _pending.size() >= maxPendingCopies;

		// keep the list of uploads in flight short while a program uploads without ever submitting
		reapLocked(false, graveyard);
	}
	graveyard.clear();

	if (flushNow) {
		flush();
	}
};

void Indium::TextureUploader::flush() {
	if (!hasPending()) {
		return;
	}
	std::scoped_lock queueLock(_device.graphicsQueueMutex());
	flushLocked();
};

void Indium::TextureUploader::flushAndWait() {
	flush();
	std::vector<std::shared_ptr<void>> graveyard;
	{
		std::scoped_lock lock(_mutex);
		reapLocked(true, graveyard);
	}
	graveyard.clear();
};

void Indium::TextureUploader::flushLocked() {
	if (!hasPending()) {
		return;
	}
	std::vector<std::shared_ptr<void>> graveyard;
	{
		std::scoped_lock lock(_mutex);
		submitPendingLocked();
		reapLocked(false, graveyard);
	}
	graveyard.clear();
};

void Indium::TextureUploader::submitPendingLocked() {
	if (_pending.empty()) {
		return;
	}

	VkDevice device = _device.device();

	if (_timeline == VK_NULL_HANDLE) {
		VkSemaphoreTypeCreateInfo typeInfo {};
		typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo createInfo {};
		createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		createInfo.pNext = &typeInfo;
		if (DynamicVK::vkCreateSemaphore(device, &createInfo, nullptr, &_timeline) != VK_SUCCESS) {
			// an unrecoverable failure, like the other submission calls that abort
			abort();
		}
	}

	// The uploads have a command pool of their own, used only with the mutex held: a pool may be used by one thread
	// at a time, and the one-shot pool is held by readers across a queue submission, which this runs inside of.
	if (_commandPool == VK_NULL_HANDLE) {
		VkCommandPoolCreateInfo poolInfo {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = _device.graphicsQueueFamilyIndex() ? *_device.graphicsQueueFamilyIndex() : *_device.computeQueueFamilyIndex();
		if (DynamicVK::vkCreateCommandPool(device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) {
			abort();
		}
	}

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	{
		VkCommandBufferAllocateInfo allocateInfo {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandPool = _commandPool;
		allocateInfo.commandBufferCount = 1;
		if (DynamicVK::vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
			abort();
		}
	}

	VkCommandBufferBeginInfo beginInfo {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	DynamicVK::vkBeginCommandBuffer(commandBuffer, &beginInfo);

	// Everything submitted before this command buffer is in the first scope: its reads of an image have to end
	// before the image is written, and its writes (which a copy into the same texels must follow) have to be
	// visible. The images stay in the general layout, which a copy may use.
	VkMemoryBarrier barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

	InFlight record {};
	record.commandBuffer = commandBuffer;

	// Consecutive copies into one image from one staging buffer are one command, as long as no two of them
	// write the same texels (the result would be undefined); copies that do get a barrier in between, so the
	// later call's bytes win as they do with a CPU write.
	std::vector<VkBufferImageCopy> run;
	VkBuffer runBuffer = VK_NULL_HANDLE;
	VkImage runImage = VK_NULL_HANDLE;
	std::vector<VkImage> writtenSinceBarrier;

	auto flushRun = [&]() {
		if (run.empty()) {
			return;
		}
		if (std::find(writtenSinceBarrier.begin(), writtenSinceBarrier.end(), runImage) != writtenSinceBarrier.end()) {
			VkMemoryBarrier writeAfterWrite {};
			writeAfterWrite.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			writeAfterWrite.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			writeAfterWrite.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &writeAfterWrite, 0, nullptr, 0, nullptr);
			writtenSinceBarrier.clear();
		}
		DynamicVK::vkCmdCopyBufferToImage(commandBuffer, runBuffer, runImage, VK_IMAGE_LAYOUT_GENERAL, run.size(), run.data());
		writtenSinceBarrier.push_back(runImage);
		run.clear();
	};

	for (auto& pending: _pending) {
		auto texture = pending.texture.lock();
		if (!texture) {
			continue;
		}

		VkImage image = texture->image();
		VkBuffer buffer = pending.chunk->buffer->buffer();
		bool conflicts = false;
		if (!run.empty() && (buffer == runBuffer && image == runImage)) {
			for (const auto& earlier: run) {
				if (copiesOverlap(earlier, pending.copy)) {
					conflicts = true;
					break;
				}
			}
		}
		if (!run.empty() && (conflicts || buffer != runBuffer || image != runImage || run.size() >= maxRunLength)) {
			flushRun();
		}
		runBuffer = buffer;
		runImage = image;
		run.push_back(pending.copy);

		if (record.textures.empty() || record.textures.back() != texture) {
			record.textures.push_back(texture);
		}
		if (record.chunks.empty() || record.chunks.back() != pending.chunk) {
			record.chunks.push_back(pending.chunk);
		}
	}
	flushRun();

	// the host wrote the staged bytes before the submission (made visible by it); later commands see the images
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

	DynamicVK::vkEndCommandBuffer(commandBuffer);

	record.value = ++_lastValue;

	VkCommandBufferSubmitInfo commandBufferInfo {};
	commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandBufferInfo.commandBuffer = commandBuffer;
	VkSemaphoreSubmitInfo signalInfo {};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = _timeline;
	signalInfo.value = record.value;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSubmitInfo2 submitInfo {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalInfo;
	// the caller holds the graphics queue mutex
	if (DynamicVK::vkQueueSubmit2(_device.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		abort();
	}

	_inFlight.push_back(std::move(record));
	_pending.clear();
	_pendingBytes = 0;
	_pendingCount.store(0, std::memory_order_relaxed);
	_current.reset();
};

void Indium::TextureUploader::reapLocked(bool wait, std::vector<std::shared_ptr<void>>& graveyard) {
	if (_inFlight.empty() || _timeline == VK_NULL_HANDLE) {
		return;
	}
	VkDevice device = _device.device();

	if (wait) {
		VkSemaphoreWaitInfo waitInfo {};
		waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &_timeline;
		waitInfo.pValues = &_lastValue;
		DynamicVK::vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
	}

	uint64_t reached = 0;
	if (DynamicVK::vkGetSemaphoreCounterValue(device, _timeline, &reached) != VK_SUCCESS) {
		return;
	}

	size_t done = 0;
	while (done < _inFlight.size() && _inFlight[done].value <= reached) {
		++done;
	}
	if (done == 0) {
		return;
	}

	for (size_t i = 0; i < done; ++i) {
		DynamicVK::vkFreeCommandBuffers(device, _commandPool, 1, &_inFlight[i].commandBuffer);
	}
	for (size_t i = 0; i < done; ++i) {
		for (auto& chunk: _inFlight[i].chunks) {
			if (chunk->reusable && _freeChunks.size() < maxFreeChunks) {
				chunk->used = 0;
				_freeChunks.push_back(std::move(chunk));
			} else {
				graveyard.push_back(std::move(chunk));
			}
		}
		for (auto& texture: _inFlight[i].textures) {
			graveyard.push_back(std::move(texture));
		}
	}
	_inFlight.erase(_inFlight.begin(), _inFlight.begin() + done);
};

void Indium::TextureUploader::releaseAll() {
	if (!_device.logicalDeviceCreated()) {
		return;
	}
	flush();

	std::vector<std::shared_ptr<void>> graveyard;
	{
		std::scoped_lock lock(_mutex);
		reapLocked(true, graveyard);
		for (auto& chunk: _freeChunks) {
			graveyard.push_back(std::move(chunk));
		}
		_freeChunks.clear();
		_current.reset();
		if (_commandPool != VK_NULL_HANDLE) {
			DynamicVK::vkDestroyCommandPool(_device.device(), _commandPool, nullptr);
			_commandPool = VK_NULL_HANDLE;
		}
		if (_timeline != VK_NULL_HANDLE) {
			DynamicVK::vkDestroySemaphore(_device.device(), _timeline, nullptr);
			_timeline = VK_NULL_HANDLE;
			_lastValue = 0;
		}
	}
	graveyard.clear();
};
