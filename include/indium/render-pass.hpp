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
		size_t level = 0;
		size_t slice = 0;
		size_t depthPlane = 0;
		std::shared_ptr<Texture> resolveTexture;
		size_t resolveLevel = 0;
		size_t resolveSlice = 0;
		size_t resolveDepthPlane = 0;
		LoadAction loadAction = LoadAction::Default;
		StoreAction storeAction = StoreAction::Default;
		StoreActionOptions storeActionOptions = StoreActionOptions::None;
	};

	struct ClearColor {
		ClearColor() {};
		ClearColor(double _red, double _green, double _blue, double _alpha):
			red(_red),
			green(_green),
			blue(_blue),
			alpha(_alpha)
			{};

		double red = 0;
		double green = 0;
		double blue = 0;
		double alpha = 1;
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
