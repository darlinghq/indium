#pragma once

#include <indium/blit-command-encoder.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <variant>
#include <vector>

namespace Indium {
	class PrivateCommandBuffer;
	class PrivateDevice;

	class PrivateBlitCommandEncoder: public BlitCommandEncoder {
	private:
		// the command buffer always outlives us
		std::weak_ptr<PrivateCommandBuffer> _privateCommandBuffer;
		std::shared_ptr<PrivateDevice> _privateDevice;
		BlitPassDescriptor _descriptor;

		void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, size_t sliceCount, size_t levelCount, Size size);

	public:
		PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer);
		PrivateBlitCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const BlitPassDescriptor& descriptor);
		~PrivateBlitCommandEncoder();

		virtual void copy(std::shared_ptr<Buffer> source, size_t sourceOffset, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t size) override;
		virtual void copy(std::shared_ptr<Buffer> source, size_t sourceOffset, size_t sourceBytesPerRow, size_t sourceBytesPerImage, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin, BlitOption options = BlitOption::None) override;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Buffer> destination, size_t destinationOffset, size_t destinationBytesPerRow, size_t destinationBytesPerImage, BlitOption options = BlitOption::None) override;
		virtual void copy(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) override;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, size_t sliceCount, size_t levelCount) override;
		virtual void copy(std::shared_ptr<Texture> source, size_t sourceSlice, size_t sourceLevel, Origin sourceOrigin, Size sourceSize, std::shared_ptr<Texture> destination, size_t destinationSlice, size_t destinationLevel, Origin destinationOrigin) override;
		virtual void fillBuffer(std::shared_ptr<Buffer> buffer, Range<size_t> range, uint8_t value) override;
		virtual void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

		virtual void endEncoding() override;
	};
};
