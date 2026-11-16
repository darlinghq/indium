#pragma once

#include <indium/resource.hpp>
#include <indium/texture.hpp>
#include <indium/types.hpp>

#include <memory>

namespace Indium {
	class Buffer: public Resource {
	public:
		virtual ~Buffer() = 0;

		virtual size_t length() const = 0;
		virtual void* contents() = 0;
		virtual void didModifyRange(Range<size_t> range) = 0;

		virtual uint64_t gpuAddress() = 0;

		/**
		 * A texture whose pixels are the rows of this buffer: row `y` starts at `offset + y * bytesPerRow`, so
		 * what the CPU or the GPU writes to the buffer is what the texture reads and what is written to the texture
		 * is in the buffer. Needs a 2D texture of one level, one slice and one sample in a format that is not
		 * compressed, with the storage mode of the buffer, an `offset` that is a multiple of 16 bytes and a `bytesPerRow`
		 * that is a multiple of `Device::minimumLinearTextureAlignment()` for the format and holds a row. Where the
		 * device's linear images have that row pitch the texture's image lies in the buffer's memory; for any other
		 * pitch it has an image of its own, which is copied from the buffer before each encoder that uses the texture
		 * and into the buffer after each encoder that may have written it (so a texture that is in use by a command
		 * buffer is only coherent with the buffer at the encoder boundaries, the way Metal's tracking of a buffer
		 * and a texture of it is). The texture keeps the buffer alive.
		 *
		 * @returns nullptr if the texture cannot be made that way on this device.
		 */
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor, size_t offset, size_t bytesPerRow) = 0;
	};
};
