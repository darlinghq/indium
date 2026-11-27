// A fragment function whose reflection names a sample count specialization constant reads the render
// pipeline's raster sample count there, and keeps its function constants beside it. The fragment shader writes
// 40 times the count to red and a function constant to green; at 4x the resolve averages identical samples.
// A reflection that gives the constant to a vertex function, or the id of a function constant, is refused.
// Needs a Vulkan device.
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
    const auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    try {
        Indium::FunctionConstantInfo shade {};
        shade.name = "shade";
        shade.index = 0;
        shade.type = Indium::FunctionConstantType::UInt;
        shade.specializationID = 3;
        shade.definedSpecializationID = 3 + 65536;

        // refused reflections
        {
            Indium::LibraryReflection reflection;
            reflection.functions["main"].functionType = Indium::FunctionType::Vertex;
            reflection.functions["main"].numSamplesSpecializationID = 262147;
            bool refused = false;
            try { refused = !device->newLibrary(vertex_shader, vertex_shader_len, reflection); } catch (const std::exception&) { refused = true; }
            check(refused, "a vertex function with a sample count constant is refused");
        }
        {
            Indium::LibraryReflection reflection;
            reflection.functions["main"].functionType = Indium::FunctionType::Fragment;
            reflection.functions["main"].functionConstants.push_back(shade);
            reflection.functions["main"].numSamplesSpecializationID = 3;
            bool refused = false;
            try { refused = !device->newLibrary(fragment_shader, fragment_shader_len, reflection); } catch (const std::exception&) { refused = true; }
            check(refused, "a sample count constant with a function constant's id is refused");
        }

        for (size_t samples : {1u, 4u}) {
            for (bool reflected : {true, false}) {
                auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(
                    Indium::PixelFormat::RGBA8Unorm, 16, 12, false);
                descriptor.resourceOptions = Indium::ResourceOptions::StorageModeShared;
                descriptor.usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
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
                auto& fragmentInfo = fragmentReflection.functions["main"];
                fragmentInfo.functionType = Indium::FunctionType::Fragment;
                fragmentInfo.functionConstants.push_back(shade);
                if (reflected) fragmentInfo.numSamplesSpecializationID = 262147;
                auto vertex = device->newLibrary(vertex_shader, vertex_shader_len, vertexReflection);
                auto fragment = device->newLibrary(fragment_shader, fragment_shader_len, fragmentReflection);
                Indium::FunctionConstantValues values;
                values.setValue(0, Indium::FunctionConstantType::UInt, 7);
                Indium::RenderPipelineDescriptor pipelineDescriptor {};
                pipelineDescriptor.vertexFunction = vertex->newFunction("main");
                pipelineDescriptor.fragmentFunction = fragment->newFunction("main", values);
                pipelineDescriptor.rasterSampleCount = samples;
                Indium::RenderPipelineColorAttachmentDescriptor pipelineColor {};
                pipelineColor.pixelFormat = descriptor.pixelFormat;
                pipelineDescriptor.colorAttachments.push_back(pipelineColor);
                auto pipeline = device->newRenderPipelineState(pipelineDescriptor);
                Indium::RenderPassDescriptor pass{};
                Indium::RenderPassColorAttachmentDescriptor attachment{};
                attachment.texture = color;
                attachment.loadAction = Indium::LoadAction::Clear;
                attachment.storeAction = samples > 1 ? Indium::StoreAction::MultisampleResolve : Indium::StoreAction::Store;
                if (samples > 1) attachment.resolveTexture = result;
                attachment.clearColor = Indium::ClearColor(0,0,1,1);
                pass.colorAttachments.push_back(attachment);
                auto command = device->newCommandQueue()->commandBuffer();
                auto encoder = command->renderCommandEncoder(pass);
                encoder->setRenderPipelineState(pipeline);
                encoder->drawPrimitives(Indium::PrimitiveType::Triangle,0,3);
                encoder->endEncoding(); command->commit(); command->waitUntilCompleted();
                std::vector<unsigned char> pixels(poison.size(),0xa5);
                result->getBytes(Indium::Region::make2D(0,0,16,12),0,pixels.data(),16*4);
                // without the reflection the constant keeps its default of 1
                const unsigned char red = static_cast<unsigned char>(40 * (reflected ? samples : 1));
                const unsigned char expected[4] = { red, 7, 0, 255 };
                unsigned mismatches = 0;
                for (size_t i = 0; i < pixels.size(); ++i) {
                    if (pixels[i] != expected[i % 4]) ++mismatches;
                }
                std::printf("%s samples=%zu reflected=%d first=%u,%u,%u,%u mismatched_bytes=%u\n", mismatches ? "FAIL" : "PASS",
                    samples, reflected, pixels[0], pixels[1], pixels[2], pixels[3], mismatches);
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
