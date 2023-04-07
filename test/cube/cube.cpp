#include "shaders.h"

// most of this code is translated directly from the MBE sample code

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <indium/indium.private.hpp>
#include <indium-kit/indium-kit.hpp>
#include <thread>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <iostream>
#include <condition_variable>

#include <cstring>

#include <cstdlib>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t inflightBufferCount = 3;

struct Vertex {
	alignas(16) glm::vec4 position;
	alignas(16) glm::vec4 color;
};

struct Uniforms {
	glm::mat4x4 modelViewProjectionMatrix;
};

static constexpr size_t alignToPowerOf2(size_t value, size_t powerOf2) {
	return (value + (powerOf2 - 1)) & ~(powerOf2 - 1);
};

static constexpr size_t bufferAlignment = 256;

static const Vertex vertices[] = {
	{ /* position */ { -1,  1,  1, 1 }, /* color */ { 0, 1, 1, 1 } },
	{ /* position */ { -1, -1,  1, 1 }, /* color */ { 0, 0, 1, 1 } },
	{ /* position */ {  1, -1,  1, 1 }, /* color */ { 1, 0, 1, 1 } },
	{ /* position */ {  1,  1,  1, 1 }, /* color */ { 1, 1, 1, 1 } },
	{ /* position */ { -1,  1, -1, 1 }, /* color */ { 0, 1, 0, 1 } },
	{ /* position */ { -1, -1, -1, 1 }, /* color */ { 0, 0, 0, 1 } },
	{ /* position */ {  1, -1, -1, 1 }, /* color */ { 1, 0, 0, 1 } },
	{ /* position */ {  1,  1, -1, 1 }, /* color */ { 1, 1, 0, 1 } }
};

static const uint16_t indices[] = {
	3, 2, 6, 6, 7, 3,
	4, 5, 1, 1, 0, 4,
	4, 0, 3, 3, 7, 4,
	1, 5, 6, 6, 2, 1,
	0, 1, 2, 2, 3, 0,
	7, 6, 5, 5, 4, 7,
};

class Semaphore {
private:
	std::condition_variable _condvar;
	std::mutex _mutex;
	size_t _counter;

public:
	Semaphore(size_t count = 0):
		_counter(count)
		{};

	void down() {
		std::unique_lock lock(_mutex);
		while (_counter == 0) {
			_condvar.wait(lock);
		}
		--_counter;
	};

	void up() {
		{
			std::unique_lock lock(_mutex);
			++_counter;
		};
		_condvar.notify_one();
	};
};

class Renderer {
private:
	GLFWwindow* _window;
	VkSurfaceKHR _surface;
	std::shared_ptr<IndiumKit::Layer> _layer;
	glm::vec2 _viewportSize;

	std::shared_ptr<Indium::Device> _device;
	std::shared_ptr<Indium::Buffer> _vertexBuffer;
	std::shared_ptr<Indium::Buffer> _indexBuffer;
	std::shared_ptr<Indium::Buffer> _uniformBuffer;
	std::shared_ptr<Indium::CommandQueue> _commandQueue;
	std::shared_ptr<Indium::RenderPipelineState> _pipelineState;
	std::shared_ptr<Indium::DepthStencilState> _depthStencilState;
	Semaphore _displaySemaphore = Semaphore(inflightBufferCount);
	size_t _bufferIndex = 0;
	float _rotationX = 0;
	float _rotationY = 0;
	float _time = 0;

	std::shared_ptr<Indium::Texture> _depthTexture;

	void makePipeline() {
		int width;
		int height;

		glfwGetFramebufferSize(_window, &width, &height);

		_layer = IndiumKit::Layer::make(_surface, _device, width, height);

		auto library = _device->newLibrary(shaders, shaders_len);

		Indium::RenderPipelineDescriptor pipelineDescriptor {};
		pipelineDescriptor.vertexFunction = library->newFunction("vertex_project");
		pipelineDescriptor.fragmentFunction = library->newFunction("fragment_flatcolor");
		pipelineDescriptor.colorAttachments.emplace_back();
		pipelineDescriptor.colorAttachments[0].pixelFormat = _layer->pixelFormat();
		pipelineDescriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;

		Indium::DepthStencilDescriptor depthStencilDescriptor {};
		depthStencilDescriptor.depthCompareFunction = Indium::CompareFunction::Less;
		depthStencilDescriptor.depthWriteEnabled = true;
		_depthStencilState = _device->newDepthStencilState(depthStencilDescriptor);

		_pipelineState = _device->newRenderPipelineState(pipelineDescriptor);
		_commandQueue = _device->newCommandQueue();
	};

	void makeBuffers() {
		_vertexBuffer = _device->newBuffer(vertices, sizeof(vertices), Indium::ResourceOptions::CPUCacheModeDefaultCache);

		_indexBuffer = _device->newBuffer(indices, sizeof(indices), Indium::ResourceOptions::CPUCacheModeDefaultCache);

		_uniformBuffer = _device->newBuffer(alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * inflightBufferCount, Indium::ResourceOptions::CPUCacheModeDefaultCache);
	};

	void init() {
		makePipeline();
		makeBuffers();

		Indium::TextureDescriptor textureDescriptor {};
		textureDescriptor.textureType = Indium::TextureType::e2D;
		textureDescriptor.pixelFormat = Indium::PixelFormat::Depth32Float;
		textureDescriptor.width = _viewportSize.x;
		textureDescriptor.height = _viewportSize.y;
		textureDescriptor.usage = Indium::TextureUsage::RenderTarget;
		textureDescriptor.resourceOptions = Indium::ResourceOptions::CPUCacheModeDefaultCache | Indium::ResourceOptions::StorageModePrivate;

		_depthTexture = _device->newTexture(textureDescriptor);
	};

	void semaphoreDown() {};

public:
	Renderer(std::shared_ptr<Indium::Device> device, GLFWwindow* window, VkSurfaceKHR surface, glm::uvec2 viewportSize):
		_device(device),
		_window(window),
		_surface(surface),
		_viewportSize(viewportSize)
	{
		init();
	};

	void updateUniforms(double duration) {
		_time += duration;
		_rotationX += duration * (M_PI / 2);
		_rotationY += duration * (M_PI / 3);
		float scaleFactor = sinf(5 * _time) * 0.25 + 1;
		glm::vec3 xAxis = { 1, 0, 0 };
		glm::vec3 yAxis = { 0, 1, 0 };
		auto xRot = glm::rotate_slow(glm::identity<glm::mat4x4>(), _rotationX, xAxis);
		auto yRot = glm::rotate_slow(glm::identity<glm::mat4x4>(), _rotationY, yAxis);
		auto scale = glm::scale(glm::identity<glm::mat4x4>(), glm::vec3(scaleFactor));
		auto modelMatrix = (xRot * yRot) * scale;

		glm::vec3 cameraTranslation = { 0, 0, -5 };
		auto viewMatrix = glm::translate(glm::identity<glm::mat4x4>(), cameraTranslation);

		float aspect = _viewportSize.x / _viewportSize.y;
		float fov = (2 * M_PI) / 5;
		float near = 1;
		float far = 100;
		auto projectionMatrix = glm::perspectiveRH_NO(fov, aspect, near, far);

		Uniforms uniforms;
		uniforms.modelViewProjectionMatrix = projectionMatrix * (viewMatrix * modelMatrix);

		size_t uniformBufferOffset = alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * _bufferIndex;
		std::memcpy(static_cast<char*>(_uniformBuffer->contents()) + uniformBufferOffset, &uniforms, sizeof(uniforms));
	};

	void render(double frameDuration) {
		_displaySemaphore.down();

		updateUniforms(frameDuration);

		auto commandBuffer = _commandQueue->commandBuffer();

		// TODO: IndiumKit should provide a "current render pass descriptor"
		Indium::RenderPassDescriptor renderPassDescriptor {};
		auto drawable = _layer->nextDrawable();

		if (!drawable) {
			return;
		}

		renderPassDescriptor.colorAttachments.emplace_back();
		renderPassDescriptor.colorAttachments[0].texture = drawable->texture();
		renderPassDescriptor.colorAttachments[0].storeAction = Indium::StoreAction::Store;
		renderPassDescriptor.colorAttachments[0].loadAction = Indium::LoadAction::Clear;

		renderPassDescriptor.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor();
		renderPassDescriptor.depthAttachment->texture = _depthTexture;
		renderPassDescriptor.depthAttachment->clearDepth = 1.0;
		renderPassDescriptor.depthAttachment->loadAction = Indium::LoadAction::Clear;
		renderPassDescriptor.depthAttachment->storeAction = Indium::StoreAction::DontCare;

		auto renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);

		renderEncoder->setRenderPipelineState(_pipelineState);
		renderEncoder->setDepthStencilState(_depthStencilState);
		renderEncoder->setFrontFacingWinding(Indium::Winding::CounterClockwise);
		renderEncoder->setCullMode(Indium::CullMode::Back);

		size_t uniformBufferOffset = alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * _bufferIndex;

		renderEncoder->setVertexBuffer(_vertexBuffer, 0, 0);
		renderEncoder->setVertexBuffer(_uniformBuffer, uniformBufferOffset, 1);

		renderEncoder->drawIndexedPrimitives(Indium::PrimitiveType::Triangle, _indexBuffer->length() / sizeof(uint16_t), Indium::IndexType::UInt16, _indexBuffer, 0);

		renderEncoder->endEncoding();

		commandBuffer->presentDrawable(drawable);

		commandBuffer->addCompletedHandler([&](std::shared_ptr<Indium::CommandBuffer> cmdBuf) {
			_bufferIndex = (_bufferIndex + 1) % inflightBufferCount;
			_displaySemaphore.up();
		});

		commandBuffer->commit();
	};
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

	// FIXME: assumes the window is on the primary monitor. GLFW provides no way to fetch the current monitor for non-fullscreen windows.
	auto videoMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	double frameDuration = 1.0 / videoMode->refreshRate;

	{
		Renderer renderer(device, window, surface, viewportSize);

		while (true) {
			if (glfwWindowShouldClose(window)) {
				break;
			}
			glfwPollEvents();
			renderer.render(frameDuration);
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
