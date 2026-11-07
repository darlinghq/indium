// Copyright (C) 2026 Lubos Dolezel
//
// Device::newTextureWithDMABuf: a texture whose storage is a linear image in a Linux DMA-BUF sees
// the CPU's writes to the buffer and its own writes reach the buffer, at the caller's offset and
// row pitch. Imported textures and textures from the memory pool can be created and destroyed in
// any order. Asking whether the device can import creates no Vulkan device. Invalid layouts and
// descriptors return nullptr.
//
// The buffer is made with udmabuf from a sealed memfd (Linux kernel documentation); without
// /dev/udmabuf, or on a device without the extensions, the import checks are skipped.

#include <indium/indium.hpp>
#include <indium/device.private.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

constexpr size_t width = 61, height = 23;
constexpr size_t bytesPerRow = 256;
constexpr size_t offset = 4096;
constexpr size_t bufferSize = offset + 7 * 4096;

uint32_t pattern(size_t x, size_t y, uint32_t salt) {
	return static_cast<uint32_t>(x | (y << 8) | (((x * 7 + y * 13 + salt) & 0xff) << 16)) | 0xff000000u;
}

struct Buffer {
	int memfd = -1;
	int dmabuf = -1;
	uint8_t* bytes = nullptr;
};

// Returns false if the host cannot make a DMA-BUF.
bool makeBuffer(Buffer& buffer) {
	buffer.memfd = memfd_create("indium-dmabuf-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (buffer.memfd < 0 || ftruncate(buffer.memfd, bufferSize) != 0 || fcntl(buffer.memfd, F_ADD_SEALS, F_SEAL_SHRINK) != 0) {
		return false;
	}
	int device = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
	if (device < 0) {
		return false;
	}
	udmabuf_create create {};
	create.memfd = static_cast<uint32_t>(buffer.memfd);
	create.flags = UDMABUF_FLAGS_CLOEXEC;
	create.offset = 0;
	create.size = bufferSize;
	buffer.dmabuf = ioctl(device, UDMABUF_CREATE, &create);
	close(device);
	if (buffer.dmabuf < 0) {
		return false;
	}
	void* mapped = mmap(nullptr, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.memfd, 0);
	if (mapped == MAP_FAILED) {
		return false;
	}
	buffer.bytes = static_cast<uint8_t*>(mapped);
	return true;
}

uint32_t& word(Buffer& buffer, size_t x, size_t y) {
	return *reinterpret_cast<uint32_t*>(buffer.bytes + offset + y * bytesPerRow + x * 4);
}

Indium::TextureDescriptor descriptor() {
	auto desc = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, width, height, false);
	desc.usage = Indium::TextureUsage::ShaderRead | Indium::TextureUsage::RenderTarget;
	desc.allowGPUOptimizedContents = false;
	desc.resourceOptions = Indium::ResourceOptions::StorageModeShared;
	return desc;
}

std::shared_ptr<Indium::Texture> pooledTexture(const std::shared_ptr<Indium::Device>& device) {
	auto desc = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 32, 32, false);
	return device->newTexture(desc);
}

}

int main() {
	Indium::init(nullptr, 0, false);
	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}
		auto privateDevice = std::static_pointer_cast<Indium::PrivateDevice>(device);

		bool supported = device->supportsDMABufImport();
		std::printf("INFO %s, DMA-BUF import %s\n", device->name().c_str(), supported ? "supported" : "unsupported");
		report(!privateDevice->logicalDeviceCreated(), "supportsDMABufImport() creates no Vulkan device");

		Buffer buffer;
		if (!supported || !makeBuffer(buffer)) {
			std::printf("SKIP %s\n", supported ? "no /dev/udmabuf" : "no DMA-BUF import");
			report(device->newTextureWithDMABuf(descriptor(), -1, offset, bytesPerRow) == nullptr, "an invalid descriptor returns nullptr");
			Indium::finit();
			return failures ? 1 : 0;
		}

		for (size_t y = 0; y < height; ++y) {
			for (size_t x = 0; x < width; ++x) {
				word(buffer, x, y) = pattern(x, y, 1);
			}
		}

		// a pooled texture before the import, so that the import is not the pool's first user
		auto before = pooledTexture(device);
		auto texture = device->newTextureWithDMABuf(descriptor(), buffer.dmabuf, offset, bytesPerRow);
		report(texture != nullptr, "newTextureWithDMABuf() imports a linear RGBA8 buffer");
		report(privateDevice->logicalDeviceCreated(), "newTextureWithDMABuf() creates the Vulkan device");
		if (texture) {
			std::vector<uint32_t> pixels(width * height);
			Indium::Region region { { 0, 0, 0 }, { width, height, 1 } };
			texture->getBytes(region, 0, pixels.data(), width * 4);
			bool matches = true;
			for (size_t y = 0; y < height; ++y) {
				for (size_t x = 0; x < width; ++x) {
					matches = matches && pixels[y * width + x] == pattern(x, y, 1);
				}
			}
			report(matches, "the texture reads the CPU's writes at the import offset and row pitch");

			for (size_t y = 0; y < height; ++y) {
				for (size_t x = 0; x < width; ++x) {
					pixels[y * width + x] = pattern(x, y, 2);
				}
			}
			texture->replaceRegion(region, 0, pixels.data(), width * 4);
			matches = true;
			for (size_t y = 0; y < height; ++y) {
				for (size_t x = 0; x < width; ++x) {
					matches = matches && word(buffer, x, y) == pattern(x, y, 2);
				}
			}
			report(matches, "writes to the texture reach the buffer");
			report(*reinterpret_cast<uint32_t*>(buffer.bytes + offset - 4) == 0 && word(buffer, width, 0) == 0,
				"bytes outside the image are left alone");
		}

		// creation and destruction in mixed order: the imported memory is freed on its own, the
		// pooled ranges go back to the pool
		auto second = device->newTextureWithDMABuf(descriptor(), buffer.dmabuf, offset, bytesPerRow);
		auto after = pooledTexture(device);
		texture.reset();
		before.reset();
		auto third = pooledTexture(device);
		second.reset();
		after.reset();
		third.reset();
		auto last = device->newTextureWithDMABuf(descriptor(), buffer.dmabuf, offset, bytesPerRow);
		report(last != nullptr && pooledTexture(device) != nullptr, "imported and pooled textures come and go in any order");
		last.reset();

		report(device->newTextureWithDMABuf(descriptor(), -1, offset, bytesPerRow) == nullptr, "an invalid descriptor returns nullptr");
		report(device->newTextureWithDMABuf(descriptor(), buffer.dmabuf, offset, 16) == nullptr, "a row pitch below the row size returns nullptr");
		report(device->newTextureWithDMABuf(descriptor(), buffer.dmabuf, bufferSize - 4096, bytesPerRow) == nullptr, "an image past the end of the buffer returns nullptr");
		report(fcntl(buffer.dmabuf, F_GETFD) >= 0, "the caller keeps its descriptor");

		munmap(buffer.bytes, bufferSize);
		close(buffer.dmabuf);
		close(buffer.memfd);
	}
	Indium::finit();

	std::fflush(stdout);
	return failures ? 1 : 0;
}
