#include "shaders.h"
#include "texture_nx.h"
#include "texture_ny.h"
#include "texture_nz.h"
#include "texture_px.h"
#include "texture_py.h"
#include "texture_pz.h"

// most of this code is translated directly from the MBE sample code

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <indium/indium.private.hpp>
#include <indium-kit/indium-kit.hpp>
#include <thread>

#define GLM_FORCE_SWIZZLE
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/type_aligned.hpp>

#include <iostream>
#include <condition_variable>

#include <cstring>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

//#define ENABLE_VALIDATION 1
//#define SINGLE_FRAME 1

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION 0
#endif

#ifndef SINGLE_FRAME
	#define SINGLE_FRAME 0
#endif

struct Vertex {
	glm::vec4 position = glm::vec4(0);
	glm::vec4 normal = glm::vec4(0);
};

struct Uniforms {
	glm::mat4x4 modelMatrix;
	glm::mat4x4 projectionMatrix;
	glm::mat4x4 normalMatrix;
	glm::mat4x4 modelViewProjectionMatrix;
	glm::vec4 worldCameraPosition;
};

static constexpr size_t alignToPowerOf2(size_t value, size_t powerOf2) {
	return (value + (powerOf2 - 1)) & ~(powerOf2 - 1);
};

static constexpr size_t bufferAlignment = 256;

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

class Mesh {
public:
	virtual std::shared_ptr<Indium::Buffer> vertexBuffer() const = 0;
	virtual std::shared_ptr<Indium::Buffer> indexBuffer() const = 0;
};

class SkyboxMesh: public Mesh {
private:
	static constexpr float vertices[] = {
		// + Y
		-0.5,  0.5,  0.5, 1.0,  0.0, -1.0,  0.0, 0.0,
		 0.5,  0.5,  0.5, 1.0,  0.0, -1.0,  0.0, 0.0,
		 0.5,  0.5, -0.5, 1.0,  0.0, -1.0,  0.0, 0.0,
		-0.5,  0.5, -0.5, 1.0,  0.0, -1.0,  0.0, 0.0,

		// -Y
		-0.5, -0.5, -0.5, 1.0,  0.0,  1.0,  0.0, 0.0,
		 0.5, -0.5, -0.5, 1.0,  0.0,  1.0,  0.0, 0.0,
		 0.5, -0.5,  0.5, 1.0,  0.0,  1.0,  0.0, 0.0,
		-0.5, -0.5,  0.5, 1.0,  0.0,  1.0,  0.0, 0.0,

		// +Z
		-0.5, -0.5,  0.5, 1.0,  0.0,  0.0, -1.0, 0.0,
		 0.5, -0.5,  0.5, 1.0,  0.0,  0.0, -1.0, 0.0,
		 0.5,  0.5,  0.5, 1.0,  0.0,  0.0, -1.0, 0.0,
		-0.5,  0.5,  0.5, 1.0,  0.0,  0.0, -1.0, 0.0,

		// -Z
		 0.5, -0.5, -0.5, 1.0,  0.0,  0.0,  1.0, 0.0,
		-0.5, -0.5, -0.5, 1.0,  0.0,  0.0,  1.0, 0.0,
		-0.5,  0.5, -0.5, 1.0,  0.0,  0.0,  1.0, 0.0,
		 0.5,  0.5, -0.5, 1.0,  0.0,  0.0,  1.0, 0.0,

		// -X
		-0.5, -0.5, -0.5, 1.0,  1.0,  0.0,  0.0, 0.0,
		-0.5, -0.5,  0.5, 1.0,  1.0,  0.0,  0.0, 0.0,
		-0.5,  0.5,  0.5, 1.0,  1.0,  0.0,  0.0, 0.0,
		-0.5,  0.5, -0.5, 1.0,  1.0,  0.0,  0.0, 0.0,

		// +X
		 0.5, -0.5,  0.5, 1.0, -1.0,  0.0,  0.0, 0.0,
		 0.5, -0.5, -0.5, 1.0, -1.0,  0.0,  0.0, 0.0,
		 0.5,  0.5, -0.5, 1.0, -1.0,  0.0,  0.0, 0.0,
		 0.5,  0.5,  0.5, 1.0, -1.0,  0.0,  0.0, 0.0,
	};

	static constexpr uint16_t indices[] = {
		 0,  3,  2,  2,  1,  0,
		 4,  7,  6,  6,  5,  4,
		 8, 11, 10, 10,  9,  8,
		12, 15, 14, 14, 13, 12,
		16, 19, 18, 18, 17, 16,
		20, 23, 22, 22, 21, 20,
	};

	std::shared_ptr<Indium::Buffer> _vertexBuffer;
	std::shared_ptr<Indium::Buffer> _indexBuffer;

public:
	SkyboxMesh(std::shared_ptr<Indium::Device> device) {
		_vertexBuffer = device->newBuffer(vertices, sizeof(vertices), Indium::ResourceOptions::CPUCacheModeDefaultCache);
		_indexBuffer = device->newBuffer(indices, sizeof(indices), Indium::ResourceOptions::CPUCacheModeDefaultCache);
	};

	virtual std::shared_ptr<Indium::Buffer> vertexBuffer() const override {
		return _vertexBuffer;
	};

	virtual std::shared_ptr<Indium::Buffer> indexBuffer() const override {
		return _indexBuffer;
	};
};

class TorusKnotMesh: public Mesh {
private:
	std::shared_ptr<Indium::Buffer> _vertexBuffer;
	std::shared_ptr<Indium::Buffer> _indexBuffer;
	long _p;
	long _q;
	long _segments;
	long _slices;
	float _tubeRadius;
	std::shared_ptr<Indium::Device> _device;

	void generateGeometry() {
		size_t vertexCount = (_segments + 1) * (_slices + 1);
		size_t indexCount = (_segments) * (_slices + 1) * 6;

		Vertex* vertices = new Vertex[vertexCount];
		uint16_t* indices = new uint16_t[indexCount];

		const float epsilon = 1e-4;
		const float dt = (2 * M_PI) / (_segments);
		const float du = (2 * M_PI) / _slices;
		
		size_t vi = 0;
		for (size_t i = 0; i <= _segments; ++i) {
			// calculate a point that lies on the curve
			float t0 = i * dt;
			float r0 = (2 + cosf(_q * t0)) * 0.5;
			glm::vec3 p0 = { r0 * cosf(_p * t0), r0 * sinf(_p * t0), -sinf(_q * t0) };

			// approximate the Frenet frame { T, N, B } for the curve at the current point

			float t1 = t0 + epsilon;
			float r1 = (2 + cosf(_q * t1)) * 0.5;

			// p1 is p0 advanced infinitesimally along the curve
			glm::vec3 p1 = { r1 * cosf(_p * t1), r1 * sinf(_p * t1), -sinf(_q * t1) };

			// compute approximate tangent as vector connecting p0 to p1
			glm::vec3 T = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };

			// rough approximation of normal vector
			glm::vec3 N = { p1.x + p0.x, p1.y + p0.y, p1.z + p0.z };

			// compute binormal of curve
			glm::vec3 B = glm::cross(T, N);

			// refine normal vector by Graham-Schmidt
			N = glm::cross(B, T);

			B = glm::normalize(B);
			N = glm::normalize(N);

			// generate points in a circle perpendicular to the curve at the current point
			for (size_t j = 0; j <= _slices; ++j, ++vi) {
				float u = j * du;

				// compute position of circle point
				float x = _tubeRadius * cosf(u);
				float y = _tubeRadius * sinf(u);

				glm::vec3 p2 = { x * N.x + y * B.x, x * N.y + y * B.y, x * N.z + y * B.z };

				vertices[vi].position.x = p0.x + p2.x;
				vertices[vi].position.y = p0.y + p2.y;
				vertices[vi].position.z = p0.z + p2.z;
				vertices[vi].position.w = 1;

				// compute normal of circle point
				glm::vec3 n2 = glm::normalize(p2);

				vertices[vi].normal.x = n2.x;
				vertices[vi].normal.y = n2.y;
				vertices[vi].normal.z = n2.z;
				vertices[vi].normal.w = 0;
			}
		}
		
		// generate triplets of indices to create torus triangles
		size_t i = 0;
		for (size_t vi = 0; vi < (_segments) * (_slices + 1); ++vi) {
			indices[i++] = vi;
			indices[i++] = vi + _slices + 1;
			indices[i++] = vi + _slices;

			indices[i++] = vi;
			indices[i++] = vi + 1;
			indices[i++] = vi + _slices + 1;
		}

		_vertexBuffer = _device->newBuffer(vertices, sizeof(Vertex) * vertexCount, Indium::ResourceOptions::CPUCacheModeDefaultCache);
		_indexBuffer = _device->newBuffer(indices, sizeof(uint16_t) * indexCount, Indium::ResourceOptions::CPUCacheModeDefaultCache);

		delete[] indices;
		delete[] vertices;
	};

public:
	TorusKnotMesh(std::vector<int> parameters, float radius, long segments, long slices, std::shared_ptr<Indium::Device> device):
		_p(parameters[0]),
		_q(parameters[1]),
		_segments(segments),
		_slices(slices),
		_tubeRadius(radius),
		_device(device)
	{
		generateGeometry();
	};

	virtual std::shared_ptr<Indium::Buffer> vertexBuffer() const override {
		return _vertexBuffer;
	};

	virtual std::shared_ptr<Indium::Buffer> indexBuffer() const override {
		return _indexBuffer;
	};
};

enum class RotationDirection {
	None,
	Positive,
	Negative,
};

class Renderer {
private:
	GLFWwindow* _window;
	VkSurfaceKHR _surface;
	std::shared_ptr<IndiumKit::Layer> _layer;
	glm::vec2 _viewportSize;

	std::shared_ptr<Indium::Device> _device;
	bool _useRefractionMaterial = false;
	glm::mat4x4 _sceneOrientation = glm::identity<glm::mat4x4>();

	std::shared_ptr<Indium::CommandQueue> _commandQueue;
	std::shared_ptr<Indium::Library> _library;
	std::shared_ptr<Indium::RenderPipelineState> _skyboxPipeline;
	std::shared_ptr<Indium::RenderPipelineState> _torusReflectPipeline;
	std::shared_ptr<Indium::RenderPipelineState> _torusRefractPipeline;
	std::shared_ptr<Indium::Buffer> _uniformBuffer;
	std::shared_ptr<Indium::Texture> _depthTexture;
	std::shared_ptr<Indium::Texture> _cubeTexture;
	std::shared_ptr<Indium::SamplerState> _samplerState;
	std::shared_ptr<Mesh> _skybox;
	std::shared_ptr<Mesh> _torus;

	size_t _drawableWidth = 0;
	size_t _drawableHeight = 0;

	float _roll = 0;
	float _pitch = 0;
	float _yaw = 0;

	std::shared_ptr<Indium::RenderPipelineState> makePipeline(const std::string& vertexFunctionName, const std::string& fragmentFunctionName) {
		Indium::VertexDescriptor vertexDescriptor {};
		vertexDescriptor.attributes[0].bufferIndex = 0;
		vertexDescriptor.attributes[0].offset = 0;
		vertexDescriptor.attributes[0].format = Indium::VertexFormat::Float4;

		vertexDescriptor.attributes[1].bufferIndex = 0;
		vertexDescriptor.attributes[1].offset = sizeof(glm::vec4);
		vertexDescriptor.attributes[1].format = Indium::VertexFormat::Float4;

		vertexDescriptor.layouts[0].stepFunction = Indium::VertexStepFunction::PerVertex;
		vertexDescriptor.layouts[0].stride = sizeof(Vertex);

		Indium::RenderPipelineDescriptor pipelineDescriptor {};
		pipelineDescriptor.vertexFunction = _library->newFunction(vertexFunctionName);
		pipelineDescriptor.fragmentFunction = _library->newFunction(fragmentFunctionName);
		pipelineDescriptor.vertexDescriptor = vertexDescriptor;
		pipelineDescriptor.colorAttachments.emplace_back();
		pipelineDescriptor.colorAttachments[0].pixelFormat = _layer->pixelFormat();
		pipelineDescriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;

		return _device->newRenderPipelineState(pipelineDescriptor);
	};

	void buildPipelines() {
		_skyboxPipeline = makePipeline("vertex_skybox", "fragment_cube_lookup");
		_torusReflectPipeline = makePipeline("vertex_reflect", "fragment_cube_lookup");
		_torusRefractPipeline = makePipeline("vertex_refract", "fragment_cube_lookup");
	};

	void* loadImage(const void* data, size_t length, size_t& width, size_t& height) {
		int textureWidth;
		int textureHeight;
		int channelsInFile;

		stbi_set_flip_vertically_on_load(false);

		auto textureData = stbi_load_from_memory(static_cast<const uint8_t*>(data), length, &textureWidth, &textureHeight, &channelsInFile, 4);

		width = textureWidth;
		height = textureHeight;

		return textureData;
	};

	void buildResources() {
		size_t sideLength;
		size_t ignored;

		std::array<void*, 6> images = {
			loadImage(texture_px, texture_px_len, sideLength, ignored),
			loadImage(texture_nx, texture_nx_len, sideLength, ignored),
			loadImage(texture_py, texture_py_len, sideLength, ignored),
			loadImage(texture_ny, texture_ny_len, sideLength, ignored),
			loadImage(texture_pz, texture_pz_len, sideLength, ignored),
			loadImage(texture_nz, texture_nz_len, sideLength, ignored),
		};

		auto textureDescriptor = Indium::TextureDescriptor::textureCubeDescriptor(Indium::PixelFormat::RGBA8Unorm, sideLength, true);
		_cubeTexture = _device->newTexture(textureDescriptor);

		auto region = Indium::Region::make2D(0, 0, sideLength, sideLength);
		auto bytesPerRow = 4 * static_cast<size_t>(sideLength);
		auto bytesPerImage = bytesPerRow * sideLength;

		for (size_t i = 0; i < images.size(); ++i) {
			_cubeTexture->replaceRegion(region, 0, i, images[i], bytesPerRow, bytesPerImage);
			stbi_image_free(images[i]);
		}

		// generate mipmaps
		auto cmdbuf = _commandQueue->commandBuffer();
		auto blitEncoder = cmdbuf->blitCommandEncoder();
		blitEncoder->generateMipmapsForTexture(_cubeTexture);
		blitEncoder->endEncoding();
		cmdbuf->commit();
		cmdbuf->waitUntilCompleted();

		_skybox = std::make_shared<SkyboxMesh>(_device);
		_torus = std::make_shared<TorusKnotMesh>(std::vector<int> { 3, 8 }, 0.2, 256, 32, _device);

		_uniformBuffer = _device->newBuffer(alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * 2, Indium::ResourceOptions::CPUCacheModeDefaultCache);

		Indium::SamplerDescriptor samplerDesc {};
		samplerDesc.minFilter = Indium::SamplerMinMagFilter::Nearest;
		samplerDesc.magFilter = Indium::SamplerMinMagFilter::Linear;
		samplerDesc.mipFilter = Indium::SamplerMipFilter::Linear;
		_samplerState = _device->newSamplerState(samplerDesc);
	};

	void buildDepthBuffer() {
		auto depthTexDesc = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, _drawableWidth, _drawableHeight, false);
		depthTexDesc.usage = Indium::TextureUsage::RenderTarget;
		depthTexDesc.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
		_depthTexture = _device->newTexture(depthTexDesc);
	};

	void buildMetal() {
		int width;
		int height;

		glfwGetFramebufferSize(_window, &width, &height);

		_drawableWidth = width;
		_drawableHeight = height;

		_layer = IndiumKit::Layer::make(_surface, _device, _drawableWidth, _drawableHeight);

		_library = _device->newLibrary(shaders, shaders_len);
		_commandQueue = _device->newCommandQueue();
	};

	void init() {
		buildMetal();
		buildPipelines();
		buildResources();
		buildDepthBuffer();

		auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, _viewportSize.x, _viewportSize.y, false);
		textureDescriptor.usage = Indium::TextureUsage::RenderTarget;
		textureDescriptor.resourceOptions = Indium::ResourceOptions::CPUCacheModeDefaultCache | Indium::ResourceOptions::StorageModePrivate;

		_depthTexture = _device->newTexture(textureDescriptor);

		rotate(0, RotationDirection::None, RotationDirection::None, RotationDirection::None);
	};

	void drawSkybox(std::shared_ptr<Indium::RenderCommandEncoder> commandEncoder) {
		Indium::DepthStencilDescriptor depthDescriptor {};
		depthDescriptor.depthCompareFunction = Indium::CompareFunction::Less;
		depthDescriptor.depthWriteEnabled = false;
		auto depthState = _device->newDepthStencilState(depthDescriptor);

		commandEncoder->setRenderPipelineState(_skyboxPipeline);
		commandEncoder->setDepthStencilState(depthState);
		commandEncoder->setVertexBuffer(_skybox->vertexBuffer(), 0, 0);
		commandEncoder->setVertexBuffer(_uniformBuffer, 0, 1);
		commandEncoder->setFragmentBuffer(_uniformBuffer, 0, 0);
		commandEncoder->setFragmentTexture(_cubeTexture, 0);
		commandEncoder->setFragmentSamplerState(_samplerState, 0);

		commandEncoder->drawIndexedPrimitives(Indium::PrimitiveType::Triangle, _skybox->indexBuffer()->length() / sizeof(uint16_t), Indium::IndexType::UInt16, _skybox->indexBuffer(), 0);
	};

	void drawTorus(std::shared_ptr<Indium::RenderCommandEncoder> commandEncoder) {
		Indium::DepthStencilDescriptor depthDescriptor {};
		depthDescriptor.depthCompareFunction = Indium::CompareFunction::Less;
		depthDescriptor.depthWriteEnabled = true;
		auto depthState = _device->newDepthStencilState(depthDescriptor);

		commandEncoder->setRenderPipelineState(_useRefractionMaterial ? _torusRefractPipeline : _torusReflectPipeline);
		commandEncoder->setDepthStencilState(depthState);
		commandEncoder->setVertexBuffer(_torus->vertexBuffer(), 0, 0);
		commandEncoder->setVertexBuffer(_uniformBuffer, alignToPowerOf2(sizeof(Uniforms), bufferAlignment), 1);
		commandEncoder->setFragmentBuffer(_uniformBuffer, alignToPowerOf2(sizeof(Uniforms), bufferAlignment), 0);
		commandEncoder->setFragmentTexture(_cubeTexture, 0);
		commandEncoder->setFragmentSamplerState(_samplerState, 0);

		commandEncoder->drawIndexedPrimitives(Indium::PrimitiveType::Triangle, _torus->indexBuffer()->length() / sizeof(uint16_t), Indium::IndexType::UInt16, _torus->indexBuffer(), 0);
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

	void updateUniforms(double duration) {
		static constexpr glm::vec4 cameraPosition = { 0, 0, -4, 1 };

		float aspectRatio = (float)_drawableWidth / (float)_drawableHeight;
		float verticalFOV = (aspectRatio > 1) ? 60 : 90;
		float near = 0.1;
		float far = 100;

		auto projectionMatrix = glm::perspectiveRH_NO(static_cast<float>(verticalFOV * (M_PI / 180)), aspectRatio, near, far);
		auto modelMatrix = glm::identity<glm::mat4x4>();
		auto skyboxViewMatrix = _sceneOrientation;
		auto torusViewMatrix = glm::translate(cameraPosition.xyz()) * _sceneOrientation;
		auto worldCameraPosition = glm::inverse(_sceneOrientation) * (-cameraPosition);

		Uniforms skyboxUniforms;
		skyboxUniforms.modelMatrix = modelMatrix;
		skyboxUniforms.projectionMatrix = projectionMatrix;
		skyboxUniforms.normalMatrix = glm::transpose(glm::inverse(skyboxUniforms.modelMatrix));
		skyboxUniforms.modelViewProjectionMatrix = projectionMatrix * (skyboxViewMatrix * modelMatrix);
		skyboxUniforms.worldCameraPosition = worldCameraPosition;
		std::memcpy(static_cast<char*>(_uniformBuffer->contents()), &skyboxUniforms, sizeof(skyboxUniforms));

		Uniforms torusUniforms;
		torusUniforms.modelMatrix = modelMatrix;
		torusUniforms.projectionMatrix = projectionMatrix;
		torusUniforms.normalMatrix = glm::transpose(glm::inverse(torusUniforms.modelMatrix));
		torusUniforms.modelViewProjectionMatrix = projectionMatrix * (torusViewMatrix * modelMatrix);
		torusUniforms.worldCameraPosition = worldCameraPosition;
		std::memcpy(static_cast<char*>(_uniformBuffer->contents()) + alignToPowerOf2(sizeof(Uniforms), bufferAlignment), &torusUniforms, sizeof(torusUniforms));
	};

	void render(double frameDuration) {
		auto drawable = _layer->nextDrawable();

		if (!drawable) {
			return;
		}

		updateUniforms(frameDuration);

		auto commandBuffer = _commandQueue->commandBuffer();

		// TODO: IndiumKit should provide a "current render pass descriptor"
		Indium::RenderPassDescriptor renderPassDescriptor {};

		renderPassDescriptor.colorAttachments.emplace_back();
		renderPassDescriptor.colorAttachments[0].texture = drawable->texture();
		renderPassDescriptor.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
		renderPassDescriptor.colorAttachments[0].storeAction = Indium::StoreAction::Store;
		renderPassDescriptor.colorAttachments[0].clearColor = Indium::ClearColor(1, 1, 1, 1);

		renderPassDescriptor.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor();
		renderPassDescriptor.depthAttachment->texture = _depthTexture;
		renderPassDescriptor.depthAttachment->loadAction = Indium::LoadAction::Clear;
		renderPassDescriptor.depthAttachment->storeAction = Indium::StoreAction::DontCare;
		renderPassDescriptor.depthAttachment->clearDepth = 1;

		auto renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);

		renderEncoder->setFrontFacingWinding(Indium::Winding::CounterClockwise);
		renderEncoder->setCullMode(Indium::CullMode::Back);

		drawSkybox(renderEncoder);
		drawTorus(renderEncoder);

		renderEncoder->endEncoding();

		commandBuffer->presentDrawable(drawable);

		commandBuffer->commit();
	};

	void toggleMaterial() {
		_useRefractionMaterial = !_useRefractionMaterial;
	};

	void rotate(double duration, RotationDirection rollDir, RotationDirection pitchDir, RotationDirection yawDir) {
		// (PI/6) rads/s a.k.a. 30 deg/s
		double rotationSpeed = duration * (M_PI / 6);

		if (rollDir == RotationDirection::Positive) {
			_roll += rotationSpeed;
		} else if (rollDir == RotationDirection::Negative) {
			_roll -= rotationSpeed;
		}

		if (pitchDir == RotationDirection::Positive) {
			_pitch += rotationSpeed;
		} else if (pitchDir == RotationDirection::Negative) {
			_pitch -= rotationSpeed;
		}

		if (yawDir == RotationDirection::Positive) {
			_yaw += rotationSpeed;
		} else if (yawDir == RotationDirection::Negative) {
			_yaw -= rotationSpeed;
		}

		struct Values {
			float roll;
			float pitch;
			float yaw;
		};

		Values cosine = {
			cosf(_roll),
			cosf(_pitch),
			cosf(_yaw),
		};
		Values sine = {
			sinf(_roll),
			sinf(_pitch),
			sinf(_yaw),
		};

		glm::mat3x3 roll = {
			1,           0,           0,
			0, cosine.roll,   sine.roll,
			0,  -sine.roll, cosine.roll,
		};

		glm::mat3x3 pitch = {
			cosine.pitch, 0,  -sine.pitch,
			           0, 1,            0,
			  sine.pitch, 0, cosine.pitch,
		};

		glm::mat3x3 yaw = {
			cosine.yaw,   sine.yaw, 0,
			 -sine.yaw, cosine.yaw, 0,
			         0,          0, 1,
		};

		glm::mat3x3 dcm = roll * pitch * yaw;

		_sceneOrientation = glm::mat4x4 {
			glm::vec4(dcm[0], 0),
			glm::vec4(dcm[1], 0),
			glm::vec4(dcm[2], 0),
			glm::vec4 { 0, 0, 0, 1 },
			/*
			dcm[1][0], dcm[2][0], dcm[0][0], 0,
			dcm[1][1], dcm[2][1], dcm[0][1], 0,
			dcm[1][2], dcm[2][2], dcm[0][2], 0,
			        0,         0,         0, 1,
			*/
		};
	};
};

static Renderer* globalRenderer = nullptr;

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button != GLFW_MOUSE_BUTTON_LEFT) {
		return;
	}

	if (action != GLFW_RELEASE) {
		return;
	}

	if (!globalRenderer) {
		return;
	}

	globalRenderer->toggleMaterial();
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

	glfwSetMouseButtonCallback(window, mouseButtonCallback);

	{
		Renderer renderer(device, window, surface, viewportSize);

		globalRenderer = &renderer;

#if SINGLE_FRAME
		renderer.render(frameDuration);
#endif

		while (true) {
			if (glfwWindowShouldClose(window)) {
				break;
			}
			glfwPollEvents();

			RotationDirection roll = RotationDirection::None;
			RotationDirection pitch = RotationDirection::None;
			RotationDirection yaw = RotationDirection::None;

			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
				roll = RotationDirection::Negative;
			} else if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
				roll = RotationDirection::Positive;
			}

			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
				pitch = RotationDirection::Positive;
			} else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
				pitch = RotationDirection::Negative;
			}

			if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
				yaw = RotationDirection::Negative;
			} else if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
				yaw = RotationDirection::Positive;
			}

			if (roll != RotationDirection::None || pitch != RotationDirection::None || yaw != RotationDirection::None) {
				renderer.rotate(frameDuration, roll, pitch, yaw);
			}

#if !SINGLE_FRAME
			renderer.render(frameDuration);
#endif
		}

		globalRenderer = nullptr;
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
