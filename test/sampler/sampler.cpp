#include "AAPLShaders.h"
#include "Image.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <indium/indium.private.hpp>
#include <indium-kit/indium-kit.hpp>
#include <thread>

#include <glm/glm.hpp>

#include <iostream>
#include <condition_variable>

#include <cstdlib>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

struct Vertex {
	glm::vec2 position;
	glm::vec2 textureCoordinate;
};

static const Vertex vertices[] = {
	{ {  250, -250 }, { 1.f, 1.f } },
	{ { -250, -250 }, { 0.f, 1.f } },
	{ { -250,  250 }, { 0.f, 0.f } },

	{ {  250, -250 }, { 1.f, 1.f } },
	{ { -250,  250 }, { 0.f, 0.f } },
	{ {  250,  250 }, { 1.f, 0.f } },
};

// translated (roughly) from AAPLImage.m from Apple's example, with some modifications to load raw data instead of a file
bool loadTGAFromData(const void* data, size_t length, void*& outputData, size_t& outputDataSize, size_t& imageWidth, size_t& imageHeight) {
	// Structure fitting the layout of a TGA header containing image metadata.
	typedef struct __attribute__ ((packed)) TGAHeader {
		uint8_t  IDSize;         // Size of ID info following header
		uint8_t  colorMapType;   // Whether this is a paletted image
		uint8_t  imageType;      // type of image 0=none, 1=indexed, 2=rgb, 3=grey, +8=rle packed
		
		int16_t  colorMapStart;  // Offset to color map in palette
		int16_t  colorMapLength; // Number of colors in palette
		uint8_t  colorMapBpp;    // Number of bits per palette entry
		
		uint16_t xOrigin;        // X Origin pixel of lower left corner if tile of larger image
		uint16_t yOrigin;        // Y Origin pixel of lower left corner if tile of larger image
		uint16_t width;          // Width in pixels
		uint16_t height;         // Height in pixels
		uint8_t  bitsPerPixel;   // Bits per pixel 8,16,24,32
		union {
			struct {
				uint8_t bitsPerAlpha : 4;
				uint8_t topOrigin    : 1;
				uint8_t rightOrigin  : 1;
				uint8_t reserved     : 2;
			};
			uint8_t descriptor;
		};
	} TGAHeader;

	const TGAHeader* tgaInfo = reinterpret_cast<const TGAHeader*>(data);

	if (tgaInfo->imageType != 2) {
		std::cerr << "This image loader only supports non-compressed BGR(A) TGA files" << std::endl;
		return false;
	}

	if (tgaInfo->colorMapType) {
		std::cerr << "This image loader doesn't support TGA files with a colormap" << std::endl;
		return false;
	}

	if (tgaInfo->xOrigin || tgaInfo->yOrigin) {
		std::cerr << "This image loader doesn't support TGA files with a non-zero origin" << std::endl;
		return false;
	}

	size_t srcBytesPerPixel;
	if (tgaInfo->bitsPerPixel == 32) {
		srcBytesPerPixel = 4;

		if (tgaInfo->bitsPerAlpha != 8) {
			std::cerr << "This image loader only supports 32-bit TGA files with 8 bits of alpha" << std::endl;
			return false;
		}
	} else if (tgaInfo->bitsPerPixel == 24) {
		srcBytesPerPixel = 3;

		if (tgaInfo->bitsPerAlpha != 0) {
			std::cerr << "This image loader only supports 24-bit TGA files with no alpha" << std::endl;
			return false;
		}
	} else {
		std::cerr << "This image loader only supports 24-bit and 32-bit TGA files" << std::endl;
		return false;
	}

	imageWidth = tgaInfo->width;
	imageHeight = tgaInfo->height;

	// The image data is stored as 32-bits per pixel BGRA data.
	outputDataSize = imageWidth * imageHeight * 4;

	// Metal will not understand an image with 24-bit BGR format so the pixels
	// are converted to a 32-bit BGRA format that Metal does understand
	// (MTLPixelFormatBGRA8Unorm)

	outputData = malloc(outputDataSize);

	// TGA spec says the image data is immediately after the header and the ID so set
	//   the pointer to file's start + size of the header + size of the ID
	// Initialize a source pointer with the source image data that's in BGR form
	const uint8_t* srcImageData = reinterpret_cast<const uint8_t*>(data) + sizeof(TGAHeader) + tgaInfo->IDSize;

	// Initialize a destination pointer to which you'll store the converted BGRA
	// image data
	uint8_t* dstImageData = reinterpret_cast<uint8_t*>(outputData);

	// For every row of the image
	for(size_t y = 0; y < imageHeight; y++) {
		// If bit 5 of the descriptor is not set, flip vertically
		// to transform the data to Metal's top-left texture origin
		size_t srcRow = (tgaInfo->topOrigin) ? y : imageHeight - 1 - y;

		// For every column of the current row
		for(size_t x = 0; x < imageWidth; x++) {
			// If bit 4 of the descriptor is set, flip horizontally
			// to transform the data to Metal's top-left texture origin
			size_t srcColumn = (tgaInfo->rightOrigin) ? imageWidth - 1 - x : x;

			// Calculate the index for the first byte of the pixel you're
			// converting in both the source and destination images
			size_t srcPixelIndex = srcBytesPerPixel * (srcRow * imageWidth + srcColumn);
			size_t dstPixelIndex = 4 * (y * imageWidth + x);

			// Copy BGR channels from the source to the destination
			// Set the alpha channel of the destination pixel to 255
			dstImageData[dstPixelIndex + 0] = srcImageData[srcPixelIndex + 0];
			dstImageData[dstPixelIndex + 1] = srcImageData[srcPixelIndex + 1];
			dstImageData[dstPixelIndex + 2] = srcImageData[srcPixelIndex + 2];

			if (tgaInfo->bitsPerPixel == 32) {
				dstImageData[dstPixelIndex + 3] =  srcImageData[srcPixelIndex + 3];
			} else {
				dstImageData[dstPixelIndex + 3] = 255;
			}
		}
	}

	return true;
};

// equivalent to AAPLRenderer in Apple's example
class Renderer {
private:
	GLFWwindow* _window;
	VkSurfaceKHR _surface;
	std::shared_ptr<IndiumKit::Layer> _layer;

	std::shared_ptr<Indium::Device> _device;
	std::shared_ptr<Indium::RenderPipelineState> _pipelineState;
	std::shared_ptr<Indium::CommandQueue> _commandQueue;
	std::shared_ptr<Indium::Texture> _texture;
	std::shared_ptr<Indium::Buffer> _vertices;
	size_t _numVertices;
	glm::uvec2 _viewportSize;

	// equivalent of `initWithMetalKitView` in Apple's example
	void init() {
		int width;
		int height;

		glfwGetFramebufferSize(_window, &width, &height);

		_layer = IndiumKit::Layer::make(_surface, _device, width, height);

		void* textureData;
		size_t textureDataSize;
		size_t textureWidth;
		size_t textureHeight;

		if (!loadTGAFromData(image, image_len, textureData, textureDataSize, textureWidth, textureHeight)) {
			throw std::runtime_error("Failed to load TGA image");
		}

		Indium::TextureDescriptor textureDescriptor {};

		textureDescriptor.pixelFormat = Indium::PixelFormat::BGRA8Unorm;
		textureDescriptor.width = textureWidth;
		textureDescriptor.height = textureHeight;

		_texture = _device->newTexture(textureDescriptor);

		_texture->replaceRegion(Indium::Region {
			Indium::Origin { 0, 0, 0 },
			Indium::Size { textureWidth, textureHeight, 1 },
		}, 0, textureData, 4 * textureWidth);

		free(textureData);

		_vertices = _device->newBuffer(&vertices[0], sizeof(vertices), Indium::ResourceOptions::StorageModeShared);
		_numVertices = sizeof(vertices) / sizeof(Vertex);

		auto library = _device->newLibrary(shader, shader_len);
		auto vertexFunction = library->newFunction("vertexShader");
		auto fragFunction = library->newFunction("samplingShader");

		Indium::RenderPipelineDescriptor psoDescriptor {};
		psoDescriptor.vertexFunction = vertexFunction;
		psoDescriptor.fragmentFunction = fragFunction;
		psoDescriptor.colorAttachments.emplace_back();
		psoDescriptor.colorAttachments[0].pixelFormat = _layer->pixelFormat();

		_pipelineState = _device->newRenderPipelineState(psoDescriptor);
		_commandQueue = _device->newCommandQueue();
	};

public:
	Renderer(std::shared_ptr<Indium::Device> device, GLFWwindow* window, VkSurfaceKHR surface, glm::uvec2 viewportSize):
		_device(device),
		_window(window),
		_surface(surface),
		_viewportSize(viewportSize)
	{
		init();
	};

	// equivalent of `drawInMTKView` in Apple's example
	void render() {
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

		auto renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);

		renderEncoder->setViewport(Indium::Viewport { 0, 0, static_cast<double>(_viewportSize.x), static_cast<double>(_viewportSize.y), -1, 1 });

		renderEncoder->setRenderPipelineState(_pipelineState);

		renderEncoder->setVertexBuffer(_vertices, 0, 0);

		renderEncoder->setVertexBytes(&_viewportSize, sizeof(_viewportSize), 1);

		renderEncoder->setFragmentTexture(_texture, 0);

		renderEncoder->drawPrimitives(Indium::PrimitiveType::Triangle, 0, _numVertices);

		renderEncoder->endEncoding();

		commandBuffer->presentDrawable(drawable);

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

	{
		Renderer renderer(device, window, surface, viewportSize);

		while (true) {
			if (glfwWindowShouldClose(window)) {
				break;
			}
			glfwPollEvents();
			renderer.render();
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
