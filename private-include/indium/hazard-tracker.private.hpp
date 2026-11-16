// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <indium/types.hpp>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace Indium {
	/**
	 * Where a resource lives, for hazard tracking: a byte range of a device memory object. Two resources
	 * conflict when their ranges overlap, which is how texture views (the same image), textures made over
	 * a buffer's memory and placement heap aliases show up as one resource without anybody listing them.
	 *
	 * A resource whose memory is not known (an image of a swapchain, say) is named by its image handle and
	 * covers all of it.
	 */
	struct HazardRange {
		uint64_t object = 0;
		bool imageHandle = false;
		uint64_t begin = 0;
		// exclusive
		uint64_t end = 0;

		bool valid() const { return object != 0 && end > begin; }

		static HazardRange memory(VkDeviceMemory memory, uint64_t begin, uint64_t end) {
			return HazardRange { reinterpret_cast<uint64_t>(memory), false, begin, end };
		}
		static HazardRange image(VkImage image) {
			return HazardRange { reinterpret_cast<uint64_t>(image), true, 0, UINT64_MAX };
		}
	};

	/**
	 * The hazard tracking mode a resource has: what it was created with, or, for the default, what its heap
	 * says, or tracked when it has no heap.
	 */
	inline HazardTrackingMode resolveHazardTrackingMode(ResourceOptions options, HazardTrackingMode inherited) {
		switch ((static_cast<size_t>(options) >> 8) & 0xf) {
			case 1:
				return HazardTrackingMode::HazardTrackingModeUntracked;
			case 2:
				return HazardTrackingMode::HazardTrackingModeTracked;
			default:
				return inherited == HazardTrackingMode::HazardTrackingModeDefault ? HazardTrackingMode::HazardTrackingModeTracked : inherited;
		}
	}

	/**
	 * Works out the synchronization the commands of one command buffer need between each other from what they
	 * access, instead of ending every encoder with a barrier on everything.
	 *
	 * Callers say what the commands they are about to record access (use()), and the tracker adds the barriers
	 * that make that safe against what earlier commands of the command buffer did: a read waits for the last
	 * write of the memory (read-after-write), a write waits for the reads since the last write
	 * (write-after-read) and for the last write (write-after-write). Reads never wait for each other. The
	 * barriers are collected and recorded by flush() before the commands, or taken with take() to be recorded
	 * in front of a render pass that is already open.
	 *
	 * The tracker only knows the commands of its own command buffer. Memory it has not seen yet may have
	 * been written or read by any earlier command buffer of the queue, so the first use of a range gets a
	 * barrier from everything that came before, narrowed to the stages and accesses of that use; one such
	 * barrier covers every later first use with the same or a weaker access.
	 *
	 * Only resources whose hazard tracking mode is tracked are given to a tracker. What an untracked resource
	 * needs is ordered by the application, with fences, events or explicit memory barriers.
	 */
	class HazardTracker {
	public:
		using Barriers = std::vector<VkMemoryBarrier2>;

		/** Accesses that change memory. */
		static constexpr VkAccessFlags2 writeAccesses =
			VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT |
			VK_ACCESS_2_MEMORY_WRITE_BIT;

		/**
		 * The commands recorded next access `range` with `access` at `stages`. Appends the barriers they need to
		 * the pending ones and records the access.
		 */
		void use(const HazardRange& range, VkPipelineStageFlags2 stages, VkAccessFlags2 access);

		/**
		 * The memory of `range` is fully ordered and visible to everything that follows, as after a barrier from
		 * all commands to all commands recorded by something else (the layout transition that ends a blit).
		 */
		void release(const HazardRange& range);

		/** Records the pending barriers into `commandBuffer`, in one command. */
		void flush(VkCommandBuffer commandBuffer);

		/** Hands over the pending barriers. */
		Barriers take();

		/** Whether barriers wait to be recorded. */
		bool hasPending() const { return !_pending.empty(); }

		/**
		 * A barrier from all commands to all commands has been recorded. Everything earlier is ordered before
		 * everything later, so nothing is outstanding any more.
		 */
		void noteFullBarrier();

		/**
		 * Whether commands were recorded since the last full barrier whose effects a later full barrier
		 * would still have to cover.
		 */
		bool dirty() const { return _dirty; }

		/**
		 * Accesses made until endUnorderedGroup() are not ordered against each other: they belong to commands
		 * (the dispatches of a concurrent compute encoder) that may run together, so only what came before
		 * the group is waited for. A barrier inside the group (memoryBarrier) is groupBarrier().
		 */
		void beginUnorderedGroup();
		void endUnorderedGroup();

		/**
		 * Orders what the group has accessed so far before the commands recorded next in it: records a barrier
		 * from those accesses to `stages` and `access`.
		 */
		void groupBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stages, VkAccessFlags2 access);

		/** Records a barrier from everything to everything and notes it. */
		void fullBarrier(VkCommandBuffer commandBuffer);

		/** Records `barriers` into `commandBuffer` in one command. */
		static void record(VkCommandBuffer commandBuffer, const Barriers& barriers);

	private:
		struct Access {
			VkPipelineStageFlags2 stages = 0;
			VkAccessFlags2 access = 0;
		};

		struct Entry {
			uint64_t end = 0;
			// the last write that is not known to be visible to everything yet
			VkPipelineStageFlags2 writeStages = 0;
			VkAccessFlags2 writeAccess = 0;
			// the reads since that write was ordered
			VkPipelineStageFlags2 readStages = 0;
			// the accesses the write has been made visible to
			std::vector<Access> visible;
			// the accesses that everything recorded so far is already ordered before (by a barrier inside an unordered group)
			std::vector<Access> ordered;
			// not seen by this command buffer yet: there may be writes and reads of earlier command buffers
			bool imported = false;
			// accesses of the unordered group that is open (zero for none)
			uint64_t group = 0;
			VkPipelineStageFlags2 groupReadStages = 0;
			VkPipelineStageFlags2 groupWriteStages = 0;
			VkAccessFlags2 groupWriteAccess = 0;
		};

		struct Key {
			uint64_t object;
			bool imageHandle;
			bool operator==(const Key& other) const { return object == other.object && imageHandle == other.imageHandle; }
		};
		struct KeyHash {
			size_t operator()(const Key& key) const { return std::hash<uint64_t>()(key.object) ^ (key.imageHandle ? 0x9e3779b97f4a7c15ull : 0); }
		};
		using Space = std::map<uint64_t, Entry>;

		// more entries than this in one memory object are merged into one, which only makes later uses wait for more
		static constexpr size_t maxEntriesPerSpace = 256;

		static bool covers(const std::vector<Access>& list, VkPipelineStageFlags2 stages, VkAccessFlags2 access);
		bool visibleTo(const Entry& entry, VkPipelineStageFlags2 stages, VkAccessFlags2 access) const;
		void addBarrier(VkPipelineStageFlags2 srcStages, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStages, VkAccessFlags2 dstAccess);
		void useEntry(Entry& entry, VkPipelineStageFlags2 stages, VkAccessFlags2 access);
		void foldGroup(Entry& entry, const Access* visibleTo);
		static void split(Space& space, uint64_t at);
		static void coalesce(Space& space, uint64_t begin, uint64_t end);
		static void collapse(Space& space);
		static bool sameState(const Entry& a, const Entry& b);

		std::unordered_map<Key, Space, KeyHash> _spaces;
		Barriers _pending;
		// what earlier command buffers' commands have been ordered against by a barrier of the first-use kind
		std::vector<Access> _importVisible;
		uint64_t _group = 0;
		uint64_t _nextGroup = 1;
		bool _dirty = true;
	};

	/**
	 * The accesses a render encoder's draws make, collected as they are recorded and given to the tracker when the
	 * encoder ends. Accesses to the same range are one, with the stages and accesses of all of them.
	 */
	class HazardAccessSet {
	public:
		void add(const HazardRange& range, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
			if (!range.valid() || !stages || !access) {
				return;
			}
			auto [it, inserted] = _index.emplace(Key { range.object, range.imageHandle, range.begin, range.end }, _items.size());
			if (inserted) {
				_items.push_back(Item { range, stages, access });
			} else {
				_items[it->second].stages |= stages;
				_items[it->second].access |= access;
			}
		}

		void applyTo(HazardTracker& tracker) const {
			for (const auto& item: _items) {
				tracker.use(item.range, item.stages, item.access);
			}
		}

		bool empty() const { return _items.empty(); }

	private:
		struct Item {
			HazardRange range;
			VkPipelineStageFlags2 stages;
			VkAccessFlags2 access;
		};
		struct Key {
			uint64_t object;
			bool imageHandle;
			uint64_t begin;
			uint64_t end;
			bool operator==(const Key& other) const {
				return object == other.object && imageHandle == other.imageHandle && begin == other.begin && end == other.end;
			}
		};
		struct KeyHash {
			size_t operator()(const Key& key) const {
				size_t hash = std::hash<uint64_t>()(key.object);
				hash = hash * 1099511628211ull ^ std::hash<uint64_t>()(key.begin);
				hash = hash * 1099511628211ull ^ std::hash<uint64_t>()(key.end);
				return hash ^ (key.imageHandle ? 0x9e3779b97f4a7c15ull : 0);
			}
		};

		std::unordered_map<Key, size_t, KeyHash> _index;
		std::vector<Item> _items;
	};
};
