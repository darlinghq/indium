// Copyright (C) 2026 Lubos Dolezel

#include <indium/parallel-render-command-encoder.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/render-command-encoder.private.hpp>

#include <stdexcept>

Indium::ParallelRenderCommandEncoder::~ParallelRenderCommandEncoder() {};

Indium::RecordingRenderCommandEncoder::~RecordingRenderCommandEncoder() {};

void Indium::RecordingRenderCommandEncoder::record(std::function<void(RenderCommandEncoder&)> call) {
	_calls.push_back(std::move(call));
};

void Indium::RecordingRenderCommandEncoder::replay(RenderCommandEncoder& encoder) {
	for (auto& call: _calls) {
		call(encoder);
	}
	// what the calls held is the real encoder's or already released with it
	_calls.clear();
};

void Indium::RecordingRenderCommandEncoder::endEncoding() {
	// nothing to do: what was recorded is played when the whole pass ends
};

void Indium::RecordingRenderCommandEncoder::setRenderPipelineState(const std::shared_ptr<RenderPipelineState>& renderPipelineState) {
	record([renderPipelineState = renderPipelineState](RenderCommandEncoder& encoder) mutable {
		encoder.setRenderPipelineState(renderPipelineState);
	});
};

void Indium::RecordingRenderCommandEncoder::sampleCounters(const std::shared_ptr<CounterSampleBuffer>& sampleBuffer, size_t sampleIndex, bool barrier) {
	record([sampleBuffer = sampleBuffer, sampleIndex = sampleIndex, barrier = barrier](RenderCommandEncoder& encoder) mutable {
		encoder.sampleCounters(sampleBuffer, sampleIndex, barrier);
	});
};

void Indium::RecordingRenderCommandEncoder::setFrontFacingWinding(Winding frontFaceWinding) {
	record([frontFaceWinding = frontFaceWinding](RenderCommandEncoder& encoder) mutable {
		encoder.setFrontFacingWinding(frontFaceWinding);
	});
};

void Indium::RecordingRenderCommandEncoder::setCullMode(CullMode cullMode) {
	record([cullMode = cullMode](RenderCommandEncoder& encoder) mutable {
		encoder.setCullMode(cullMode);
	});
};

void Indium::RecordingRenderCommandEncoder::setDepthBias(float depthBias, float slopeScale, float clamp) {
	record([depthBias = depthBias, slopeScale = slopeScale, clamp = clamp](RenderCommandEncoder& encoder) mutable {
		encoder.setDepthBias(depthBias, slopeScale, clamp);
	});
};

void Indium::RecordingRenderCommandEncoder::setDepthClipMode(DepthClipMode depthClipMode) {
	record([depthClipMode = depthClipMode](RenderCommandEncoder& encoder) mutable {
		encoder.setDepthClipMode(depthClipMode);
	});
};

void Indium::RecordingRenderCommandEncoder::setViewport(const Viewport& viewport) {
	record([viewport = viewport](RenderCommandEncoder& encoder) mutable {
		encoder.setViewport(viewport);
	});
};

void Indium::RecordingRenderCommandEncoder::setViewports(const Viewport* viewports, size_t count) {
	record([values = std::vector<Viewport>(viewports, viewports + count)](RenderCommandEncoder& encoder) mutable {
		encoder.setViewports(values.data(), values.size());
	});
};

void Indium::RecordingRenderCommandEncoder::setViewports(const std::vector<Viewport>& viewports) {
	record([viewports = viewports](RenderCommandEncoder& encoder) mutable {
		encoder.setViewports(viewports);
	});
};

void Indium::RecordingRenderCommandEncoder::setScissorRect(const ScissorRect& scissorRect) {
	record([scissorRect = scissorRect](RenderCommandEncoder& encoder) mutable {
		encoder.setScissorRect(scissorRect);
	});
};

void Indium::RecordingRenderCommandEncoder::setScissorRects(const ScissorRect* scissorRects, size_t count) {
	record([values = std::vector<ScissorRect>(scissorRects, scissorRects + count)](RenderCommandEncoder& encoder) mutable {
		encoder.setScissorRects(values.data(), values.size());
	});
};

void Indium::RecordingRenderCommandEncoder::setScissorRects(const std::vector<ScissorRect>& scissorRects) {
	record([scissorRects = scissorRects](RenderCommandEncoder& encoder) mutable {
		encoder.setScissorRects(scissorRects);
	});
};

void Indium::RecordingRenderCommandEncoder::setBlendColor(float red, float green, float blue, float alpha) {
	record([red = red, green = green, blue = blue, alpha = alpha](RenderCommandEncoder& encoder) mutable {
		encoder.setBlendColor(red, green, blue, alpha);
	});
};

void Indium::RecordingRenderCommandEncoder::setDepthStencilState(const std::shared_ptr<DepthStencilState>& state) {
	record([state = state](RenderCommandEncoder& encoder) mutable {
		encoder.setDepthStencilState(state);
	});
};

void Indium::RecordingRenderCommandEncoder::setTriangleFillMode(TriangleFillMode triangleFillMode) {
	record([triangleFillMode = triangleFillMode](RenderCommandEncoder& encoder) mutable {
		encoder.setTriangleFillMode(triangleFillMode);
	});
};

void Indium::RecordingRenderCommandEncoder::setStencilReferenceValue(uint32_t value) {
	record([value = value](RenderCommandEncoder& encoder) mutable {
		encoder.setStencilReferenceValue(value);
	});
};

void Indium::RecordingRenderCommandEncoder::setStencilReferenceValue(uint32_t front, uint32_t back) {
	record([front = front, back = back](RenderCommandEncoder& encoder) mutable {
		encoder.setStencilReferenceValue(front, back);
	});
};

void Indium::RecordingRenderCommandEncoder::setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) {
	record([storeAction = storeAction, colorAttachmentIndex = colorAttachmentIndex](RenderCommandEncoder& encoder) mutable {
		encoder.setColorStoreAction(storeAction, colorAttachmentIndex);
	});
};

void Indium::RecordingRenderCommandEncoder::setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) {
	record([options = options, colorAttachmentIndex = colorAttachmentIndex](RenderCommandEncoder& encoder) mutable {
		encoder.setColorStoreActionOptions(options, colorAttachmentIndex);
	});
};

void Indium::RecordingRenderCommandEncoder::setDepthStoreAction(StoreAction storeAction) {
	record([storeAction = storeAction](RenderCommandEncoder& encoder) mutable {
		encoder.setDepthStoreAction(storeAction);
	});
};

void Indium::RecordingRenderCommandEncoder::setDepthStoreActionOptions(StoreActionOptions options) {
	record([options = options](RenderCommandEncoder& encoder) mutable {
		encoder.setDepthStoreActionOptions(options);
	});
};

void Indium::RecordingRenderCommandEncoder::setStencilStoreAction(StoreAction storeAction) {
	record([storeAction = storeAction](RenderCommandEncoder& encoder) mutable {
		encoder.setStencilStoreAction(storeAction);
	});
};

void Indium::RecordingRenderCommandEncoder::setStencilStoreActionOptions(StoreActionOptions options) {
	record([options = options](RenderCommandEncoder& encoder) mutable {
		encoder.setStencilStoreActionOptions(options);
	});
};

void Indium::RecordingRenderCommandEncoder::setVisibilityResultMode(VisibilityResultMode mode, size_t offset) {
	record([mode = mode, offset = offset](RenderCommandEncoder& encoder) mutable {
		encoder.setVisibilityResultMode(mode, offset);
	});
};

void Indium::RecordingRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount, size_t baseInstance) {
	record([primitiveType = primitiveType, vertexStart = vertexStart, vertexCount = vertexCount, instanceCount = instanceCount, baseInstance = baseInstance](RenderCommandEncoder& encoder) mutable {
		encoder.drawPrimitives(primitiveType, vertexStart, vertexCount, instanceCount, baseInstance);
	});
};

void Indium::RecordingRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount, size_t instanceCount) {
	record([primitiveType = primitiveType, vertexStart = vertexStart, vertexCount = vertexCount, instanceCount = instanceCount](RenderCommandEncoder& encoder) mutable {
		encoder.drawPrimitives(primitiveType, vertexStart, vertexCount, instanceCount);
	});
};

void Indium::RecordingRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, size_t vertexStart, size_t vertexCount) {
	record([primitiveType = primitiveType, vertexStart = vertexStart, vertexCount = vertexCount](RenderCommandEncoder& encoder) mutable {
		encoder.drawPrimitives(primitiveType, vertexStart, vertexCount);
	});
};

void Indium::RecordingRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount, int64_t baseVertex, size_t baseInstance) {
	record([primitiveType = primitiveType, indexCount = indexCount, indexType = indexType, indexBuffer = indexBuffer, indexBufferOffset = indexBufferOffset, instanceCount = instanceCount, baseVertex = baseVertex, baseInstance = baseInstance](RenderCommandEncoder& encoder) mutable {
		encoder.drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount, baseVertex, baseInstance);
	});
};

void Indium::RecordingRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, size_t instanceCount) {
	record([primitiveType = primitiveType, indexCount = indexCount, indexType = indexType, indexBuffer = indexBuffer, indexBufferOffset = indexBufferOffset, instanceCount = instanceCount](RenderCommandEncoder& encoder) mutable {
		encoder.drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset, instanceCount);
	});
};

void Indium::RecordingRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, size_t indexCount, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset) {
	record([primitiveType = primitiveType, indexCount = indexCount, indexType = indexType, indexBuffer = indexBuffer, indexBufferOffset = indexBufferOffset](RenderCommandEncoder& encoder) mutable {
		encoder.drawIndexedPrimitives(primitiveType, indexCount, indexType, indexBuffer, indexBufferOffset);
	});
};

void Indium::RecordingRenderCommandEncoder::drawPrimitives(PrimitiveType primitiveType, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) {
	record([primitiveType = primitiveType, indirectBuffer = indirectBuffer, indirectBufferOffset = indirectBufferOffset](RenderCommandEncoder& encoder) mutable {
		encoder.drawPrimitives(primitiveType, indirectBuffer, indirectBufferOffset);
	});
};

void Indium::RecordingRenderCommandEncoder::drawIndexedPrimitives(PrimitiveType primitiveType, IndexType indexType, const std::shared_ptr<Buffer>& indexBuffer, size_t indexBufferOffset, const std::shared_ptr<Buffer>& indirectBuffer, size_t indirectBufferOffset) {
	record([primitiveType = primitiveType, indexType = indexType, indexBuffer = indexBuffer, indexBufferOffset = indexBufferOffset, indirectBuffer = indirectBuffer, indirectBufferOffset = indirectBufferOffset](RenderCommandEncoder& encoder) mutable {
		encoder.drawIndexedPrimitives(primitiveType, indexType, indexBuffer, indexBufferOffset, indirectBuffer, indirectBufferOffset);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexBytes(const void* bytes, size_t length, size_t index) {
	record([bytes = std::vector<uint8_t>(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + length), length = length, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexBytes(bytes.data(), length, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) {
	record([buffer = buffer, offset = offset, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexBuffer(buffer, offset, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) {
	record([buffers = buffers, offsets = offsets, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexBuffers(buffers, offsets, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexBufferOffset(size_t offset, size_t index) {
	record([offset = offset, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexBufferOffset(offset, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) {
	record([state = state, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexSamplerState(state, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) {
	record([state = state, lodMinClamp = lodMinClamp, lodMaxClamp = lodMaxClamp, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexSamplerState(state, lodMinClamp, lodMaxClamp, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) {
	record([states = states, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexSamplerStates(states, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) {
	record([states = states, lodMinClamps = lodMinClamps, lodMaxClamps = lodMaxClamps, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexSamplerStates(states, lodMinClamps, lodMaxClamps, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexTexture(const std::shared_ptr<Texture>& texture, size_t index) {
	record([texture = texture, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexTexture(texture, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setVertexTextures(const std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	record([textures = textures, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setVertexTextures(textures, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentBytes(const void* bytes, size_t length, size_t index) {
	record([bytes = std::vector<uint8_t>(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + length), length = length, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentBytes(bytes.data(), length, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentBuffer(const std::shared_ptr<Buffer>& buffer, size_t offset, size_t index) {
	record([buffer = buffer, offset = offset, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentBuffer(buffer, offset, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentBuffers(const std::vector<std::shared_ptr<Buffer>>& buffers, const std::vector<size_t>& offsets, Range<size_t> range) {
	record([buffers = buffers, offsets = offsets, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentBuffers(buffers, offsets, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentBufferOffset(size_t offset, size_t index) {
	record([offset = offset, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentBufferOffset(offset, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, size_t index) {
	record([state = state, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentSamplerState(state, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentSamplerState(const std::shared_ptr<SamplerState>& state, float lodMinClamp, float lodMaxClamp, size_t index) {
	record([state = state, lodMinClamp = lodMinClamp, lodMaxClamp = lodMaxClamp, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentSamplerState(state, lodMinClamp, lodMaxClamp, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, Range<size_t> range) {
	record([states = states, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentSamplerStates(states, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentSamplerStates(const std::vector<std::shared_ptr<SamplerState>>& states, const std::vector<float>& lodMinClamps, const std::vector<float>& lodMaxClamps, Range<size_t> range) {
	record([states = states, lodMinClamps = lodMinClamps, lodMaxClamps = lodMaxClamps, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentSamplerStates(states, lodMinClamps, lodMaxClamps, range);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentTexture(const std::shared_ptr<Texture>& texture, size_t index) {
	record([texture = texture, index = index](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentTexture(texture, index);
	});
};

void Indium::RecordingRenderCommandEncoder::setFragmentTextures(std::vector<std::shared_ptr<Texture>>& textures, Range<size_t> range) {
	record([textures = textures, range = range](RenderCommandEncoder& encoder) mutable {
		encoder.setFragmentTextures(textures, range);
	});
};

void Indium::RecordingRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage, RenderStages stages) {
	record([resource = resource, usage = usage, stages = stages](RenderCommandEncoder& encoder) mutable {
		encoder.useResource(resource, usage, stages);
	});
};

void Indium::RecordingRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage, RenderStages stages) {
	record([resources = resources, usage = usage, stages = stages](RenderCommandEncoder& encoder) mutable {
		encoder.useResources(resources, usage, stages);
	});
};

void Indium::RecordingRenderCommandEncoder::useResource(std::shared_ptr<Resource> resource, ResourceUsage usage) {
	record([resource = resource, usage = usage](RenderCommandEncoder& encoder) mutable {
		encoder.useResource(resource, usage);
	});
};

void Indium::RecordingRenderCommandEncoder::useResources(const std::vector<std::shared_ptr<Resource>>& resources, ResourceUsage usage) {
	record([resources = resources, usage = usage](RenderCommandEncoder& encoder) mutable {
		encoder.useResources(resources, usage);
	});
};

void Indium::RecordingRenderCommandEncoder::useHeap(const std::shared_ptr<Heap>& heap) {
	record([heap = heap](RenderCommandEncoder& encoder) mutable {
		encoder.useHeap(heap);
	});
};

void Indium::RecordingRenderCommandEncoder::useHeap(const std::shared_ptr<Heap>& heap, RenderStages stages) {
	record([heap = heap, stages = stages](RenderCommandEncoder& encoder) mutable {
		encoder.useHeap(heap, stages);
	});
};

void Indium::RecordingRenderCommandEncoder::useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps) {
	record([heaps = heaps](RenderCommandEncoder& encoder) mutable {
		encoder.useHeaps(heaps);
	});
};

void Indium::RecordingRenderCommandEncoder::useHeaps(const std::vector<std::shared_ptr<Heap>>& heaps, RenderStages stages) {
	record([heaps = heaps, stages = stages](RenderCommandEncoder& encoder) mutable {
		encoder.useHeaps(heaps, stages);
	});
};

void Indium::RecordingRenderCommandEncoder::memoryBarrier(BarrierScope scope, RenderStages after, RenderStages before) {
	record([scope = scope, after = after, before = before](RenderCommandEncoder& encoder) mutable {
		encoder.memoryBarrier(scope, after, before);
	});
};

void Indium::RecordingRenderCommandEncoder::updateFence(const std::shared_ptr<Fence>& fence, RenderStages after) {
	record([fence = fence, after = after](RenderCommandEncoder& encoder) mutable {
		encoder.updateFence(fence, after);
	});
};

void Indium::RecordingRenderCommandEncoder::waitForFence(const std::shared_ptr<Fence>& fence, RenderStages before) {
	record([fence = fence, before = before](RenderCommandEncoder& encoder) mutable {
		encoder.waitForFence(fence, before);
	});
};

Indium::PrivateParallelRenderCommandEncoder::PrivateParallelRenderCommandEncoder(std::shared_ptr<PrivateCommandBuffer> commandBuffer, const RenderPassDescriptor& descriptor):
	_commandBuffer(commandBuffer),
	_descriptor(descriptor)
{};

Indium::PrivateParallelRenderCommandEncoder::~PrivateParallelRenderCommandEncoder() {};

std::shared_ptr<Indium::RenderCommandEncoder> Indium::PrivateParallelRenderCommandEncoder::renderCommandEncoder() {
	std::scoped_lock lock(_mutex);
	if (_ended) {
		return nullptr;
	}
	auto encoder = std::make_shared<RecordingRenderCommandEncoder>();
	_encoders.push_back(encoder);
	return encoder;
};

void Indium::PrivateParallelRenderCommandEncoder::setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) {
	std::scoped_lock lock(_mutex);
	if (colorAttachmentIndex < _descriptor.colorAttachments.size()) {
		_descriptor.colorAttachments[colorAttachmentIndex].storeAction = storeAction;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) {
	std::scoped_lock lock(_mutex);
	if (colorAttachmentIndex < _descriptor.colorAttachments.size()) {
		_descriptor.colorAttachments[colorAttachmentIndex].storeActionOptions = options;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::setDepthStoreAction(StoreAction storeAction) {
	std::scoped_lock lock(_mutex);
	if (_descriptor.depthAttachment) {
		_descriptor.depthAttachment->storeAction = storeAction;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::setDepthStoreActionOptions(StoreActionOptions options) {
	std::scoped_lock lock(_mutex);
	if (_descriptor.depthAttachment) {
		_descriptor.depthAttachment->storeActionOptions = options;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::setStencilStoreAction(StoreAction storeAction) {
	std::scoped_lock lock(_mutex);
	if (_descriptor.stencilAttachment) {
		_descriptor.stencilAttachment->storeAction = storeAction;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::setStencilStoreActionOptions(StoreActionOptions options) {
	std::scoped_lock lock(_mutex);
	if (_descriptor.stencilAttachment) {
		_descriptor.stencilAttachment->storeActionOptions = options;
	}
};

void Indium::PrivateParallelRenderCommandEncoder::endEncoding() {
	std::vector<std::shared_ptr<RecordingRenderCommandEncoder>> encoders;
	{
		std::scoped_lock lock(_mutex);
		if (_ended) {
			return;
		}
		_ended = true;
		encoders.swap(_encoders);
	}

	auto commandBuffer = _commandBuffer.lock();
	if (!commandBuffer) {
		return;
	}

	// One pass: the load actions apply at its start and the store actions at its end, and the encoders' commands
	// follow each other in the order the encoders were made.
	auto pass = std::dynamic_pointer_cast<PrivateRenderCommandEncoder>(commandBuffer->renderCommandEncoder(_descriptor));
	bool first = true;
	for (const auto& encoder: encoders) {
		if (!first) {
			pass->resetState();
		}
		first = false;
		encoder->replay(*pass);
	}
	pass->endEncoding();
};
