#pragma once

#include <memory>
#include <vector>

#include <indium/types.hpp>

namespace Indium {
	class Device;

	class CounterSampleBuffer {
	public:
		virtual ~CounterSampleBuffer() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual size_t sampleCount() = 0;
		virtual std::vector<char> resolveCounterRange(Range<size_t> range) = 0;
	};
};
