#pragma once

#include <indium/base.hpp>
#include <indium/types.hpp>
#include <indium/library.hpp>
#include <indium/vertex-descriptor.hpp>
#include <indium/pipeline.hpp>
#include <indium/binary-archive.hpp>
#include <indium/linked-functions.hpp>

#include <memory>

namespace Indium {
	class Device;
	class RenderCommandEncoder;

	struct RenderPipelineColorAttachmentDescriptor {
		ColorWriteMask writeMask = ColorWriteMask::All;
		PixelFormat pixelFormat;
		bool blendingEnabled = false;
		BlendFactor sourceRGBBlendFactor = BlendFactor::One;
		BlendFactor destinationRGBBlendFactor = BlendFactor::Zero;
		BlendOperation rgbBlendOperation = BlendOperation::Add;
		BlendFactor sourceAlphaBlendFactor = BlendFactor::One;
		BlendFactor destinationAlphaBlendFactor = BlendFactor::Zero;
		BlendOperation alphaBlendOperation = BlendOperation::Add;
	};

	struct RenderPipelineDescriptor {
		std::shared_ptr<Function> vertexFunction = nullptr;
		std::shared_ptr<Function> fragmentFunction = nullptr;
		size_t maxVertexCallStackDepth = 1;
		size_t maxFragmentCallStackDepth = 1;
		std::shared_ptr<VertexDescriptor> vertexDescriptor;
		std::vector<PipelineBufferDescriptor> vertexBuffers;
		std::vector<PipelineBufferDescriptor> fragmentBuffers;
		std::vector<RenderPipelineColorAttachmentDescriptor> colorAttachments;
		PixelFormat depthAttachmentPixelFormat = PixelFormat::Invalid;
		PixelFormat stencilAttachmentPixelFormat = PixelFormat::Invalid;
		bool alphaToCoverageEnabled = false;
		bool alphaToOneEnabled = false;
		bool rasterizationEnabled = true;
		PrimitiveTopologyClass inputPrimitiveTopology = PrimitiveTopologyClass::Unspecified;
		size_t rasterSampleCount;
		size_t maxTessellationFactor = 16;
		bool tessellationFactorScaleEnabled = false;
		TessellationFactorFormat tessellationFactorFormat = TessellationFactorFormat::Half;
		TessellationControlPointIndexType tessellationControlPointIndexType = TessellationControlPointIndexType::None;
		TessellationFactorStepFunction tessellationFactorStepFunction = TessellationFactorStepFunction::Constant;
		Winding tessellationOutputWindingOrder = Winding::Clockwise;
		TessellationPartitionMode tessellationPartitionMode = TessellationPartitionMode::Pow2;
		bool supportIndirectCommandBuffers;
		size_t maxVertexAmplificationCount;
		bool supportAddingVertexBinaryFunctions;
		bool supportAddingFragmentBinaryFunctions;
		std::vector<std::shared_ptr<BinaryArchive>> binaryArchives;
		std::shared_ptr<LinkedFunctions> vertexLinkedFunctions;
		std::shared_ptr<LinkedFunctions> fragmentLinkedFunctions;
	};

	class RenderPipelineState {
	public:
		virtual ~RenderPipelineState() = 0;

		virtual std::shared_ptr<Device> device() = 0;
		INDIUM_PROPERTY_READONLY(size_t, m, M,axTotalThreadsPerThreadgroup);
		INDIUM_PROPERTY_READONLY(bool, t, T,hreadgroupSizeMatchesTileSize);
		INDIUM_PROPERTY_READONLY(size_t, i, I,mageblockSampleLength);
		INDIUM_PROPERTY_READONLY(size_t, i, I,mageblockMemoryLength);
		INDIUM_PROPERTY_READONLY(bool, s, S,upportIndirectCommandBuffers);
		INDIUM_PROPERTY_READONLY(size_t, m, M,axTotalThreadsPerObjectThreadgroup);
		INDIUM_PROPERTY_READONLY(size_t, m, M,axTotalThreadsPerMeshThreadgroup);
		INDIUM_PROPERTY_READONLY(size_t, o, O,bjectThreadExecutionWidth);
		INDIUM_PROPERTY_READONLY(size_t, m, M,eshThreadExecutionWidth);
	};
};
