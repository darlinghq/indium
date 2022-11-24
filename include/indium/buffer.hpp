#pragma once

#include <indium/resource.hpp>
#include <indium/types.hpp>

namespace Indium {
	class Buffer: public Resource {
	public:
		virtual ~Buffer() = 0;

		virtual size_t length() const = 0;
		virtual void* contents() = 0;
		virtual void didModifyRange(Range<size_t> range) = 0;

		virtual uint64_t gpuAddress() = 0;
	};
};
