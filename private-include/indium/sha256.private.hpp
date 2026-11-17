// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Indium {
	using Sha256Digest = std::array<uint8_t, 32>;

	/**
	 * SHA-256 as specified in FIPS 180-4. Used where a collision must not go unnoticed: it keys the
	 * translation cache and checksums the pipeline cache file.
	 */
	class Sha256 {
	public:
		Sha256();
		void update(const void* data, size_t length);
		Sha256Digest finish();

		static Sha256Digest of(const void* data, size_t length);

	private:
		void block(const uint8_t* bytes);

		uint32_t _state[8];
		uint8_t _buffer[64];
		size_t _buffered = 0;
		uint64_t _length = 0;
	};
};
