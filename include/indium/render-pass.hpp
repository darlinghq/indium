#pragma once

#include <memory>
#include <vector>
#include <optional>

#include <indium/buffer.hpp>
#include <indium/base.hpp>
#include <indium/rasterization-rate.hpp>
#include <indium/types.hpp>
#include <indium/texture.hpp>

namespace Indium {
	struct RenderPassAttachmentDescriptor {
		std::shared_ptr<Texture> texture;
		size_t level;
		size_t slice;
		size_t depthPlane;
		std::shared_ptr<Texture> resolveTexture;
		size_t resolveLevel;
		size_t resolveSlice;
		size_t resolveDepthPlane;
		LoadAction loadAction;
		StoreAction storeAction;
		StoreActionOptions storeActionOptions;
	};

	struct ClearColor {
		ClearColor() {};
		ClearColor(double _red, double _green, double _blue, double _alpha):
			red(_red),
			green(_green),
			blue(_blue),
			alpha(_alpha)
			{};

		double red;
		double green;
		double blue;
		double alpha;
	};

	struct RenderPassColorAttachmentDescriptor: public RenderPassAttachmentDescriptor {
		ClearColor clearColor;
	};

	struct RenderPassDepthAttachmentDescriptor: public RenderPassAttachmentDescriptor {
		double clearDepth;
		MultisampleDepthResolveFilter depthResolveFilter;
	};

	struct RenderPassStencilAttachmentDescriptor: public RenderPassAttachmentDescriptor {
		uint32_t clearStencil;
		MultisampleStencilResolveFilter stencilResolveFilter;
	};

	struct RenderPassSampleBufferAttachmentDescriptor {
		// TODO
	};

	struct RenderPassDescriptor {
		std::vector<RenderPassColorAttachmentDescriptor> colorAttachments;
		std::optional<RenderPassDepthAttachmentDescriptor> depthAttachment;
		std::optional<RenderPassStencilAttachmentDescriptor> stencilAttachment;
		std::shared_ptr<Buffer> visibilityResultBuffer;
		size_t renderTargetArrayLength;
		size_t imageblockSampleLength;
		size_t threadgroupMemoryLength;
		size_t tileWidth;
		size_t tileHeight;
		size_t defaultRasterSampleCount;
		size_t renderTargetWidth;
		size_t renderTargetHeight;
		std::shared_ptr<RasterizationRateMap> rasterizationRateMap;
		std::shared_ptr<RenderPassSampleBufferAttachmentDescriptor> sampleBufferAttachments;
		std::vector<SamplePosition> samplePositions;
	};
};
