#pragma once

#include <indium/command-encoder.hpp>
#include <indium/base.hpp>
#include <indium/counter.hpp>

#include <memory>
#include <unordered_map>

namespace Indium {
	class Texture;
	class Buffer;

	// TODO: check what the actual value is (this is just a good guess)
	static constexpr size_t counterDontSample = SIZE_MAX;

	struct BlitPassSampleBufferAttachmentDescriptor {
		std::shared_ptr<CounterSampleBuffer> sampleBuffer;
		size_t startOfEncoderSampleIndex;
		size_t endOfEncoderSampleIndex;
	};

	struct BlitPassDescriptor {
		std::unordered_map<size_t, BlitPassSampleBufferAttachmentDescriptor> sampleBufferAttachments;
	};

	class BlitCommandEncoder: public CommandEncoder {
	public:
		virtual ~BlitCommandEncoder() = 0;

		virtual void copy(std::shared_ptr<Buffer> source, size_t sourceOffset, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t size) = 0;
		virtual void copy(std::shared_ptr<Buffer> source, size_t sourceOffset, size_t sourceBytesPerRow, size_t sourceBytesPerImage, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, BlitOption options = BlitOption::None) = 0;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t destinationBytesPerRow, size_t destinationBytesPerImage, BlitOption options = BlitOption::None) = 0;
		virtual void copy(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) = 0;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, size_t sliceCount, size_t levelCount) = 0;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin) = 0;
		virtual void fillBuffer(std::shared_ptr<Buffer> buffer, Range<size_t> range, uint8_t value) = 0;
		virtual void generateMipmapsForTexture(std::shared_ptr<Texture> texture) = 0;
	};
};
