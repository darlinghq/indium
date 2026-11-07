#include <indium/indium.hpp>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
#include "vertex-shader.h"
#include "fragment-shader.h"

int main() {
    Indium::init(nullptr, 0, false);
    auto device = Indium::createSystemDefaultDevice();
    if (!device) return 2;
    std::atomic<bool> running{true};
    std::thread polling([&] { while (running) device->pollEvents(UINT64_MAX); });
    unsigned failures = 0;
    try {
        for (size_t samples : {1u, 4u}) {
            auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(
                Indium::PixelFormat::RGBA8Unorm, 16, 12, false);
            descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
            auto result = device->newTexture(descriptor);
            std::vector<unsigned char> poison(16*12*4, 0xa5);
            result->replaceRegion(Indium::Region::make2D(0,0,16,12),0,poison.data(),16*4);
            auto color = result;
            if (samples > 1) {
                descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
                descriptor.textureType = Indium::TextureType::e2DMultisample;
                descriptor.sampleCount = samples;
                color = device->newTexture(descriptor);
            }
            Indium::LibraryReflection vertexReflection, fragmentReflection;
            vertexReflection.functions["main"].functionType = Indium::FunctionType::Vertex;
            fragmentReflection.functions["main"].functionType = Indium::FunctionType::Fragment;
            auto vertex = device->newLibrary(vertex_shader, vertex_shader_len, vertexReflection);
            auto fragment = device->newLibrary(fragment_shader, fragment_shader_len, fragmentReflection);
            Indium::RenderPipelineDescriptor pipelineDescriptor {};
            pipelineDescriptor.vertexFunction = vertex->newFunction("main");
            pipelineDescriptor.fragmentFunction = fragment->newFunction("main");
            pipelineDescriptor.rasterSampleCount = samples;
            Indium::RenderPipelineColorAttachmentDescriptor pipelineColor {};
            pipelineColor.pixelFormat = descriptor.pixelFormat;
            pipelineDescriptor.colorAttachments.push_back(pipelineColor);
            auto pipeline = device->newRenderPipelineState(pipelineDescriptor);
            for (unsigned operation : {0u, 1u, 2u}) {
                unsigned channel = operation == 0 ? 0 : 2;
                Indium::RenderPassDescriptor pass{};
                Indium::RenderPassColorAttachmentDescriptor attachment{};
                attachment.texture = color;
                attachment.loadAction = Indium::LoadAction::Clear;
                attachment.storeAction = samples > 1 ? Indium::StoreAction::MultisampleResolve : Indium::StoreAction::Store;
                if (samples > 1) attachment.resolveTexture = result;
                attachment.clearColor = Indium::ClearColor(operation == 2 || channel == 0,0,operation != 2 && channel == 2,1);
                pass.colorAttachments.push_back(attachment);
                auto command = device->newCommandQueue()->commandBuffer();
                auto encoder = command->renderCommandEncoder(pass);
                if (operation == 2) {
                    encoder->setRenderPipelineState(pipeline);
                    encoder->drawPrimitives(Indium::PrimitiveType::Triangle,0,3);
                }
                encoder->endEncoding(); command->commit(); command->waitUntilCompleted();
                std::vector<unsigned char> pixels(poison.size(),0xa5);
                result->getBytes(Indium::Region::make2D(0,0,16,12),0,pixels.data(),16*4);
                unsigned mismatches = 0;
                for (size_t i=0;i<pixels.size();++i) {
                    unsigned char expected = i%4 == channel || i%4 == 3 ? 255 : 0;
                    if (pixels[i] != expected) ++mismatches;
                }
                std::printf("%s samples=%zu operation=%u mismatched_bytes=%u\n", mismatches ? "FAIL" : "PASS",samples,operation,mismatches);
                if (mismatches) ++failures;
            }
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr,"FAIL exception: %s\n",error.what()); ++failures;
    }
    running = false; device->wakeupEventLoop(); polling.join(); device.reset();
    Indium::finit();
    std::printf("RESULT failures=%u\n",failures);
    return failures ? 1 : 0;
}
