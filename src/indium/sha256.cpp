// Copyright (C) 2026 Lubos Dolezel

#include <indium/sha256.private.hpp>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint32_t roundConstants[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotateRight(uint32_t value, unsigned count) {
	return (value >> count) | (value << (32 - count));
}

}

Indium::Sha256::Sha256() {
	static constexpr uint32_t initial[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	std::memcpy(_state, initial, sizeof(_state));
};

void Indium::Sha256::block(const uint8_t* bytes) {
	uint32_t w[64];
	for (size_t i = 0; i < 16; ++i) {
		w[i] = (uint32_t(bytes[i * 4]) << 24) | (uint32_t(bytes[i * 4 + 1]) << 16) | (uint32_t(bytes[i * 4 + 2]) << 8) | uint32_t(bytes[i * 4 + 3]);
	}
	for (size_t i = 16; i < 64; ++i) {
		uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];
	uint32_t e = _state[4], f = _state[5], g = _state[6], h = _state[7];
	for (size_t i = 0; i < 64; ++i) {
		uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
		uint32_t choose = (e & f) ^ (~e & g);
		uint32_t t1 = h + s1 + choose + roundConstants[i] + w[i];
		uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	_state[0] += a; _state[1] += b; _state[2] += c; _state[3] += d;
	_state[4] += e; _state[5] += f; _state[6] += g; _state[7] += h;
};

void Indium::Sha256::update(const void* data, size_t length) {
	auto bytes = static_cast<const uint8_t*>(data);
	_length += length;
	while (length > 0) {
		if (_buffered == 0 && length >= 64) {
			block(bytes);
			bytes += 64;
			length -= 64;
			continue;
		}
		size_t take = std::min<size_t>(64 - _buffered, length);
		std::memcpy(_buffer + _buffered, bytes, take);
		_buffered += take;
		bytes += take;
		length -= take;
		if (_buffered == 64) {
			block(_buffer);
			_buffered = 0;
		}
	}
};

Indium::Sha256Digest Indium::Sha256::finish() {
	uint64_t bitLength = _length * 8;
	uint8_t padding[72] = { 0x80 };
	size_t paddingLength = (_buffered < 56) ? (56 - _buffered) : (120 - _buffered);
	update(padding, paddingLength);
	uint8_t lengthBytes[8];
	for (size_t i = 0; i < 8; ++i) {
		lengthBytes[i] = static_cast<uint8_t>(bitLength >> (56 - i * 8));
	}
	update(lengthBytes, 8);

	Sha256Digest digest;
	for (size_t i = 0; i < 8; ++i) {
		digest[i * 4] = static_cast<uint8_t>(_state[i] >> 24);
		digest[i * 4 + 1] = static_cast<uint8_t>(_state[i] >> 16);
		digest[i * 4 + 2] = static_cast<uint8_t>(_state[i] >> 8);
		digest[i * 4 + 3] = static_cast<uint8_t>(_state[i]);
	}
	return digest;
};

Indium::Sha256Digest Indium::Sha256::of(const void* data, size_t length) {
	Sha256 hash;
	hash.update(data, length);
	return hash.finish();
};
