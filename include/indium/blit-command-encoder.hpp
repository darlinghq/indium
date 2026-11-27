#pragma once

#include <indium/command-encoder.hpp>
#include <indium/event.hpp>
#include <indium/base.hpp>
#include <indium/counter.hpp>

#include <memory>
#include <unordered_map>

namespace Indium {
	class Texture;
	class Buffer;

	struct BlitPassSampleBufferAttachmentDescriptor {
		std::shared_ptr<CounterSampleBuffer> sampleBuffer;
		size_t startOfEncoderSampleIndex = counterDontSample;
		size_t endOfEncoderSampleIndex = counterDontSample;
	};

	struct BlitPassDescriptor {
		std::unordered_map<size_t, BlitPassSampleBufferAttachmentDescriptor> sampleBufferAttachments;
	};

	class BlitCommandEncoder: public CommandEncoder {
	public:
		virtual ~BlitCommandEncoder() = 0;

		virtual void copy(const std::shared_ptr<Buffer>& source, size_t sourceOffset, const std::shared_ptr<Buffer>& destination, size_t destinationOffset, size_t size) = 0;
		virtual void copy(const std::shared_ptr<Buffer>& source, size_t sourceOffset, size_t sourceBytesPerRow, size_t sourceBytesPerImage, Size sourceSize, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, BlitOption options = BlitOption::None) = 0;
		virtual void copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, const std::shared_ptr<Buffer>& destination, size_t destinationOffset, size_t destinationBytesPerRow, size_t destinationBytesPerImage, BlitOption options = BlitOption::None) = 0;
		virtual void copy(const std::shared_ptr<Texture>& source, const std::shared_ptr<Texture>& destination) = 0;
		virtual void copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, size_t sliceCount, size_t levelCount) = 0;
		virtual void copy(const std::shared_ptr<Texture>& source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, const std::shared_ptr<Texture>& destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin) = 0;
		virtual void fillBuffer(const std::shared_ptr<Buffer>& buffer, Range<size_t> range, uint8_t value) = 0;
		virtual void generateMipmapsForTexture(const std::shared_ptr<Texture>& texture) = 0;

		/**
		 * Makes what the commands before this one wrote to `buffer` visible to the CPU once the command buffer
		 * has completed: its contents() then read it (Metal's synchronizeResource: on a managed buffer). For a
		 * buffer whose memory is not host-coherent, the mapped range is invalidated before the command buffer
		 * is marked completed.
		 */
		virtual void synchronize(const std::shared_ptr<Buffer>& buffer) = 0;

		/**
		 * Makes what the commands before this one wrote to `texture` visible to getBytes once the command buffer
		 * has completed (Metal's synchronizeResource: and synchronizeTexture:slice:level: on a managed
		 * texture). getBytes copies out of the image itself, so this orders the writes before it; a texture over a
		 * buffer synchronizes that buffer.
		 */
		virtual void synchronize(const std::shared_ptr<Texture>& texture) = 0;

		/**
		 * Samples the counters of `sampleBuffer` into `sampleIndex` at this point in the encoder.
		 * Does nothing for a buffer that is not from this device or an index outside the buffer.
		 */
		virtual void sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) = 0;

		/**
		 * Updates `fence` once the commands encoded so far in this encoder have finished. A fence from another
		 * device throws `std::invalid_argument`.
		 */
		virtual void updateFence(const std::shared_ptr<Fence>& fence) = 0;

		/**
		 * Holds the commands encoded after this call back until the work `fence` was updated after in earlier
		 * encoders has finished.
		 */
		virtual void waitForFence(const std::shared_ptr<Fence>& fence) = 0;
	};
};
