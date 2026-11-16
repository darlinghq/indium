// Copyright (C) 2026 Lubos Dolezel
//
// Metal's replaceRegion is a CPU write that is complete when the call returns: GPU work committed afterwards sees
// it (and a command buffer that was committed earlier but has not started does too), getBytes returns it at once,
// and a GPU write that completed afterwards is the last word. Indium batches the writes and submits them with the
// next work; this checks that this cannot be told from a write at the time of the call, that many small writes and
// large ones arrive, that views of a texture address its levels and slices, and what the storage modes allow.

#include <indium/indium.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	std::fflush(stdout);
	failures += !ok;
}

using namespace Indium;

std::shared_ptr<Device> device;
std::shared_ptr<CommandQueue> queue;

constexpr uint32_t red = 0xff0000ff;

std::vector<uint8_t> pattern(size_t count, uint8_t base) {
	std::vector<uint8_t> bytes(count);
	for (size_t i = 0; i < count; ++i) {
		bytes[i] = static_cast<uint8_t>(base + i * 7);
	}
	return bytes;
}

TextureDescriptor descriptor2D(PixelFormat format, size_t width, size_t height, size_t levels = 1, StorageMode mode = StorageMode::Shared) {
	auto d = TextureDescriptor::texture2DDescriptor(format, width, height, false);
	d.mipmapLevelCount = levels;
	d.usage = TextureUsage::ShaderRead | TextureUsage::RenderTarget | TextureUsage::PixelFormatView;
	d.resourceOptions = (mode == StorageMode::Shared ? ResourceOptions::StorageModeShared : mode == StorageMode::Private ? ResourceOptions::StorageModePrivate : ResourceOptions::StorageModeManaged);
	return d;
}

std::vector<uint8_t> read(const std::shared_ptr<Texture>& texture, size_t width, size_t height, size_t level = 0, size_t slice = 0, size_t bpp = 4) {
	std::vector<uint8_t> bytes(width * height * bpp, 0xee);
	texture->getBytes(Region::make2D(0, 0, width, height), level, slice, bytes.data(), width * bpp, 0);
	return bytes;
}

void write(const std::shared_ptr<Texture>& texture, size_t width, size_t height, const std::vector<uint8_t>& bytes, size_t level = 0, size_t slice = 0, size_t bpp = 4) {
	texture->replaceRegion(Region::make2D(0, 0, width, height), level, slice, bytes.data(), width * bpp, 0);
}

RenderPassDescriptor clearPass(const std::shared_ptr<Texture>& texture, double r) {
	RenderPassDescriptor pass {};
	pass.colorAttachments.emplace_back();
	pass.colorAttachments.back().texture = texture;
	pass.colorAttachments.back().loadAction = LoadAction::Clear;
	pass.colorAttachments.back().storeAction = StoreAction::Store;
	pass.colorAttachments.back().clearColor = ClearColor(r, 0, 0, 1);
	return pass;
}

// a command buffer that copies the texture into `buffer`, committed and completed
void gpuRead(const std::shared_ptr<Texture>& texture, const std::shared_ptr<Buffer>& buffer, size_t width, size_t height) {
	auto commandBuffer = queue->commandBuffer();
	auto blit = commandBuffer->blitCommandEncoder();
	blit->copy(texture, 0, 0, Origin { 0, 0, 0 }, Size { width, height, 1 }, buffer, 0, width * 4, width * height * 4);
	blit->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

std::shared_ptr<Buffer> sharedBuffer(size_t size) {
	auto buffer = device->newBuffer(size, ResourceOptions::StorageModeShared);
	std::memset(buffer->contents(), 0xee, size);
	return buffer;
}

bool equal(const void* a, const std::vector<uint8_t>& b) {
	return std::memcmp(a, b.data(), b.size()) == 0;
}

void roundTrips() {
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 8, 6));
		auto bytes = pattern(8 * 6 * 4, 1);
		write(texture, 8, 6, bytes);
		report(read(texture, 8, 6) == bytes, "getBytes returns what replaceRegion wrote (shared)");
	}
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 8, 6, 1, StorageMode::Managed));
		auto bytes = pattern(8 * 6 * 4, 3);
		write(texture, 8, 6, bytes);
		report(read(texture, 8, 6) == bytes, "getBytes returns what replaceRegion wrote (managed)");
	}
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 8, 6));
		auto zero = std::vector<uint8_t>(8 * 6 * 4, 0);
		write(texture, 8, 6, zero);
		auto part = pattern(3 * 2 * 4, 0x40);
		// the source rows are 32 bytes apart although a row of the region is 12 bytes long
		std::vector<uint8_t> source(32 * 2, 0xcc);
		for (size_t row = 0; row < 2; ++row) {
			std::memcpy(source.data() + row * 32, part.data() + row * 12, 12);
		}
		texture->replaceRegion(Region { Origin { 2, 3, 0 }, Size { 3, 2, 1 } }, 0, source.data(), 32);
		auto got = read(texture, 8, 6);
		bool ok = true;
		for (size_t y = 0; y < 6; ++y) {
			for (size_t x = 0; x < 8; ++x) {
				bool inside = x >= 2 && x < 5 && y >= 3 && y < 5;
				for (size_t c = 0; c < 4; ++c) {
					uint8_t want = inside ? part[((y - 3) * 3 + (x - 2)) * 4 + c] : 0;
					ok = ok && got[(y * 8 + x) * 4 + c] == want;
				}
			}
		}
		report(ok, "a sub-region with a larger bytesPerRow lands where it belongs and nothing else changes");
	}
	{
		// the last row only needs the bytes of the region: a source that ends right after them is fine
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		auto bytes = pattern(4 * 4 * 4, 9);
		write(texture, 4, 4, std::vector<uint8_t>(64, 0));
		std::vector<uint8_t> exact(40, 0);
		for (size_t i = 0; i < 40; ++i) exact[i] = static_cast<uint8_t>(0x50 + i);
		texture->replaceRegion(Region { Origin { 0, 0, 0 }, Size { 2, 2, 1 } }, 0, exact.data(), 32);
		auto got = read(texture, 4, 4);
		report(std::memcmp(got.data(), exact.data(), 8) == 0 && std::memcmp(got.data() + 16, exact.data() + 32, 8) == 0, "only the bytes of the region are read from the source");
	}
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 8, 8, 3));
		for (size_t level = 0; level < 3; ++level) {
			write(texture, 8 >> level, 8 >> level, pattern((8 >> level) * (8 >> level) * 4, 0x10 * (level + 1)), level);
		}
		bool ok = true;
		for (size_t level = 0; level < 3; ++level) {
			ok = ok && read(texture, 8 >> level, 8 >> level, level) == pattern((8 >> level) * (8 >> level) * 4, 0x10 * (level + 1));
		}
		report(ok, "every mip level keeps its own bytes");
	}
	{
		auto d = descriptor2D(PixelFormat::RGBA8Unorm, 4, 4);
		d.textureType = TextureType::e2DArray;
		d.arrayLength = 3;
		auto texture = device->newTexture(d);
		for (size_t slice = 0; slice < 3; ++slice) {
			write(texture, 4, 4, pattern(64, 0x20 * (slice + 1)), 0, slice);
		}
		bool ok = true;
		for (size_t slice = 0; slice < 3; ++slice) {
			ok = ok && read(texture, 4, 4, 0, slice) == pattern(64, 0x20 * (slice + 1));
		}
		report(ok, "every array slice keeps its own bytes");
	}
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		write(texture, 4, 4, pattern(64, 1));
		write(texture, 4, 4, pattern(64, 2));
		auto first = pattern(16, 0x77);
		texture->replaceRegion(Region { Origin { 1, 1, 0 }, Size { 2, 2, 1 } }, 0, pattern(16, 0x77).data(), 8);
		auto got = read(texture, 4, 4);
		auto want = pattern(64, 2);
		std::memcpy(want.data() + 20, first.data(), 8);
		std::memcpy(want.data() + 36, first.data() + 8, 8);
		report(got == want, "overlapping writes land in call order");
	}
}

void ordering() {
	auto buffer = sharedBuffer(64);
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		write(texture, 4, 4, pattern(64, 0x10));
		gpuRead(texture, buffer, 4, 4);
		report(equal(buffer->contents(), pattern(64, 0x10)), "a command buffer committed after replaceRegion sees the bytes");
	}
	{
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		write(texture, 4, 4, pattern(64, 1));
		write(texture, 4, 4, pattern(64, 0x80));
		gpuRead(texture, buffer, 4, 4);
		report(equal(buffer->contents(), pattern(64, 0x80)), "a command buffer sees the last of two writes of a region");
	}
	{
		// committed first, not started (it waits for an event), written afterwards: the later write is what it reads
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		auto event = device->newSharedEvent();
		std::memset(buffer->contents(), 0xee, 64);
		write(texture, 4, 4, pattern(64, 0x10));
		auto commandBuffer = queue->commandBuffer();
		commandBuffer->encodeWaitForEvent(event, 1);
		auto blit = commandBuffer->blitCommandEncoder();
		blit->copy(texture, 0, 0, Origin { 0, 0, 0 }, Size { 4, 4, 1 }, buffer, 0, 16, 64);
		blit->endEncoding();
		commandBuffer->commit();
		usleep(100000);
		write(texture, 4, 4, pattern(64, 0x80));
		event->setSignaledValue(1);
		commandBuffer->waitUntilCompleted();
		report(equal(buffer->contents(), pattern(64, 0x80)), "a command buffer committed earlier that has not started sees a later write");
	}
	{
		// the held command buffer clears the texture: the clear is the last word, getBytes before it ran sees the write
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		auto event = device->newSharedEvent();
		write(texture, 4, 4, pattern(64, 0x10));
		auto commandBuffer = queue->commandBuffer();
		commandBuffer->encodeWaitForEvent(event, 1);
		commandBuffer->renderCommandEncoder(clearPass(texture, 1))->endEncoding();
		commandBuffer->commit();
		usleep(100000);
		write(texture, 4, 4, pattern(64, 0x80));
		bool before = read(texture, 4, 4) == pattern(64, 0x80);
		event->setSignaledValue(1);
		commandBuffer->waitUntilCompleted();
		auto after = read(texture, 4, 4);
		bool allRed = true;
		for (size_t i = 0; i < 16; ++i) {
			uint32_t pixel;
			std::memcpy(&pixel, after.data() + i * 4, 4);
			allRed = allRed && pixel == red;
		}
		report(before, "getBytes while a writer is held back returns the bytes written so far");
		report(allRed, "a GPU write that completes after a replaceRegion is the last word");
	}
	{
		// a completed GPU write, then a sub-region write: both are in getBytes
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		auto commandBuffer = queue->commandBuffer();
		commandBuffer->renderCommandEncoder(clearPass(texture, 1))->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		auto two = pattern(8, 1);
		texture->replaceRegion(Region { Origin { 1, 1, 0 }, Size { 2, 1, 1 } }, 0, two.data(), 8);
		auto got = read(texture, 4, 4);
		uint32_t corner, inside;
		std::memcpy(&corner, got.data() + 16, 4);
		std::memcpy(&inside, got.data() + 20, 4);
		report(corner == red && std::memcmp(got.data() + 20, two.data(), 8) == 0 && inside != red, "getBytes shows a completed GPU write with a later sub-region write over it");
	}
	{
		// a write between two command buffers of one queue lands between them
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		auto first = sharedBuffer(64);
		auto second = sharedBuffer(64);
		write(texture, 4, 4, pattern(64, 1));
		auto c1 = queue->commandBuffer();
		auto b1 = c1->blitCommandEncoder();
		b1->copy(texture, 0, 0, Origin { 0, 0, 0 }, Size { 4, 4, 1 }, first, 0, 16, 64);
		b1->endEncoding();
		c1->commit();
		c1->waitUntilCompleted();
		write(texture, 4, 4, pattern(64, 2));
		gpuRead(texture, second, 4, 4);
		report(equal(first->contents(), pattern(64, 1)) && equal(second->contents(), pattern(64, 2)), "a write between two completed command buffers is seen by the second only");
	}
}

void many() {
	{
		// 1000 one-texel writes of different texels, then a GPU read and a getBytes
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 64, 64));
		write(texture, 64, 64, std::vector<uint8_t>(64 * 64 * 4, 0));
		std::vector<uint8_t> want(64 * 64 * 4, 0);
		for (size_t i = 0; i < 1000; ++i) {
			size_t x = (i * 7) % 64, y = (i * 13) % 64;
			uint8_t texel[4] = { static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i * 3), 255 };
			texture->replaceRegion(Region { Origin { x, y, 0 }, Size { 1, 1, 1 } }, 0, texel, 4);
			std::memcpy(want.data() + (y * 64 + x) * 4, texel, 4);
		}
		auto buffer = sharedBuffer(64 * 64 * 4);
		gpuRead(texture, buffer, 64, 64);
		report(equal(buffer->contents(), want), "1000 one-texel writes all arrive (GPU read)");
		report(read(texture, 64, 64) == want, "1000 one-texel writes all arrive (getBytes)");
	}
	{
		// the same texel written many times: the last one wins
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		write(texture, 4, 4, std::vector<uint8_t>(64, 0));
		for (uint32_t i = 0; i < 600; ++i) {
			texture->replaceRegion(Region { Origin { 1, 2, 0 }, Size { 1, 1, 1 } }, 0, &i, 4);
		}
		auto got = read(texture, 4, 4);
		uint32_t value;
		std::memcpy(&value, got.data() + (2 * 4 + 1) * 4, 4);
		report(value == 599, "the last of 600 writes of one texel wins");
	}
	{
		// large uploads, bigger than a staging chunk
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 1024, 1024));
		auto bytes = pattern(1024 * 1024 * 4, 5);
		write(texture, 1024, 1024, bytes);
		auto texture2 = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 1500, 700));
		auto bytes2 = pattern(1500 * 700 * 4, 6);
		write(texture2, 1500, 700, bytes2);
		report(read(texture, 1024, 1024) == bytes, "a 1024x1024 upload arrives");
		report(read(texture2, 1500, 700) == bytes2, "a second large upload arrives");
	}
	{
		// many textures, each written and read after the others
		std::vector<std::shared_ptr<Texture>> textures;
		for (size_t i = 0; i < 50; ++i) {
			textures.push_back(device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 16, 16)));
			write(textures.back(), 16, 16, pattern(16 * 16 * 4, static_cast<uint8_t>(i)));
		}
		bool ok = true;
		for (size_t i = 0; i < textures.size(); ++i) {
			ok = ok && read(textures[i], 16, 16) == pattern(16 * 16 * 4, static_cast<uint8_t>(i));
		}
		report(ok, "writes to 50 textures all arrive");
	}
	{
		// a texture that is gone before its write was submitted costs nothing and breaks nothing
		auto keep = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		{
			auto doomed = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
			write(doomed, 4, 4, pattern(64, 1));
			write(keep, 4, 4, pattern(64, 2));
		}
		auto buffer = sharedBuffer(64);
		gpuRead(keep, buffer, 4, 4);
		report(equal(buffer->contents(), pattern(64, 2)), "a texture released with a write pending does not disturb the others");
	}
	{
		// textures released right after their write was submitted, while it may still be running
		for (size_t round = 0; round < 20; ++round) {
			auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 256, 256));
			write(texture, 256, 256, pattern(256 * 256 * 4, static_cast<uint8_t>(round)));
			auto commandBuffer = queue->commandBuffer();
			commandBuffer->commit();
		}
		auto probe = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4));
		write(probe, 4, 4, pattern(64, 1));
		report(read(probe, 4, 4) == pattern(64, 1), "textures released after submission leave the device working");
	}
	{
		// writers on several threads, each with its own texture
		std::vector<std::thread> threads;
		std::vector<int> ok(4, 0);
		for (size_t t = 0; t < 4; ++t) {
			threads.emplace_back([&, t]() {
				auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 32, 32));
				bool good = true;
				for (size_t i = 0; i < 50; ++i) {
					auto bytes = pattern(32 * 32 * 4, static_cast<uint8_t>(t * 50 + i));
					write(texture, 32, 32, bytes);
					if (i % 10 == 0) {
						auto commandBuffer = queue->commandBuffer();
						commandBuffer->commit();
					}
					good = good && read(texture, 32, 32) == bytes;
				}
				ok[t] = good;
			});
		}
		for (auto& thread: threads) thread.join();
		report(ok[0] && ok[1] && ok[2] && ok[3], "writers on four threads each read back their own writes");
	}
}

void views() {
	auto d = descriptor2D(PixelFormat::RGBA8Unorm, 8, 8, 3);
	d.textureType = TextureType::e2DArray;
	d.arrayLength = 6;
	auto base = device->newTexture(d);
	for (size_t level = 0; level < 3; ++level) {
		for (size_t slice = 0; slice < 6; ++slice) {
			write(base, 8 >> level, 8 >> level, std::vector<uint8_t>((8 >> level) * (8 >> level) * 4, 0), level, slice);
		}
	}

	{
		auto view = base->newTextureView(PixelFormat::RGBA8Unorm, TextureType::e2DArray, Range<size_t> { 1, 2 }, Range<size_t> { 2, 3 });
		report(view && view->width() == 4 && view->height() == 4, "a view that starts at level 1 reports the size of that level");
		write(view, 4, 4, pattern(64, 0x40), 0, 1);
		report(read(base, 4, 4, 1, 3) == pattern(64, 0x40), "replaceRegion on a view reaches the level and slice it addresses");
		bool others = true;
		for (size_t slice = 0; slice < 6; ++slice) {
			if (slice != 3) others = others && read(base, 4, 4, 1, slice) == std::vector<uint8_t>(64, 0);
		}
		others = others && read(base, 8, 8, 0, 3) == std::vector<uint8_t>(256, 0);
		report(others, "nothing else in the texture changed");
		report(read(view, 4, 4, 0, 1) == pattern(64, 0x40), "getBytes on a view reads the level and slice it addresses");
		write(base, 2, 2, pattern(16, 0x11), 2, 4);
		report(read(view, 2, 2, 1, 2) == pattern(16, 0x11), "getBytes on a view sees a write to its texture");
	}
	{
		PixelFormat formats[] = { PixelFormat::BGRA8Unorm, PixelFormat::RGBA8Unorm_sRGB, PixelFormat::RGBA8Snorm };
		bool ok = true;
		for (auto format: formats) {
			auto view = base->newTextureView(format, TextureType::e2DArray, Range<size_t> { 0, 1 }, Range<size_t> { 0, 1 });
			if (!view) { ok = false; continue; }
			auto bytes = pattern(256, 0x21 + static_cast<uint8_t>(format));
			write(view, 8, 8, bytes);
			ok = ok && read(base, 8, 8) == bytes;
			auto bytes2 = pattern(256, 0x91);
			write(base, 8, 8, bytes2);
			ok = ok && read(view, 8, 8) == bytes2;
		}
		report(ok, "views of another pixel format of the same texel size pass the bytes through unchanged");
	}
	{
		auto view = base->newTextureView(PixelFormat::RGBA8Unorm, TextureType::e2D, Range<size_t> { 1, 1 }, Range<size_t> { 4, 1 });
		write(view, 4, 4, pattern(64, 0x40));
		report(read(base, 4, 4, 1, 4) == pattern(64, 0x40), "a 2D view of one level and slice of an array");
		auto v1 = base->newTextureView(PixelFormat::RGBA8Unorm, TextureType::e2DArray, Range<size_t> { 1, 2 }, Range<size_t> { 2, 3 });
		auto vv = v1->newTextureView(PixelFormat::RGBA8Unorm, TextureType::e2DArray, Range<size_t> { 1, 1 }, Range<size_t> { 1, 2 });
		write(vv, 2, 2, pattern(16, 0x70), 0, 1);
		report(read(base, 2, 2, 2, 4) == pattern(16, 0x70) && read(base, 2, 2, 2, 3) == std::vector<uint8_t>(16, 0), "the levels and slices of a view of a view add up");
	}
	{
		auto cube = base->newTextureView(PixelFormat::RGBA8Unorm, TextureType::eCube, Range<size_t> { 0, 1 }, Range<size_t> { 0, 6 });
		write(cube, 8, 8, pattern(256, 0x33), 0, 5);
		report(read(base, 8, 8, 0, 5) == pattern(256, 0x33) && read(cube, 8, 8, 0, 5) == pattern(256, 0x33), "a cube view addresses the faces as slices");
	}
	{
		// a write through a view is in the next GPU read of the texture
		auto view = base->newTextureView(PixelFormat::RGBA8Unorm, TextureType::e2DArray, Range<size_t> { 0, 1 }, Range<size_t> { 1, 1 });
		write(view, 8, 8, pattern(256, 0x66), 0, 0);
		auto buffer = sharedBuffer(256);
		auto commandBuffer = queue->commandBuffer();
		auto blit = commandBuffer->blitCommandEncoder();
		blit->copy(base, 1, 0, Origin { 0, 0, 0 }, Size { 8, 8, 1 }, buffer, 0, 32, 256);
		blit->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		report(equal(buffer->contents(), pattern(256, 0x66)), "a write through a view is seen by GPU work on the texture");
	}
}

void storageModes() {
	bool threw = false;
	try {
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4, 1, StorageMode::Private));
		auto bytes = pattern(64, 1);
		texture->replaceRegion(Region::make2D(0, 0, 4, 4), 0, bytes.data(), 16);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	report(threw, "replaceRegion on a private texture is refused");
	threw = false;
	try {
		auto texture = device->newTexture(descriptor2D(PixelFormat::RGBA8Unorm, 4, 4, 1, StorageMode::Private));
		auto bytes = pattern(64, 1);
		texture->getBytes(Region::make2D(0, 0, 4, 4), 0, bytes.data(), 16);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	report(threw, "getBytes on a private texture is refused");
}

}

int main() {
	Indium::init(nullptr, 0, false);
	{
		device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}
		std::printf("device: %s\n", device->name().c_str());

		bool keepPollingDevice = true;
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		queue = device->newCommandQueue();

		// a section that throws counts as a failure of the section instead of ending the run
		auto section = [](const char* name, void (*run)()) {
			try {
				run();
			} catch (const std::exception& error) {
				report(false, std::string(name) + ": threw " + error.what());
			}
		};
		section("round trips", roundTrips);
		section("ordering", ordering);
		section("many writes", many);
		section("views", views);
		section("storage modes", storageModes);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
		queue.reset();
		device.reset();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
