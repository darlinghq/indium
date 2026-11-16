#pragma once

// Copyright (C) 2026 Lubos Dolezel

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>

namespace Indium {
	/**
	 * Hands out aligned ranges of one linear address space [0, size), first fit by address, and
	 * takes them back, merging neighbouring free ranges. It only does the bookkeeping; the memory it
	 * stands for belongs to the caller. Not thread-safe.
	 */
	class RangeAllocator {
	public:
		explicit RangeAllocator(uint64_t size): _size(size) {
			if (size != 0) {
				_free[0] = size;
			}
		}

		uint64_t size() const { return _size; }

		/** The offset of a free range of `size` bytes that starts at a multiple of `alignment`, now taken. */
		std::optional<uint64_t> allocate(uint64_t size, uint64_t alignment) {
			if (size == 0 || alignment == 0) {
				return std::nullopt;
			}
			for (auto it = _free.begin(); it != _free.end(); ++it) {
				uint64_t rangeStart = it->first;
				uint64_t rangeEnd = it->first + it->second;
				uint64_t start = alignUp(rangeStart, alignment);
				if (start > rangeEnd || rangeEnd - start < size) {
					continue;
				}
				_free.erase(it);
				if (start > rangeStart) {
					_free[rangeStart] = start - rangeStart;
				}
				if (start + size < rangeEnd) {
					_free[start + size] = rangeEnd - (start + size);
				}
				return start;
			}
			return std::nullopt;
		}

		/** Gives back a range that `allocate()` returned (the same offset and size). */
		void free(uint64_t offset, uint64_t size) {
			auto next = _free.lower_bound(offset);
			if (next != _free.begin()) {
				auto previous = std::prev(next);
				if (previous->first + previous->second == offset) {
					offset = previous->first;
					size += previous->second;
					_free.erase(previous);
				}
			}
			if (next != _free.end() && offset + size == next->first) {
				size += next->second;
				_free.erase(next);
			}
			_free[offset] = size;
		}

		/** The size of the biggest range `allocate()` could take at a multiple of `alignment` right now. */
		uint64_t largestAvailable(uint64_t alignment) const {
			if (alignment == 0) {
				alignment = 1;
			}
			uint64_t best = 0;
			for (const auto& [rangeStart, length]: _free) {
				uint64_t start = alignUp(rangeStart, alignment);
				uint64_t end = rangeStart + length;
				if (start < end) {
					best = std::max(best, end - start);
				}
			}
			return best;
		}

		/** Whether nothing is allocated. */
		bool isEmpty() const {
			return _free.size() == 1 && _free.begin()->second == _size;
		}

	private:
		static uint64_t alignUp(uint64_t value, uint64_t alignment) {
			return (value + alignment - 1) / alignment * alignment;
		}

		uint64_t _size;
		// offset -> size of each free range, coalesced
		std::map<uint64_t, uint64_t> _free;
	};
};
