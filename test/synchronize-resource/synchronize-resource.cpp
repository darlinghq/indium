// BlitCommandEncoder::synchronize, Metal's synchronizeResource: and synchronizeTexture:slice:level:, on managed
// resources the GPU wrote: a buffer filled by the GPU, a texture cleared by a render pass and a texture over a
// buffer the GPU filled. Each is read from the host after waitUntilCompleted, and starts with a poison pattern so
// that an untouched resource cannot pass. Needs a Vulkan device; no shaders. On memory that is host-coherent (all
// of an NVIDIA desktop GPU's host-visible types) the reads succeed without synchronize too: there the test shows
// that the calls work in a command buffer, not that the invalidate is needed.
#include <indium/indium.hpp>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main() {
    Indium::init(nullptr, 0, false);
    auto device = Indium::createSystemDefaultDevice();
    if (!device) return 2;
    std::atomic<bool> running{true};
    std::thread polling([&] { while (running) device->pollEvents(UINT64_MAX); });
    unsigned failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    try {
        auto queue = device->newCommandQueue();

        // a managed buffer the GPU fills
        {
            auto buffer = device->newBuffer(256, Indium::ResourceOptions::StorageModeManaged);
            std::memset(buffer->contents(), 0xa5, 256);
            buffer->didModifyRange({0, 256});
            auto command = queue->commandBuffer();
            auto blit = command->blitCommandEncoder();
            blit->fillBuffer(buffer, {0, 256}, 0x5a);
            blit->synchronize(buffer);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            auto bytes = static_cast<const unsigned char*>(buffer->contents());
            size_t mismatches = 0;
            for (size_t i = 0; i < 256; ++i) if (bytes[i] != 0x5a) ++mismatches;
            std::printf("  buffer mismatched_bytes=%zu\n", mismatches);
            check(mismatches == 0, "a managed buffer reads what the GPU filled it with");
        }

        // a managed texture a render pass clears
        {
            auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 8, 8, false);
            descriptor.resourceOptions = Indium::ResourceOptions::StorageModeManaged;
            descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
            auto texture = device->newTexture(descriptor);
            std::vector<unsigned char> poison(8 * 8 * 4, 0xa5);
            texture->replaceRegion(Indium::Region::make2D(0, 0, 8, 8), 0, poison.data(), 8 * 4);
            Indium::RenderPassDescriptor pass {};
            Indium::RenderPassColorAttachmentDescriptor attachment {};
            attachment.texture = texture;
            attachment.loadAction = Indium::LoadAction::Clear;
            attachment.storeAction = Indium::StoreAction::Store;
            attachment.clearColor = Indium::ClearColor(1, 0, 0, 1);
            pass.colorAttachments.push_back(attachment);
            auto command = queue->commandBuffer();
            command->renderCommandEncoder(pass)->endEncoding();
            auto blit = command->blitCommandEncoder();
            blit->synchronize(texture);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            std::vector<unsigned char> pixels(poison.size(), 0);
            texture->getBytes(Indium::Region::make2D(0, 0, 8, 8), 0, pixels.data(), 8 * 4);
            const unsigned char expected[4] = { 255, 0, 0, 255 };
            size_t mismatches = 0;
            for (size_t i = 0; i < pixels.size(); ++i) if (pixels[i] != expected[i % 4]) ++mismatches;
            std::printf("  texture mismatched_bytes=%zu\n", mismatches);
            check(mismatches == 0, "a managed texture reads what a render pass cleared it to");
        }

        // a texture over a managed buffer the GPU fills
        {
            auto buffer = device->newBuffer(8 * 8 * 4, Indium::ResourceOptions::StorageModeManaged);
            std::memset(buffer->contents(), 0xa5, 8 * 8 * 4);
            buffer->didModifyRange({0, 8 * 8 * 4});
            auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 8, 8, false);
            descriptor.resourceOptions = Indium::ResourceOptions::StorageModeManaged;
            descriptor.usage = Indium::TextureUsage::ShaderRead;
            auto texture = buffer->newTexture(descriptor, 0, 8 * 4);
            auto command = queue->commandBuffer();
            auto blit = command->blitCommandEncoder();
            blit->fillBuffer(buffer, {0, 8 * 8 * 4}, 0x3c);
            blit->synchronize(texture);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            std::vector<unsigned char> pixels(8 * 8 * 4, 0);
            texture->getBytes(Indium::Region::make2D(0, 0, 8, 8), 0, pixels.data(), 8 * 4);
            size_t mismatches = 0;
            for (unsigned char byte: pixels) if (byte != 0x3c) ++mismatches;
            std::printf("  buffer texture mismatched_bytes=%zu\n", mismatches);
            check(mismatches == 0, "a texture over a managed buffer reads what the GPU filled the buffer with");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL exception: %s\n", error.what());
        ++failures;
    }
    running = false; device->wakeupEventLoop(); polling.join(); device.reset();
    Indium::finit();
    std::printf("RESULT failures=%u\n", failures);
    return failures ? 1 : 0;
}
