// QUIRK test (see the QUIRK comment on ShaderWrite in ConcreteTexture's constructor): a private multisample
// texture created with RenderTarget, ShaderRead and ShaderWrite usage, as WebKit's ANGLE creates a multisample
// renderbuffer, is made rather than refused, and works as a render target that resolves: cleared red at 2x, 4x
// and 8x, resolved into a shared texture that starts with a poison pattern. Needs a Vulkan device; no shaders.
#include <indium/indium.hpp>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    Indium::init(nullptr, 0, false);
    auto device = Indium::createSystemDefaultDevice();
    if (!device) return 2;
    std::atomic<bool> running{true};
    std::thread polling([&] { while (running) device->pollEvents(UINT64_MAX); });
    unsigned failures = 0;
    try {
        auto queue = device->newCommandQueue();
        for (uint32_t samples : {2u, 4u, 8u}) {
            if (!device->supportsTextureSampleCount(samples)) {
                std::printf("SKIP samples=%u is not supported\n", samples);
                continue;
            }
            auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 16, 12, false);
            descriptor.textureType = Indium::TextureType::e2DMultisample;
            descriptor.sampleCount = samples;
            descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
            descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite;
            auto color = device->newTexture(descriptor);
            if (!color) {
                std::printf("FAIL samples=%u: the texture was not made\n", samples);
                ++failures;
                continue;
            }
            auto resolvedDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, 16, 12, false);
            resolvedDescriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
            resolvedDescriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
            auto resolved = device->newTexture(resolvedDescriptor);
            std::vector<unsigned char> pixels(16 * 12 * 4, 0xa5);
            resolved->replaceRegion(Indium::Region::make2D(0, 0, 16, 12), 0, pixels.data(), 16 * 4);
            Indium::RenderPassDescriptor pass {};
            Indium::RenderPassColorAttachmentDescriptor attachment {};
            attachment.texture = color;
            attachment.resolveTexture = resolved;
            attachment.loadAction = Indium::LoadAction::Clear;
            attachment.storeAction = Indium::StoreAction::MultisampleResolve;
            attachment.clearColor = Indium::ClearColor(1, 0, 0, 1);
            pass.colorAttachments.push_back(attachment);
            auto command = queue->commandBuffer();
            command->renderCommandEncoder(pass)->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            resolved->getBytes(Indium::Region::make2D(0, 0, 16, 12), 0, pixels.data(), 16 * 4);
            const unsigned char expected[4] = { 255, 0, 0, 255 };
            unsigned mismatches = 0;
            for (size_t i = 0; i < pixels.size(); ++i) if (pixels[i] != expected[i % 4]) ++mismatches;
            std::printf("%s samples=%u mismatched_bytes=%u\n", mismatches ? "FAIL" : "PASS", samples, mismatches);
            if (mismatches) ++failures;
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
