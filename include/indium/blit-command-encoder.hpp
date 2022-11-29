#pragma once

#include <indium/command-encoder.hpp>
#include <indium/base.hpp>

#include <memory>

namespace Indium {
	class Texture;

	class BlitCommandEncoder: public CommandEncoder {
	public:
		virtual ~BlitCommandEncoder() = 0;

		virtual void generateMipmapsForTexture(std::shared_ptr<Texture> texture) = 0;
	};
};
