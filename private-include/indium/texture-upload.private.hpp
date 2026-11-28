#pragma once

// Copyright (C) 2026 Lubos Dolezel

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Indium {
	class PrivateBuffer;
	class PrivateDevice;
	class PrivateTexture;

	/**
	 * Texture uploads that are written by the host but need the GPU to land in an image (the memory of an
	 * optimally tiled image cannot be mapped).
	 *
	 * Metal's replaceRegion is a CPU write that is complete when the call returns and that GPU work submitted
	 * afterwards sees. To keep that without a command buffer and a fence wait per call, `stage()` copies the bytes
	 * into host-visible staging memory and remembers the copy; the pending copies are recorded together in one
	 * command buffer that is submitted, without waiting for it, the next time the device submits work
	 * (`flushLocked()`, called by the command buffer submission before its own commands), when a host read of a
	 * texture needs them (`flush()`), or when too much is waiting. The queue executes the submissions in order and
	 * the upload ends with a memory dependency on everything that follows, so the work of later command buffers
	 * sees the bytes and the uploads come after the work submitted earlier.
	 *
	 * Staging memory is carved from chunks that are reused once the GPU has finished with the upload that used
	 * them.
	 */
	class TextureUploader {
	public:
		explicit TextureUploader(PrivateDevice& device);
		~TextureUploader();

		/**
		 * Reserves `size` bytes of staging memory, has `fill` write them, and queues the copy of them to `texture`'s
		 * image as described by `copy`; its `bufferOffset` is set here, and `bufferRowLength` and `bufferImageHeight`
		 * are relative to the staged bytes. The image is in the general layout. Holds `texture` weakly until the
		 * copy is submitted: a texture that is gone needs no upload.
		 */
		void stage(const std::shared_ptr<PrivateTexture>& texture, size_t size, const VkBufferImageCopy& copy, const std::function<void(uint8_t* destination)>& fill);

		/**
		 * Submits the pending copies on the graphics queue; the caller holds `PrivateDevice::graphicsQueueMutex()`.
		 * Does not wait for the GPU.
		 */
		void flushLocked();

		/** `flushLocked()` for a caller that does not hold the graphics queue mutex. */
		void flush();

		/**
		 * `flush()`, then waits until every upload submitted so far has finished on the GPU, so that the bytes of the
		 * pending copies are in the images when it returns. For a texture whose memory other users read directly.
		 */
		void flushAndWait();

		/** Whether copies are waiting for a submission. */
		bool hasPending() const { return _pendingCount.load(std::memory_order_relaxed) != 0; }

		/**
		 * Submits what is pending, waits for every upload to finish and releases the staging memory, the
		 * command buffers and the semaphore. Staging buffers and textures that are still in flight hold the
		 * device, so this is what lets it be destroyed.
		 */
		void releaseAll();

	private:
		struct Chunk {
			std::shared_ptr<PrivateBuffer> buffer;
			size_t capacity = 0;
			size_t used = 0;
			uint8_t* mapped = nullptr;
			bool reusable = false;
		};

		struct Pending {
			std::weak_ptr<PrivateTexture> texture;
			std::shared_ptr<Chunk> chunk;
			VkBufferImageCopy copy;
		};

		struct InFlight {
			uint64_t value;
			VkCommandBuffer commandBuffer;
			std::vector<std::shared_ptr<Chunk>> chunks;
			std::vector<std::shared_ptr<PrivateTexture>> textures;
		};

		PrivateDevice& _device;

		std::mutex _mutex;
		std::vector<Pending> _pending;
		std::atomic<size_t> _pendingCount { 0 };
		size_t _pendingBytes = 0;
		std::shared_ptr<Chunk> _current;
		std::vector<std::shared_ptr<Chunk>> _freeChunks;
		std::vector<InFlight> _inFlight;
		VkCommandPool _commandPool = VK_NULL_HANDLE;
		VkSemaphore _timeline = VK_NULL_HANDLE;
		uint64_t _lastValue = 0;

		std::shared_ptr<Chunk> newChunk(size_t size);
		/**
		 * Retires the uploads the GPU has finished (all of them with `wait`). What they held is moved to `graveyard`
		 * for the caller to drop once nothing is locked: a texture or buffer may hold the last reference to the device.
		 */
		void reapLocked(bool wait, std::vector<std::shared_ptr<void>>& graveyard);
		void submitPendingLocked();
	};
};
