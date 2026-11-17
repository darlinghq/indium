// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <iridium/iridium.hpp>
#include <indium/sha256.private.hpp>

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Indium {
	/**
	 * Remembers the SPIR-V that a library's AIR was translated into, in memory, keyed by the SHA-256 of the
	 * library data. Translation depends on nothing but those bytes (function constants are Vulkan
	 * specialization constants and do not change the module), so one entry serves every device and every
	 * specialization of the library. Entries are dropped least recently used first once the total size
	 * passes the budget.
	 */
	class TranslationCache {
	public:
		struct Entry {
			std::vector<uint8_t> spirv;
			Iridium::OutputInfo outputInfo;
		};

		explicit TranslationCache(size_t budgetBytes = 64 * 1024 * 1024);

		static TranslationCache& shared();

		std::shared_ptr<const Entry> find(const Sha256Digest& key);
		std::shared_ptr<const Entry> insert(const Sha256Digest& key, std::shared_ptr<const Entry> entry);

		size_t hits() const;
		size_t misses() const;
		size_t entryCount() const;

	private:
		struct Slot {
			std::shared_ptr<const Entry> entry;
			std::list<Sha256Digest>::iterator position;
		};

		mutable std::mutex _mutex;
		size_t _budget;
		size_t _size = 0;
		size_t _hits = 0;
		size_t _misses = 0;
		std::map<Sha256Digest, Slot> _slots;
		std::list<Sha256Digest> _recent;  // most recently used first
	};
};
