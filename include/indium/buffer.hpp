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
		 * compressed, with the storage mode of the buffer and a `bytesPerRow` that is the row pitch the device uses
		 * for such a texture (a multiple of `Device::minimumLinearTextureAlignment()` for the format). The texture
		 * keeps the buffer alive.
		 *
		 * @returns nullptr if the texture cannot be made that way on this device.
		 */
		virtual std::shared_ptr<Texture> newTexture(const TextureDescriptor& descriptor, size_t offset, size_t bytesPerRow) = 0;
	};
};
