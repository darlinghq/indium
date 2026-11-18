#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <indium/types.hpp>

namespace Indium {
	class Device;

	/**
	 * The points in a pass at which a device can take a counter sample.
	 */
	enum class CounterSamplingPoint {
		AtStageBoundary = 0,
		AtDrawBoundary = 1,
		AtDispatchBoundary = 2,
		AtTileDispatchBoundary = 3,
		AtBlitBoundary = 4,
	};

	/// The sample index that stands for "take no sample" in a pass descriptor.
	static constexpr size_t counterDontSample = SIZE_MAX;

	/**
	 * Why a counter sample buffer could not be created.
	 */
	enum class CounterSampleBufferError {
		OutOfMemory = 0,
		Invalid = 1,
		Internal = 2,
	};

	/**
	 * The names of the common counter sets and counters a device can offer.
	 */
	static constexpr const char* counterSetTimestamp = "timestamp";
	static constexpr const char* counterTimestamp = "GPUTimestamp";

	struct Counter {
		std::string name;
	};

	struct CounterSet {
		std::string name;
		std::vector<Counter> counters;
	};

	struct CounterSampleBufferDescriptor {
		/// The name of the counter set to sample; empty if none was chosen.
		std::string counterSetName;
		size_t sampleCount = 0;
		std::string label;
	};

	/**
	 * Receives the result of a failed counter sample buffer creation.
	 */
	struct CounterSampleBufferFailure {
		CounterSampleBufferError error = CounterSampleBufferError::Internal;
		std::string message;
	};

	class CounterSampleBuffer {
	public:
		virtual ~CounterSampleBuffer() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		virtual size_t sampleCount() = 0;

		/**
		 * Reads back the samples in `range`, one 64-bit value each for a timestamp set.
		 *
		 * A sample that has not been written, or whose command buffer has not finished,
		 * reads as zero. A value is in nanoseconds on the device's timeline (see
		 * Device::sampleTimestamps()). An empty vector is returned for an empty or
		 * out-of-bounds range.
		 */
		virtual std::vector<char> resolveCounterRange(Range<size_t> range) = 0;
	};
};
