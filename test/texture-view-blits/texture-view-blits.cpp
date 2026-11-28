// Copyright (C) 2026 Darling Developers
//
// The levels and slices of a texture view are those of the view: its level 0 and slice 0 are the first level and
// slice of the ranges it was made with (MTLTexture's parentRelativeLevel and parentRelativeSlice). A blit command
// that names a level and a slice of a view therefore reads or writes that level and slice of the view, which is
// another level and slice of the texture it was made from:
//  - a copy from a buffer into a view, and from a view into a buffer;
//  - a copy from a view into a texture, and from a texture into a view;
//  - mipmap generation for a view, which fills the view's levels from its first level and leaves the texture's
//    levels before the view and its other slices as they were;
//  - the same for a view made of a view with newTextureView(pixelFormat), which has all the levels and slices of
//    the view it is made from;
//  - a multisample resolve into a view that the store action asks for once the pass is encoded
//    (setColorStoreAction), which is done outside of the pass with the images.
// Every level and slice of the texture starts with its own texel value, and each check reads the texture back
// with getBytes, so a command that addresses the texture's own numbers instead shows as the wrong subresource
// changing.
// Prints SKIP and exits 0 without a Vulkan device.

#include <indium/indium.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	std::fflush(stdout);
	failures += !ok;
}

constexpr size_t rootSize = 8;
constexpr size_t rootLevels = 4; // 8x8, 4x4, 2x2, 1x1
constexpr size_t rootSlices = 3;

size_t levelSize(size_t level) {
	return rootSize >> level;
}

// the value every texel of a level and slice of the texture starts with
uint32_t initialTexel(size_t level, size_t slice) {
	return 0xff000000u | static_cast<uint32_t>(slice) << 16 | static_cast<uint32_t>(level) << 8 | 0x10u;
}

void fill(const std::shared_ptr<Indium::Texture>& texture, size_t level, size_t slice, uint32_t texel) {
	size_t size = levelSize(level);
	std::vector<uint32_t> texels(size * size, texel);
	texture->replaceRegion(Indium::Region::make2D(0, 0, size, size), level, slice, texels.data(), size * 4, size * size * 4);
}

// whether every texel of a level and slice of the texture is `texel`; prints what it holds when not
bool holds(const std::shared_ptr<Indium::Texture>& texture, size_t level, size_t slice, uint32_t texel) {
	size_t size = levelSize(level);
	std::vector<uint32_t> texels(size * size, 0);
	texture->getBytes(Indium::Region::make2D(0, 0, size, size), level, slice, texels.data(), size * 4, size * size * 4);
	for (auto value: texels) {
		if (value != texel) {
			std::printf("     level %zu slice %zu holds %08x, expected %08x\n", level, slice, value, texel);
			return false;
		}
	}
	return true;
}

std::shared_ptr<Indium::Texture> newRoot(const std::shared_ptr<Indium::Device>& device) {
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, rootSize, rootSize, true);
	descriptor.textureType = Indium::TextureType::e2DArray;
	descriptor.arrayLength = rootSlices;
	auto texture = device->newTexture(descriptor);
	for (size_t level = 0; level < rootLevels; ++level) {
		for (size_t slice = 0; slice < rootSlices; ++slice) {
			fill(texture, level, slice, initialTexel(level, slice));
		}
	}
	return texture;
}

// whether every level and slice of the texture still holds its initial value, except `changed` ones
bool othersUnchanged(const std::shared_ptr<Indium::Texture>& texture, std::initializer_list<std::pair<size_t, size_t>> changed) {
	bool ok = true;
	for (size_t level = 0; level < rootLevels; ++level) {
		for (size_t slice = 0; slice < rootSlices; ++slice) {
			bool skip = false;
			for (auto& subresource: changed) {
				skip = skip || (subresource.first == level && subresource.second == slice);
			}
			if (!skip) {
				ok = holds(texture, level, slice, initialTexel(level, slice)) && ok;
			}
		}
	}
	return ok;
}

}

int main() {
	// a submission that never completes is a failure, not a hang
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(60));
		std::printf("FAIL timed out\n");
		std::fflush(stdout);
		_exit(1);
	}).detach();

	Indium::init(nullptr, 0, false);

	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto queue = device->newCommandQueue();
		auto run = [&](auto encode) {
			auto commandBuffer = queue->commandBuffer();
			auto blit = commandBuffer->blitCommandEncoder();
			encode(blit);
			blit->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();
		};
		// the texture's levels 1 to 3 and slices 1 and 2: the view's level l and slice s are the texture's l + 1 and s + 1
		auto viewOf = [](const std::shared_ptr<Indium::Texture>& texture) {
			return texture->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2DArray, Indium::Range<size_t> { 1, 3 }, Indium::Range<size_t> { 1, 2 });
		};
		constexpr uint32_t copied = 0xff3366ccu;

		{
			auto texture = newRoot(device);
			auto view = viewOf(texture);
			// the view's level 1 and slice 0: the texture's level 2 and slice 1, which is 2x2
			auto source = device->newBuffer(2 * 2 * 4, Indium::ResourceOptions::StorageModeShared);
			std::vector<uint32_t> texels(2 * 2, copied);
			std::memcpy(source->contents(), texels.data(), texels.size() * 4);
			run([&](auto& blit) {
				blit->copy(source, 0, 2 * 4, 2 * 2 * 4, Indium::Size { 2, 2, 1 }, view, 0, 1, Indium::Origin { 0, 0, 0 });
			});
			bool ok = holds(texture, 2, 1, copied);
			ok = othersUnchanged(texture, { { 2, 1 } }) && ok;
			report(ok, "a copy from a buffer into level 1 and slice 0 of a view writes the texture's level 2 and slice 1");
		}

		{
			auto texture = newRoot(device);
			auto view = viewOf(texture);
			auto destination = device->newBuffer(4 * 4 * 4, Indium::ResourceOptions::StorageModeShared);
			std::memset(destination->contents(), 0, 4 * 4 * 4);
			run([&](auto& blit) {
				blit->copy(view, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 4, 4, 1 }, destination, 0, 4 * 4, 4 * 4 * 4);
			});
			auto texels = static_cast<const uint32_t*>(destination->contents());
			bool ok = true;
			for (size_t i = 0; i < 4 * 4; ++i) {
				ok = ok && texels[i] == initialTexel(1, 1);
			}
			if (!ok) {
				std::printf("     the buffer holds %08x, expected %08x\n", texels[0], initialTexel(1, 1));
			}
			report(ok, "a copy from level 0 and slice 0 of a view into a buffer reads the texture's level 1 and slice 1");
		}

		{
			auto texture = newRoot(device);
			auto view = viewOf(texture);
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			auto other = device->newTexture(descriptor);
			std::vector<uint32_t> zero(4 * 4, 0);
			other->replaceRegion(Indium::Region::make2D(0, 0, 4, 4), 0, zero.data(), 4 * 4);
			run([&](auto& blit) {
				blit->copy(view, 1, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 4, 4, 1 }, other, 0, 0, Indium::Origin { 0, 0, 0 });
			});
			std::vector<uint32_t> texels(4 * 4, 0);
			other->getBytes(Indium::Region::make2D(0, 0, 4, 4), 0, texels.data(), 4 * 4);
			bool ok = true;
			for (auto value: texels) {
				ok = ok && value == initialTexel(1, 2);
			}
			if (!ok) {
				std::printf("     the texture holds %08x, expected %08x\n", texels[0], initialTexel(1, 2));
			}
			ok = othersUnchanged(texture, {}) && ok;
			report(ok, "a copy from level 0 and slice 1 of a view into a texture reads the texture's level 1 and slice 2");
		}

		{
			auto texture = newRoot(device);
			auto view = viewOf(texture);
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			auto other = device->newTexture(descriptor);
			std::vector<uint32_t> texels(4 * 4, copied);
			other->replaceRegion(Indium::Region::make2D(0, 0, 4, 4), 0, texels.data(), 4 * 4);
			run([&](auto& blit) {
				blit->copy(other, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 4, 4, 1 }, view, 1, 0, Indium::Origin { 0, 0, 0 });
			});
			bool ok = holds(texture, 1, 2, copied);
			ok = othersUnchanged(texture, { { 1, 2 } }) && ok;
			report(ok, "a copy from a texture into level 0 and slice 1 of a view writes the texture's level 1 and slice 2");
		}

		{
			auto texture = newRoot(device);
			// the texture's levels 1 to 3 of slice 2 alone
			auto view = texture->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2D, Indium::Range<size_t> { 1, 3 }, Indium::Range<size_t> { 2, 1 });
			fill(texture, 1, 2, copied);
			run([&](auto& blit) {
				blit->generateMipmapsForTexture(view);
			});
			// every texel of level 1 is the same, so is every texel the levels are filtered down to
			bool ok = holds(texture, 2, 2, copied);
			ok = holds(texture, 3, 2, copied) && ok;
			ok = holds(texture, 1, 2, copied) && ok;
			ok = othersUnchanged(texture, { { 1, 2 }, { 2, 2 }, { 3, 2 } }) && ok;
			report(ok, "mipmap generation for a view fills the texture's levels 2 and 3 of slice 2 from its level 1, and only those");
		}

		{
			auto texture = newRoot(device);
			auto whole = viewOf(texture)->newTextureView(Indium::PixelFormat::RGBA8Unorm);
			auto destination = device->newBuffer(4 * 4 * 4, Indium::ResourceOptions::StorageModeShared);
			std::memset(destination->contents(), 0, 4 * 4 * 4);
			run([&](auto& blit) {
				blit->copy(whole, 1, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { 4, 4, 1 }, destination, 0, 4 * 4, 4 * 4 * 4);
			});
			auto texels = static_cast<const uint32_t*>(destination->contents());
			bool ok = whole->width() == 4 && whole->mipmapLevelCount() == 3 && whole->arrayLength() == 2;
			if (!ok) {
				std::printf("     the view is %zu wide with %zu levels and %zu slices, expected 4, 3 and 2\n", whole->width(), whole->mipmapLevelCount(), whole->arrayLength());
			}
			for (size_t i = 0; i < 4 * 4; ++i) {
				ok = ok && texels[i] == initialTexel(1, 2);
			}
			if (texels[0] != initialTexel(1, 2)) {
				std::printf("     the buffer holds %08x, expected %08x\n", texels[0], initialTexel(1, 2));
			}
			report(ok, "a view made of a view with its pixel format has its levels and slices: its level 0 and slice 1 are the texture's level 1 and slice 2");
		}

		{
			auto texture = newRoot(device);
			// the texture's level 1 of slice 2 alone, which is 4x4
			auto view = texture->newTextureView(Indium::PixelFormat::RGBA8Unorm, Indium::TextureType::e2D, Indium::Range<size_t> { 1, 1 }, Indium::Range<size_t> { 2, 1 });
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.textureType = Indium::TextureType::e2DMultisample;
			descriptor.sampleCount = 4;
			descriptor.usage = Indium::TextureUsage::RenderTarget;
			descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
			auto multisample = device->newTexture(descriptor);

			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments.back().texture = multisample;
			pass.colorAttachments.back().resolveTexture = view;
			pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;
			pass.colorAttachments.back().storeAction = Indium::StoreAction::Unknown;
			pass.colorAttachments.back().clearColor = Indium::ClearColor(0.2, 0.4, 0.6, 0.8);

			auto commandBuffer = queue->commandBuffer();
			auto render = commandBuffer->renderCommandEncoder(pass);
			render->setColorStoreAction(Indium::StoreAction::MultisampleResolve, 0);
			render->endEncoding();
			commandBuffer->commit();
			commandBuffer->waitUntilCompleted();

			// 0.2, 0.4, 0.6 and 0.8 as unorm8: 51, 102, 153 and 204
			bool ok = holds(texture, 1, 2, 0xcc996633u);
			ok = othersUnchanged(texture, { { 1, 2 } }) && ok;
			report(ok, "a resolve into a view that setColorStoreAction asks for writes the texture's level 1 and slice 2");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
