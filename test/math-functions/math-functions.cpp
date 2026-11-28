// Copyright (C) 2026 Darling Developers
//
// Runs the kernels in math-functions.metallib, which call the AIR math functions without a direct
// GLSL.std.450 counterpart (sinpi, cospi, tanpi, exp10, log10, ldexp, nextafter, sincos) and the inverse
// hyperbolic functions on float, half and float and half vectors, and llvm.copysign, and compares the results
// with the functions computed here on the host: exactly where the result is exact (ldexp, nextafter, copysign,
// and sinpi, cospi and tanpi at integers and half-integers), otherwise to a tolerance.
// Prints SKIP and exits 0 without a Vulkan device.

#include "math-functions.h"

#include <indium/indium.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	failures += !ok;
}

// Runs `kernel` with one thread per input value and returns `slots` results per thread.
std::vector<int32_t> run(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, size_t slots) {
	auto pipeline = device->newComputePipelineState(library->newFunction(kernel));
	if (!pipeline) {
		throw std::runtime_error(std::string(kernel) + " did not load");
	}
	std::vector<int32_t> poison(input.size() * slots);
	std::memset(poison.data(), 0xA5, poison.size() * sizeof(int32_t));
	auto in = device->newBuffer(input.data(), input.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared);
	auto out = device->newBuffer(poison.data(), poison.size() * sizeof(int32_t), Indium::ResourceOptions::StorageModeShared);
	auto commandBuffer = queue->commandBuffer();
	auto encoder = commandBuffer->computeCommandEncoder();
	encoder->setComputePipelineState(pipeline);
	encoder->setBuffer(out, 0, 0);
	encoder->setBuffer(in, 0, 1);
	encoder->dispatchThreadgroups(Indium::Size { 1, 1, 1 }, Indium::Size { input.size(), 1, 1 });
	encoder->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	std::vector<int32_t> result(poison.size());
	std::memcpy(result.data(), out->contents(), result.size() * sizeof(int32_t));
	return result;
}

// Runs `kernel` and compares its results with `want`, `slots` per input value.
void check(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, const std::vector<int32_t>& want, const std::string& what) {
	try {
		size_t slots = want.size() / input.size();
		auto got = run(device, queue, library, kernel, input, slots);
		for (size_t i = 0; i < want.size(); ++i) {
			if (got[i] != want[i]) {
				std::printf("  %s: input 0x%08x slot %zu: got 0x%08x, expected 0x%08x\n", kernel, static_cast<uint32_t>(input[i / slots]), i % slots, static_cast<uint32_t>(got[i]), static_cast<uint32_t>(want[i]));
			}
		}
		report(got == want, std::string(kernel) + ": " + what);
	} catch (const std::exception& e) {
		report(false, std::string(kernel) + ": " + e.what());
	}
}

// The result a slot should hold: `value` rounded to the kernel's float type, bit for bit when `exact` (NaN
// matching any NaN, and a subnormal also a zero of its sign, since Metal may flush subnormals), otherwise within the kernel's tolerance, relative to the
// result or to 1, whichever is larger (Vulkan bounds the error of some functions absolutely, e.g. sin's).
struct Expected {
	double value;
	bool exact;
	// whether +0 is right too: the input was subnormal, and may have been flushed
	bool orZero = false;
};

// Runs `kernel` with one thread per input value, reading `want.size() / input.size()` float (`half` false) or
// half (`half` true) results per thread, and compares them with `want`.
void checkFloat(Indium::Device* device, Indium::CommandQueue* queue, Indium::Library* library, const char* kernel, const std::vector<int32_t>& input, const std::vector<Expected>& want, bool half, double tolerance, const std::string& what) {
	try {
		size_t slots = want.size() / input.size();
		auto got = run(device, queue, library, kernel, input, slots);
		bool ok = true;
		for (size_t i = 0; i < want.size(); ++i) {
			double value;
			uint32_t bits, wantBits;
			if (half) {
				uint16_t raw = static_cast<uint16_t>(got[i]);
				_Float16 h, w = static_cast<_Float16>(want[i].value);
				std::memcpy(&h, &raw, 2);
				uint16_t rawWant;
				std::memcpy(&rawWant, &w, 2);
				value = h;
				bits = static_cast<uint32_t>(got[i]);
				wantBits = rawWant;
			} else {
				float f, w = static_cast<float>(want[i].value);
				std::memcpy(&f, &got[i], 4);
				std::memcpy(&wantBits, &w, 4);
				value = f;
				bits = static_cast<uint32_t>(got[i]);
			}
			bool match;
			if (std::isnan(want[i].value)) {
				match = std::isnan(value);
			} else if (want[i].exact) {
				uint32_t signBit = half ? 0x8000u : 0x80000000u, exponentBits = half ? 0x7c00u : 0x7f800000u;
				bool subnormal = (wantBits & exponentBits) == 0;
				match = bits == wantBits || (subnormal && bits == (wantBits & signBit)) || (want[i].orZero && bits == 0);
			} else {
				match = std::fabs(value - want[i].value) <= tolerance * std::max(std::fabs(want[i].value), 1.0);
			}
			if (!match) {
				std::printf("  %s: input 0x%08x slot %zu: got %a (0x%x), expected %a%s\n", kernel, static_cast<uint32_t>(input[i / slots]), i % slots, value, bits, want[i].value, want[i].exact ? " exactly" : "");
				ok = false;
			}
		}
		report(ok, std::string(kernel) + ": " + what);
	} catch (const std::exception& e) {
		report(false, std::string(kernel) + ": " + e.what());
	}
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		if (Indium::copyAllDevices().empty()) {
			std::printf("SKIP no device\n");
			Indium::finit();
			return 0;
		}

		auto device = Indium::createSystemDefaultDevice();
		std::printf("device: %s\n", device->name().c_str());
		// the bounded wait lets the thread see the flag even if the wakeup comes before it waits
		std::atomic<bool> keepPollingDevice { true };
		std::thread devicePollingThread([&]() {
			while (keepPollingDevice) {
				device->pollEvents(100000000);
			}
		});
		auto queue = device->newCommandQueue();
		auto library = device->newLibrary(math_functions_metallib, math_functions_metallib_len);
		report(library != nullptr, "the library loads");

		if (library) {
			// thread i pairs values[i] with values[(i + 8) % 16]; all are nonzero and smaller than 16 in magnitude, so that every
			// function is defined on what the kernel passes it, and representable as half
			std::vector<double> values { 0.75, -0.3, 1.5, -2.25, 0.1, 3.0, -5.5, 12.0, 0.001, -0.9, 7.25, -15.5, 2.0, -0.5, 9.75, -1.0 };
			// integers and half-integers, where sinpi, cospi and tanpi are exact, their neighbours, other values, infinities and a NaN
			std::vector<double> piValues { 0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 3.0, -4.0, 0.5, -0.5, 1.5, -1.5, 2.5, -3.5, 0.25, -0.25, 0.75, 1.0 / 3, -0.1, 0.9, 0.001, -7.75, 123.4, 1048576.5, 16777216.0, 33554434.0, 8388609.0, INFINITY, -INFINITY, NAN, std::nextafter(0.5f, 1.0f), std::nextafter(1.5f, 1.0f) };
			std::vector<double> piValuesHalf { 0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 3.0, -4.0, 0.5, -0.5, 1.5, -1.5, 2.5, -3.5, 0.25, -0.25, 0.75, 1.0 / 3, -0.1, 0.9, 0.001, -7.75, 123.4, 512.5, 2050.0, 1025.0, 65504.0, INFINITY, -INFINITY, NAN, 0.5 + 0x1p-11, 1.5 - 0x1p-10 };

			// the bits of the values as float or as half, and the values as rounded to that type
			auto encode = [](const std::vector<double>& in, bool half, std::vector<int32_t>& bits, std::vector<double>& rounded) {
				for (double v: in) {
					if (half) {
						_Float16 h = static_cast<_Float16>(v);
						uint16_t b;
						std::memcpy(&b, &h, 2);
						bits.push_back(b);
						rounded.push_back(h);
					} else {
						float f = static_cast<float>(v);
						int32_t b;
						std::memcpy(&b, &f, 4);
						bits.push_back(b);
						rounded.push_back(f);
					}
				}
			};
			auto toType = [](double v, bool half) { return half ? static_cast<double>(static_cast<_Float16>(v)) : static_cast<double>(static_cast<float>(v)); };
			// the half after x in the direction of y, found among all halves
			auto nextafterHalf = [](double x, double y) -> double {
				if (std::isnan(x) || std::isnan(y)) {
					return NAN;
				}
				if (x == y) {
					return y;
				}
				double next = y > x ? INFINITY : -INFINITY;
				for (uint32_t b = 0; b < 0x10000; ++b) {
					uint16_t bits = static_cast<uint16_t>(b);
					_Float16 h;
					std::memcpy(&h, &bits, 2);
					double v = h;
					if (y > x ? (v > x && v < next) : (v < x && v > next)) {
						next = v;
					}
				}
				// a step from a subnormal to zero keeps the sign
				return next == 0 ? std::copysign(0.0, x) : next;
			};

			for (bool half: { false, true }) {
				std::vector<int32_t> bits;
				std::vector<double> x;
				encode(values, half, bits, x);
				std::vector<Expected> want;
				auto next = [&](double a, double b) { return half ? nextafterHalf(a, b) : static_cast<double>(std::nextafter(static_cast<float>(a), static_cast<float>(b))); };
				for (size_t i = 0; i < x.size(); ++i) {
					double y = x[(i + 8) % x.size()];
					want.push_back({ std::asinh(x[i]), false });
					want.push_back({ std::acosh(toType(std::fabs(x[i]) + 1, half)), false });
					want.push_back({ std::atanh(x[i] / 16), false });
					want.push_back({ std::pow(10.0, x[i] / 4), false });
					want.push_back({ std::log10(std::fabs(x[i])), false });
					want.push_back({ std::copysign(x[i], y), true });
					want.push_back({ std::sin(x[i]), false });
					want.push_back({ std::cos(x[i]), false });
					want.push_back({ next(x[i], y), true });
					want.push_back({ next(x[i], 0), true });
					want.push_back({ next(-0.0, x[i]), true });
					want.push_back({ x[i], true });
					want.push_back({ std::copysign(INFINITY, x[i]), true });
				}
				checkFloat(device.get(), queue.get(), library.get(), half ? "functions_f16" : "functions_f32", bits, want, half, half ? 0x1p-8 : 0x1p-16, std::string("asinh, acosh, atanh (and its poles), exp10, log10, copysign, sincos and nextafter of ") + (half ? "half" : "float"));
			}

			for (bool half: { false, true }) {
				// thread i scales x[i] by 2^k[i] (as a scalar and as the first lane of a vector) and by 2^-k[i] (the second lane), for
				// i < 8; threads 8 to 15 scale the ks, as floating-point values, by the bits of the xs, which are large exponents
				// as float, 0x1.000802p-126 * 2^-11 is rounded once to 0x1.001p-137, the subnormal with bits 0x00001001; as half,
				// 65504 * 2^-40 is the smallest subnormal and 65504 * 2^-41 rounds to zero
				std::vector<double> xs { 0x1p-20, 0.25, -3.0, 1.5, 0x1p-130, -0.0, 1e30, 0x1.000802p-126 };
				std::vector<int32_t> ks { 129, 17, -140, 127, 200, 5, -300, -11 };
				if (half) {
					xs = { 0x1p-20, 65504.0, -3.0, 1.5, 65504.0, -0.0, 1e30, 0.25 };
					ks = { 129, -40, -140, 127, -41, 5, -300, 17 };
				}
				std::vector<int32_t> bits;
				std::vector<double> x;
				encode(xs, half, bits, x);
				bits.insert(bits.end(), ks.begin(), ks.end());
				std::vector<Expected> want;
				for (size_t i = 0; i < bits.size(); ++i) {
					double v;
					int32_t k = bits[(i + 8) % bits.size()];
					// the ks are subnormals as floating-point values, and so is one of the xs as float
					bool subnormal = i >= 8 || (x[i] != 0 && std::fabs(x[i]) < (half ? 0x1p-14 : 0x1p-126));
					if (i < 8) {
						v = x[i];
					} else if (half) {
						_Float16 h;
						uint16_t b = static_cast<uint16_t>(bits[i]);
						std::memcpy(&h, &b, 2);
						v = h;
					} else {
						float f;
						std::memcpy(&f, &bits[i], 4);
						v = f;
					}
					// the exponents only matter up to where every result is zero or infinite
					auto clamp = [](int32_t n) { return std::max(-400, std::min(400, n)); };
					// -k as the kernel computes it, wrapping around
					int32_t negated = static_cast<int32_t>(0u - static_cast<uint32_t>(k));
					want.push_back({ std::ldexp(v, clamp(k)), true, subnormal });
					want.push_back({ std::ldexp(v, clamp(k)), true, subnormal });
					want.push_back({ std::ldexp(v, clamp(negated)), true, subnormal });
				}
				checkFloat(device.get(), queue.get(), library.get(), half ? "ldexp_f16" : "ldexp_f32", bits, want, half, 0, std::string("ldexp of ") + (half ? "half and <2 x half>" : "float and <2 x float>") + " with exponents beyond those of normal numbers");
			}

			for (bool half: { false, true }) {
				// thread i < 8 steps from xs[i] towards ys[i], and thread i + 8 from ys[i] towards xs[i]: from and to zeros of
				// both signs and subnormals, to and from infinities, and NaNs
				double subnormal = half ? 0x1p-21 : 0x1.4p-146;
				double largest = half ? 65504.0 : static_cast<double>(FLT_MAX);
				double smallest = half ? 0x1p-24 : 0x1p-149;
				std::vector<double> xs { 0.0, 0.0, INFINITY, NAN, 1.0, largest, -0.0, -smallest };
				std::vector<double> ys { subnormal, -0.0, 0.0, 1.0, NAN, INFINITY, 0.0, 0.0 };
				std::vector<int32_t> bits;
				std::vector<double> x;
				encode(xs, half, bits, x);
				encode(ys, half, bits, x);
				std::vector<Expected> want;
				for (size_t i = 0; i < x.size(); ++i) {
					double from = x[i], to = x[(i + 8) % x.size()];
					want.push_back({ half ? nextafterHalf(from, to) : static_cast<double>(std::nextafter(static_cast<float>(from), static_cast<float>(to))), true });
				}
				checkFloat(device.get(), queue.get(), library.get(), half ? "nextafter_f16" : "nextafter_f32", bits, want, half, 0, std::string("nextafter of ") + (half ? "half" : "float") + " zeros, subnormals, infinities and NaNs");
			}

			for (bool half: { false, true }) {
				std::vector<int32_t> bits;
				std::vector<double> x;
				encode(half ? piValuesHalf : piValues, half, bits, x);
				std::vector<Expected> want;
				for (double v: x) {
					// x = k + r with an integer k and |r| <= 1/2
					double k = std::nearbyint(v), r = v - k;
					bool odd = std::fmod(k, 2) != 0;
					if (!std::isfinite(v)) {
						want.insert(want.end(), { { NAN, false }, { NAN, false }, { NAN, false } });
						continue;
					}
					// exact at the integers and half-integers, with the signs IEEE 754 specifies for zeros and infinities
					bool exact = r == 0 || std::fabs(r) == 0.5;
					double sine = r == 0 ? std::copysign(0.0, v) : (odd ? -1 : 1) * (std::fabs(r) == 0.5 ? std::copysign(1.0, r) : std::sin(M_PI * r));
					double cosine = std::fabs(r) == 0.5 ? 0.0 : (odd ? -1 : 1) * std::cos(M_PI * r);
					double tangent = exact ? sine / cosine : std::tan(M_PI * r);
					want.push_back({ sine, exact });
					want.push_back({ cosine, exact });
					want.push_back({ tangent, exact });
				}
				checkFloat(device.get(), queue.get(), library.get(), half ? "pi_f16" : "pi_f32", bits, want, half, half ? 0x1p-10 : 0x1p-16, std::string("sinpi, cospi and tanpi of ") + (half ? "half" : "float"));
			}

			{
				std::vector<int32_t> bits;
				std::vector<double> x;
				encode(values, false, bits, x);
				std::vector<Expected> want;
				for (size_t i = 0; i < x.size(); ++i) {
					double y = x[(i + 8) % x.size()];
					double shifted = toType(x[i] + 0.5, false);
					auto cospi = [](double v) {
						double k = std::nearbyint(v), r = v - k;
						return std::fabs(r) == 0.5 ? 0.0 : (std::fmod(k, 2) != 0 ? -1 : 1) * std::cos(M_PI * r);
					};
					auto exact = [](double v) { double r = v - std::nearbyint(v); return r == 0 || std::fabs(r) == 0.5; };
					want.push_back({ cospi(x[i]), exact(x[i]) });
					want.push_back({ cospi(shifted), exact(shifted) });
					want.push_back({ std::copysign(x[i], y), true });
					want.push_back({ std::copysign(shifted, x[i]), true });
				}
				checkFloat(device.get(), queue.get(), library.get(), "vector_v2f32", bits, want, false, 0x1p-16, "cospi and copysign of <2 x float>");
			}
		}

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();
	return failures ? 1 : 0;
}
