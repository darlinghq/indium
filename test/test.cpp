#include "AAPLShaders.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <indium/indium.private.hpp>
#include <indium-kit/indium-kit.hpp>
#include <thread>

#include <glm/glm.hpp>

#include <iostream>
#include <condition_variable>

//#define ENABLE_VALIDATION 1

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION 0
#endif

struct Vertex {
	glm::vec2 position;
	alignas(16) glm::vec4 color;
};

static const Vertex vertices[] = {
	{ {  250,  -250 }, { 1, 0, 0, 1 } },
	{ { -250,  -250 }, { 0, 1, 0, 1 } },
	{ {    0,   250 }, { 0, 0, 1, 1 } },
};

int main(int argc, char** argv) {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	uint32_t extensionCount = 0;
	auto extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

	Indium::init(extensions, extensionCount, ENABLE_VALIDATION);

	auto device = Indium::createSystemDefaultDevice();

	bool keepPollingDevice = true;

	std::thread devicePollingThread([device, &keepPollingDevice]() {
		while (keepPollingDevice) {
			device->pollEvents(UINT64_MAX);
		}
	});

	glm::uvec2 viewportSize { 800, 600 };

	auto window = glfwCreateWindow(viewportSize.x, viewportSize.y, "Indium Test", nullptr, nullptr);

	VkSurfaceKHR surface;

	if (glfwCreateWindowSurface(Indium::globalInstance, window, nullptr, &surface) != VK_SUCCESS) {
		// TODO
		abort();
	}

	{
		int width;
		int height;

		glfwGetFramebufferSize(window, &width, &height);

		auto layer = IndiumKit::Layer::make(surface, device, width, height);

		auto library = device->newLibrary(shader, shader_len);
		auto vertexFunction = library->newFunction("vertexShader");
		auto fragFunction = library->newFunction("fragmentShader");

		Indium::RenderPipelineDescriptor psoDescriptor {};
		psoDescriptor.vertexFunction = vertexFunction;
		psoDescriptor.fragmentFunction = fragFunction;
		psoDescriptor.colorAttachments.emplace_back();
		psoDescriptor.colorAttachments[0].pixelFormat = layer->pixelFormat();

		auto pso = device->newRenderPipelineState(psoDescriptor);
		auto commandQueue = device->newCommandQueue();

		auto render = [&]() {
			auto commandBuffer = commandQueue->commandBuffer();

			// TODO: IndiumKit should provide a "current render pass descriptor"
			Indium::RenderPassDescriptor renderPassDescriptor {};

			auto drawable = layer->nextDrawable();

			renderPassDescriptor.colorAttachments.emplace_back();
			renderPassDescriptor.colorAttachments[0].texture = drawable->texture();
			renderPassDescriptor.colorAttachments[0].storeAction = Indium::StoreAction::Store;
			renderPassDescriptor.colorAttachments[0].loadAction = Indium::LoadAction::Clear;

			auto renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);

			renderEncoder->setViewport(Indium::Viewport { 0, 0, static_cast<double>(viewportSize.x), static_cast<double>(viewportSize.y), 0, 1 });

			renderEncoder->setRenderPipelineState(pso);

			renderEncoder->setVertexBytes(&vertices, sizeof(vertices), 0);

			renderEncoder->setVertexBytes(&viewportSize, sizeof(viewportSize), 1);

			renderEncoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, 3);

			renderEncoder->endEncoding();

			commandBuffer->presentDrawable(drawable);

			commandBuffer->commit();
		};

		// putting it in the loop results in high CPU usage because we currently don't have a way to do vsync
		render();

		while (!glfwWindowShouldClose(window)) {
			glfwWaitEvents();
		}
	}

	glfwDestroyWindow(window);

	glfwTerminate();

	keepPollingDevice = false;
	device->wakeupEventLoop();
	devicePollingThread.join();

	device = nullptr;
	Indium::finit();

	return 0;
};
