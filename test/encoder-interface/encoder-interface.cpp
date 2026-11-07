// Copyright (C) 2026 Darling Developers
//
// Checks of encoder and device entry points:
//  - buffer/texture blits honor the buffer offset and the row pitch, which for a block-compressed
//    format counts bytes per row of blocks;
//  - render encoder memory barriers record, and refuse what a render pass cannot express;
//  - the Depth24Unorm_Stencil8 support query agrees with texture creation.

#include <indium/indium.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

void submit(Indium::CommandQueue* queue, const std::function<void(Indium::CommandBuffer*)>& encode) {
	auto commandBuffer = queue->commandBuffer();
	encode(commandBuffer.get());
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

// copies `upload` (laid out with the given offset and pitch) into a texture and back out tightly packed
std::vector<uint8_t> roundTrip(Indium::Device* device, Indium::CommandQueue* queue, Indium::PixelFormat format, size_t width, size_t height,
	const std::vector<uint8_t>& upload, size_t offset, size_t bytesPerRow, size_t tightBytesPerRow, size_t tightRows)
{
	auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(format, width, height, false);
	descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
	auto texture = device->newTexture(descriptor);
	auto source = device->newBuffer(upload.data(), upload.size(), Indium::ResourceOptions::StorageModeShared);
	auto destination = device->newBuffer(tightBytesPerRow * tightRows, Indium::ResourceOptions::StorageModeShared);
	std::memset(destination->contents(), 0, tightBytesPerRow * tightRows);

	submit(queue, [&](Indium::CommandBuffer* commandBuffer) {
		auto blit = commandBuffer->blitCommandEncoder();
		blit->copy(source, offset, bytesPerRow, bytesPerRow * tightRows, Indium::Size { width, height, 1 }, texture, 0, 0, Indium::Origin { 0, 0, 0 });
		blit->copy(texture, 0, 0, Indium::Origin { 0, 0, 0 }, Indium::Size { width, height, 1 }, destination, 0, tightBytesPerRow, tightBytesPerRow * tightRows);
		blit->endEncoding();
	});

	auto bytes = static_cast<const uint8_t*>(destination->contents());
	return std::vector<uint8_t>(bytes, bytes + tightBytesPerRow * tightRows);
}

// fills rows of `rowBytes` meaningful bytes, each followed by padding, after `offset` bytes of padding
std::vector<uint8_t> padded(size_t offset, size_t rows, size_t rowBytes, size_t bytesPerRow, std::vector<uint8_t>& tight) {
	std::vector<uint8_t> data(offset + rows * bytesPerRow, 0xee);
	tight.clear();
	for (size_t row = 0; row < rows; ++row) {
		for (size_t i = 0; i < rowBytes; ++i) {
			uint8_t value = static_cast<uint8_t>(row * 37 + i * 11 + 1);
			data[offset + row * bytesPerRow + i] = value;
			tight.push_back(value);
		}
	}
	return data;
}

}

int main() {
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

		{
			std::vector<uint8_t> tight;
			auto upload = padded(64, 4, 4 * 4, 32, tight);
			auto result = roundTrip(device.get(), queue.get(), Indium::PixelFormat::RGBA8Unorm, 4, 4, upload, 64, 32, 16, 4);
			report(result == tight, "a buffer to texture blit honors the source offset and row pitch");
		}

		{
			// 8x8 BC1 is 2x2 blocks of 8 bytes; rows of blocks padded to 32 bytes
			std::vector<uint8_t> tight;
			auto upload = padded(16, 2, 2 * 8, 32, tight);
			try {
				auto result = roundTrip(device.get(), queue.get(), Indium::PixelFormat::BC1_RGBA, 8, 8, upload, 16, 32, 16, 2);
				report(result == tight, "a compressed blit counts the row pitch in rows of blocks");
			} catch (const std::exception& e) {
				std::printf("SKIP compressed blit: %s\n", e.what());
			}
		}

		{
			auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 4, 4, false);
			descriptor.usage = Indium::TextureUsage::RenderTarget;
			Indium::RenderPassDescriptor pass {};
			pass.colorAttachments.emplace_back();
			pass.colorAttachments.back().texture = device->newTexture(descriptor);
			pass.colorAttachments.back().loadAction = Indium::LoadAction::Clear;

			bool recorded = true;
			bool refused = true;
			submit(queue.get(), [&](Indium::CommandBuffer* commandBuffer) {
				auto render = commandBuffer->renderCommandEncoder(pass);
				try {
					render->memoryBarrier(Indium::BarrierScope::Textures | Indium::BarrierScope::RenderTargets, Indium::RenderStages::Fragment, Indium::RenderStages::Fragment);
					render->memoryBarrier(Indium::BarrierScope::Buffers, Indium::RenderStages::Vertex, Indium::RenderStages::Vertex | Indium::RenderStages::Fragment);
				} catch (const std::exception& e) {
					std::printf("     %s\n", e.what());
					recorded = false;
				}
				for (auto [scope, after, before]: {
					std::tuple { Indium::BarrierScope::Buffers, Indium::RenderStages::Fragment, Indium::RenderStages::Vertex },
					std::tuple { static_cast<Indium::BarrierScope>(0), Indium::RenderStages::Fragment, Indium::RenderStages::Fragment },
					std::tuple { Indium::BarrierScope::Buffers, Indium::RenderStages::Mesh, Indium::RenderStages::Fragment },
				}) {
					try {
						render->memoryBarrier(scope, after, before);
						refused = false;
					} catch (const std::invalid_argument&) {
					}
				}
				render->endEncoding();
			});
			report(recorded, "render pass memory barriers between supported stages");
			report(refused, "render pass memory barriers a render pass cannot express are refused");
		}

		{
			bool supported = device->supportsDepth24Stencil8PixelFormat();
			bool created = false;
			try {
				auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth24Unorm_Stencil8, 4, 4, false);
				descriptor.usage = Indium::TextureUsage::RenderTarget;
				descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
				created = device->newTexture(descriptor) != nullptr;
			} catch (const std::exception&) {
			}
			std::printf("     Depth24Unorm_Stencil8 supported: %d\n", supported);
			report(!supported || created, "a supported Depth24Unorm_Stencil8 render target can be created");
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	return failures ? 1 : 0;
}
