#include "shaders.h"
#include "spot.h"
#include "spot_texture.h"

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

#include <cstdlib>

#ifndef ENABLE_VALIDATION
	#define ENABLE_VALIDATION (!!getenv("INDIUM_TEST_VALIDATION"))
#endif

static constexpr size_t inflightBufferCount = 3;

struct Vertex {
	glm::vec4 position = glm::vec4(0);
	glm::vec4 normal = glm::vec4(0);
	glm::vec2 texCoords = glm::vec2(0);
};

static_assert(sizeof(Vertex) == 40);

struct Uniforms {
	glm::mat4x4 modelViewProjectionMatrix;
	glm::mat4x4 modelViewMatrix;

	// necessary because GLM's mat3x3 definition differs from Apple's matrix_float3x3 definition
	// GLM uses an array of 3 `glm::vec3`s (with no additional internal padding), while Apple's matrix_float3x3
	// is an array of 3 4-component vectors, with the fourth component of each ignored.
	float normalMatrix[3][4];
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

struct DataDeleter {
	void operator()(void* data) const {
		free(data);
	};
};

struct OBJGroup {
	std::string name;
	std::shared_ptr<void> vertexData;
	size_t vertexDataLength = 0;
	std::shared_ptr<void> indexData;
	size_t indexDataLength = 0;
};

struct FaceVertex {
	uint16_t vi = 0;
	uint16_t ti = 0;
	uint16_t ni = 0;
};

template<>
struct std::hash<FaceVertex> {
	size_t operator()(const FaceVertex& vertex) const {
		size_t result = std::hash<uint16_t>()(vertex.vi);
		result = ((result << 1) ^ std::hash<uint16_t>()(vertex.ti)) >> 1;
		result = ((result << 1) ^ std::hash<uint16_t>()(vertex.ni)) >> 1;
		return result;
	};
};

static bool operator==(const FaceVertex& vertex1, const FaceVertex& vertex2) {
	return (vertex1.vi == vertex2.vi) && (vertex1.ti == vertex2.ti) && (vertex1.ni == vertex2.ni);
};

class OBJModel {
private:
	std::vector<std::shared_ptr<OBJGroup>> _groups;
	std::vector<glm::vec4> _vertices;
	std::vector<glm::vec4> _normals;
	std::vector<glm::vec2> _texCoords;
	std::vector<Vertex> _groupVertices;
	std::vector<uint16_t> _groupIndices;
	std::unordered_map<FaceVertex, uint16_t> _vertexToGroupIndexMap;
	std::shared_ptr<OBJGroup> _currentGroup;
	bool _shouldGenerateNormals;

	void addVertexToCurrentGroup(const FaceVertex& faceVertex) {
		static constexpr glm::vec4 UP = { 0, 1, 0, 0 };
		static constexpr glm::vec2 ZERO2 = { 0, 0 };
		//static constexpr glm::vec4 RGBA_WHITE = { 1, 1, 1, 1 };
		static constexpr uint16_t INVALID_INDEX = 0xffff;

		uint16_t groupIndex;
		auto it = _vertexToGroupIndexMap.find(faceVertex);
		if (it != _vertexToGroupIndexMap.end()) {
			groupIndex = it->second;
		} else {
			Vertex vertex;
			vertex.position = _vertices[faceVertex.vi];
			vertex.normal = (faceVertex.ni != INVALID_INDEX) ? _normals[faceVertex.ni] : UP;
			//vertex.diffuseColor = RGBA_WHITE;
			vertex.texCoords = (faceVertex.ti != INVALID_INDEX) ? _texCoords[faceVertex.ti] : ZERO2;

			_groupVertices.push_back(vertex);
			groupIndex = _groupVertices.size() - 1;
			_vertexToGroupIndexMap[faceVertex] = groupIndex;
		}

		_groupIndices.push_back(groupIndex);
	};

	void addFaceWithFaceVertices(const std::vector<FaceVertex>& faceVertices) {
		// transform polygonal faces into "fans" of triangles, three vertices at a time
		for (size_t i = 0; i < faceVertices.size() - 2; ++i) {
			addVertexToCurrentGroup(faceVertices[0]);
			addVertexToCurrentGroup(faceVertices[i + 1]);
			addVertexToCurrentGroup(faceVertices[i + 2]);
		}
	};

	void generateNormalsForCurrentGroup() {
		static constexpr glm::vec4 ZERO = { 0, 0, 0, 0 };

		for (size_t i = 0; i < _groupVertices.size(); ++i) {
			_groupVertices[i].normal = ZERO;
		}

		for (size_t i = 0; i < _groupIndices.size(); i += 3) {
			uint16_t i0 = _groupIndices[i];
			uint16_t i1 = _groupIndices[i + 1];
			uint16_t i2 = _groupIndices[i + 2];

			auto v0 = &_groupVertices[i0];
			auto v1 = &_groupVertices[i1];
			auto v2 = &_groupVertices[i2];

			auto p0 = v0->position.xyz();
			auto p1 = v1->position.xyz();
			auto p2 = v2->position.xyz();

			auto cross = glm::cross((p1 - p0), (p2 - p0));
			auto cross4 = glm::vec4 { cross.x, cross.y, cross.z, 0 };

			v0->normal += cross4;
			v1->normal += cross4;
			v2->normal += cross4;
		}

		for (size_t i = 0; i < _groupVertices.size(); ++i) {
			_groupVertices[i].normal = glm::normalize(_groupVertices[i].normal);
		}
	};

	void endCurrentGroup() {
		if (!_currentGroup) {
			return;
		}

		if (_shouldGenerateNormals) {
			generateNormalsForCurrentGroup();
		}

		_currentGroup->vertexDataLength = _groupVertices.size() * sizeof(Vertex);
		void* vertexDataCopy = malloc(_currentGroup->vertexDataLength);
		memcpy(vertexDataCopy, _groupVertices.data(), _currentGroup->vertexDataLength);
		_currentGroup->vertexData = std::shared_ptr<void>(vertexDataCopy, DataDeleter());

		_currentGroup->indexDataLength = _groupIndices.size() * sizeof(uint16_t);
		void* indexDataCopy = malloc(_currentGroup->indexDataLength);
		memcpy(indexDataCopy, _groupIndices.data(), _currentGroup->indexDataLength);
		_currentGroup->indexData = std::shared_ptr<void>(indexDataCopy, DataDeleter());

		_groupVertices.clear();
		_groupIndices.clear();
		_vertexToGroupIndexMap.clear();

		_currentGroup = nullptr;
	};

	void beginGroupWithName(std::string name) {
		endCurrentGroup();

		auto newGroup = std::make_shared<OBJGroup>();
		newGroup->name = name;
		_groups.push_back(newGroup);
		_currentGroup = newGroup;
	};

	void parseModelFromData(const void* data, size_t length) {
		std::istringstream stream(std::string(static_cast<const char*>(data), length));

		while (!stream.eof()) {
			std::string token;

			stream >> token;

			if (token == "v") {
				float x;
				float y;
				float z;
				stream >> x;
				stream >> y;
				stream >> z;

				_vertices.push_back(glm::vec4 { x, y, z, 1 });
			} else if (token == "vt") {
				float u;
				float v;
				stream >> u;
				stream >> v;

				_texCoords.push_back(glm::vec2 { u, v });
			} else if (token == "vn") {
				float nx;
				float ny;
				float nz;
				stream >> nx;
				stream >> ny;
				stream >> nz;

				_normals.push_back(glm::vec4 { nx, ny, nz, 0 });
			} else if (token == "f") {
				std::vector<FaceVertex> faceVertices;
				faceVertices.reserve(4);

				std::string line;

				std::getline(stream, line);

				std::stringstream linestream(line);

				while (true) {
					int vi = 0;
					int ti = 0;
					int ni = 0;

					std::string token2;
					linestream.clear();
					linestream >> token2;
					if (linestream.fail()) {
						break;
					}
					linestream.clear();

					auto firstSlash = token2.find('/');
					auto secondSlash = (firstSlash != std::string::npos) ? token2.find('/', firstSlash + 1) : std::string::npos;

					try {
						vi = std::stoi(token2.substr(0, firstSlash));
					} catch (...) {
						break;
					}

					if (firstSlash != std::string::npos) {
						try {
							ti = std::stoi(token2.substr(firstSlash + 1, secondSlash - (firstSlash + 1)));

							if (secondSlash != std::string::npos) {
								try {
									ni = std::stoi(token2.substr(secondSlash + 1));
								} catch (...) {
									// nothing
								}
							}
						} catch (...) {
							// nothing
						}
					}

					FaceVertex faceVertex {};

					faceVertex.vi = (vi < 0) ? (_vertices.size() + vi - 1) : (vi - 1);
					faceVertex.ti = (ti < 0) ? (_texCoords.size() + ti - 1) : (ti - 1);
					faceVertex.ni = (ni < 0) ? (_vertices.size() + ni - 1) : (ni - 1);

					faceVertices.push_back(faceVertex);
				}

				addFaceWithFaceVertices(faceVertices);
			} else if (token == "g") {
				std::string groupName;
				stream >> groupName;
				beginGroupWithName(groupName);
			}
		}

		endCurrentGroup();
	};

public:
	OBJModel(const void* data, size_t dataLength, bool generateNormals):
		_shouldGenerateNormals(generateNormals)
	{
		parseModelFromData(data, dataLength);
	};

	std::shared_ptr<OBJGroup> groupForName(std::string name) {
		for (const auto& group: _groups) {
			if (group->name == name) {
				return group;
			}
		}
		return nullptr;
	};
};

class OBJMesh: public Mesh {
private:
	std::shared_ptr<Indium::Buffer> _vertexBuffer;
	std::shared_ptr<Indium::Buffer> _indexBuffer;

public:
	OBJMesh(std::shared_ptr<OBJGroup> group, std::shared_ptr<Indium::Device> device) {
		_vertexBuffer = device->newBuffer(group->vertexData.get(), group->vertexDataLength, Indium::ResourceOptions::CPUCacheModeDefaultCache);
		_indexBuffer = device->newBuffer(group->indexData.get(), group->indexDataLength, Indium::ResourceOptions::CPUCacheModeDefaultCache);
	};

	virtual std::shared_ptr<Indium::Buffer> vertexBuffer() const override {
		return _vertexBuffer;
	};

	virtual std::shared_ptr<Indium::Buffer> indexBuffer() const override {
		return _indexBuffer;
	};
};

class Renderer {
private:
	GLFWwindow* _window;
	VkSurfaceKHR _surface;
	std::shared_ptr<IndiumKit::Layer> _layer;
	glm::vec2 _viewportSize;

	std::shared_ptr<Indium::Device> _device;
	std::shared_ptr<Mesh> _mesh;
	std::shared_ptr<Indium::Buffer> _uniformBuffer;
	std::shared_ptr<Indium::CommandQueue> _commandQueue;
	std::shared_ptr<Indium::RenderPipelineState> _renderPipelineState;
	std::shared_ptr<Indium::DepthStencilState> _depthStencilState;
	Semaphore _displaySemaphore = Semaphore(inflightBufferCount);
	size_t _bufferIndex = 0;
	float _rotationX = 0;
	float _rotationY = 0;
	float _time = 0;

	std::shared_ptr<Indium::Texture> _depthTexture;
	std::shared_ptr<Indium::Texture> _diffuseTexture;
	std::shared_ptr<Indium::SamplerState> _samplerState;

	void makePipeline() {
		int width;
		int height;

		glfwGetFramebufferSize(_window, &width, &height);

		_layer = IndiumKit::Layer::make(_surface, _device, width, height);

		auto library = _device->newLibrary(shaders, shaders_len);

		Indium::RenderPipelineDescriptor pipelineDescriptor {};
		pipelineDescriptor.vertexFunction = library->newFunction("vertex_project");
		pipelineDescriptor.fragmentFunction = library->newFunction("fragment_texture");
		pipelineDescriptor.colorAttachments.emplace_back();
		pipelineDescriptor.colorAttachments[0].pixelFormat = _layer->pixelFormat();
		pipelineDescriptor.depthAttachmentPixelFormat = Indium::PixelFormat::Depth32Float;

		Indium::DepthStencilDescriptor depthStencilDescriptor {};
		depthStencilDescriptor.depthCompareFunction = Indium::CompareFunction::Less;
		depthStencilDescriptor.depthWriteEnabled = true;
		_depthStencilState = _device->newDepthStencilState(depthStencilDescriptor);

		_renderPipelineState = _device->newRenderPipelineState(pipelineDescriptor);
		_commandQueue = _device->newCommandQueue();
	};

	void makeResources() {
		int textureWidth;
		int textureHeight;
		int channelsInFile;

		stbi_set_flip_vertically_on_load(true);

		auto textureData = stbi_load_from_memory(spot_texture, spot_texture_len, &textureWidth, &textureHeight, &channelsInFile, 4);
		auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::RGBA8Unorm, textureWidth, textureHeight, true);
		textureDescriptor.usage = Indium::TextureUsage::ShaderRead;

		_diffuseTexture = _device->newTexture(textureDescriptor);
		auto region = Indium::Region::make2D(0, 0, textureWidth, textureHeight);
		auto bytesPerRow = 4 * static_cast<size_t>(textureWidth);
		_diffuseTexture->replaceRegion(region, 0, textureData, bytesPerRow);

		stbi_image_free(textureData);

		// generate mipmaps
		auto cmdbuf = _commandQueue->commandBuffer();
		auto blitEncoder = cmdbuf->blitCommandEncoder();
		blitEncoder->generateMipmapsForTexture(_diffuseTexture);
		blitEncoder->endEncoding();
		cmdbuf->commit();
		cmdbuf->waitUntilCompleted();

		auto model = std::make_shared<OBJModel>(spot, spot_len, true);
		auto group = model->groupForName("spot");
		_mesh = std::make_shared<OBJMesh>(group, _device);

		_uniformBuffer = _device->newBuffer(alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * inflightBufferCount, Indium::ResourceOptions::CPUCacheModeDefaultCache);

		Indium::SamplerDescriptor samplerDesc {};
		samplerDesc.sAddressMode = Indium::SamplerAddressMode::ClampToEdge;
		samplerDesc.tAddressMode = Indium::SamplerAddressMode::ClampToEdge;
		samplerDesc.minFilter = Indium::SamplerMinMagFilter::Nearest;
		samplerDesc.magFilter = Indium::SamplerMinMagFilter::Linear;
		samplerDesc.mipFilter = Indium::SamplerMipFilter::Linear;
		_samplerState = _device->newSamplerState(samplerDesc);
	};

	void init() {
		makePipeline();
		makeResources();

		auto textureDescriptor = Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float, _viewportSize.x, _viewportSize.y, false);
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
		float scaleFactor = 1;
		glm::vec3 xAxis = { 1, 0, 0 };
		glm::vec3 yAxis = { 0, 1, 0 };
		auto xRot = glm::rotate_slow(glm::identity<glm::mat4x4>(), _rotationX, xAxis);
		auto yRot = glm::rotate_slow(glm::identity<glm::mat4x4>(), _rotationY, yAxis);
		auto scale = glm::scale(glm::identity<glm::mat4x4>(), glm::vec3(scaleFactor));
		auto modelMatrix = (xRot * yRot) * scale;

		glm::vec3 cameraTranslation = { 0, 0, -1.5 };
		auto viewMatrix = glm::translate(glm::identity<glm::mat4x4>(), cameraTranslation);

		float aspect = _viewportSize.x / _viewportSize.y;
		float fov = (2 * M_PI) / 5;
		float near = 0.1;
		float far = 100;
		auto projectionMatrix = glm::perspectiveRH_NO(fov, aspect, near, far);

		Uniforms uniforms;
		uniforms.modelViewMatrix = viewMatrix * modelMatrix;
		uniforms.modelViewProjectionMatrix = projectionMatrix * uniforms.modelViewMatrix;

		// see `Uniforms::normalMatrix` for why we can't use `glm::mat3x3` here
		uniforms.normalMatrix[0][0] = uniforms.modelViewMatrix[0].x;
		uniforms.normalMatrix[0][1] = uniforms.modelViewMatrix[0].y;
		uniforms.normalMatrix[0][2] = uniforms.modelViewMatrix[0].z;
		uniforms.normalMatrix[1][0] = uniforms.modelViewMatrix[1].x;
		uniforms.normalMatrix[1][1] = uniforms.modelViewMatrix[1].y;
		uniforms.normalMatrix[1][2] = uniforms.modelViewMatrix[1].z;
		uniforms.normalMatrix[2][0] = uniforms.modelViewMatrix[2].x;
		uniforms.normalMatrix[2][1] = uniforms.modelViewMatrix[2].y;
		uniforms.normalMatrix[2][2] = uniforms.modelViewMatrix[2].z;

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
		renderPassDescriptor.colorAttachments[0].clearColor = Indium::ClearColor(0.95, 0.95, 0.95, 1);

		renderPassDescriptor.depthAttachment = Indium::RenderPassDepthAttachmentDescriptor();
		renderPassDescriptor.depthAttachment->texture = _depthTexture;
		renderPassDescriptor.depthAttachment->clearDepth = 1.0;
		renderPassDescriptor.depthAttachment->loadAction = Indium::LoadAction::Clear;
		renderPassDescriptor.depthAttachment->storeAction = Indium::StoreAction::DontCare;

		auto renderEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);

		renderEncoder->setRenderPipelineState(_renderPipelineState);
		renderEncoder->setDepthStencilState(_depthStencilState);
		renderEncoder->setFrontFacingWinding(Indium::Winding::CounterClockwise);
		renderEncoder->setCullMode(Indium::CullMode::Back);

		size_t uniformBufferOffset = alignToPowerOf2(sizeof(Uniforms), bufferAlignment) * _bufferIndex;

		renderEncoder->setVertexBuffer(_mesh->vertexBuffer(), 0, 0);
		renderEncoder->setVertexBuffer(_uniformBuffer, uniformBufferOffset, 1);

		renderEncoder->setFragmentTexture(_diffuseTexture, 0);
		renderEncoder->setFragmentSamplerState(_samplerState, 0);

		// !!!
		// this is not present in the original code, but it should be, since the shader expects this binding to exist (but it never uses it)
		// !!!
		renderEncoder->setFragmentBuffer(_uniformBuffer, uniformBufferOffset, 0);

		renderEncoder->drawIndexedPrimitives(Indium::PrimitiveType::Triangle, _mesh->indexBuffer()->length() / sizeof(uint16_t), Indium::IndexType::UInt16, _mesh->indexBuffer(), 0);

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
