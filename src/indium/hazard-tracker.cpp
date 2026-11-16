// Copyright (C) 2026 Lubos Dolezel
#include <indium/hazard-tracker.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>

namespace {
	constexpr VkPipelineStageFlags2 allCommands = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	constexpr VkAccessFlags2 memoryRead = VK_ACCESS_2_MEMORY_READ_BIT;
	constexpr VkAccessFlags2 memoryWrite = VK_ACCESS_2_MEMORY_WRITE_BIT;
}

bool Indium::HazardTracker::covers(const std::vector<Access>& list, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
	for (const auto& entry: list) {
		if ((stages & ~entry.stages) == 0 && (access & ~entry.access) == 0) {
			return true;
		}
	}
	return false;
};

bool Indium::HazardTracker::visibleTo(const Entry& entry, VkPipelineStageFlags2 stages, VkAccessFlags2 access) const {
	return covers(entry.visible, stages, access);
};

void Indium::HazardTracker::addBarrier(VkPipelineStageFlags2 srcStages, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStages, VkAccessFlags2 dstAccess) {
	// barriers between the same stages are one barrier with the accesses of both
	for (auto& barrier: _pending) {
		if (barrier.srcStageMask == srcStages && barrier.dstStageMask == dstStages) {
			barrier.srcAccessMask |= srcAccess;
			barrier.dstAccessMask |= dstAccess;
			return;
		}
	}
	VkMemoryBarrier2 barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStages;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStages;
	barrier.dstAccessMask = dstAccess;
	_pending.push_back(barrier);
};

void Indium::HazardTracker::split(Space& space, uint64_t at) {
	auto it = space.upper_bound(at);
	if (it == space.begin()) {
		return;
	}
	--it;
	if (it->first < at && it->second.end > at) {
		Entry tail = it->second;
		it->second.end = at;
		space.emplace(at, std::move(tail));
	}
};

bool Indium::HazardTracker::sameState(const Entry& a, const Entry& b) {
	return a.writeStages == b.writeStages && a.writeAccess == b.writeAccess && a.readStages == b.readStages &&
		a.imported == b.imported && a.group == b.group && a.groupReadStages == b.groupReadStages &&
		a.groupWriteStages == b.groupWriteStages && a.groupWriteAccess == b.groupWriteAccess && a.visible.size() == b.visible.size() &&
		a.ordered.size() == b.ordered.size() &&
		std::equal(a.ordered.begin(), a.ordered.end(), b.ordered.begin(), [](const Access& x, const Access& y) {
			return x.stages == y.stages && x.access == y.access;
		}) &&
		std::equal(a.visible.begin(), a.visible.end(), b.visible.begin(), [](const Access& x, const Access& y) {
			return x.stages == y.stages && x.access == y.access;
		});
};

void Indium::HazardTracker::coalesce(Space& space, uint64_t begin, uint64_t end) {
	// merge entries of equal state around the range that was just changed
	auto it = space.lower_bound(begin);
	if (it != space.begin()) {
		--it;
	}
	while (it != space.end() && it->first <= end) {
		auto next = std::next(it);
		if (next != space.end() && it->second.end == next->first && sameState(it->second, next->second)) {
			it->second.end = next->second.end;
			space.erase(next);
			continue;
		}
		it = next;
	}
};

void Indium::HazardTracker::collapse(Space& space) {
	// one entry for everything: it keeps every outstanding access, so later uses can only wait for more
	Entry merged;
	merged.end = space.rbegin()->second.end;
	uint64_t begin = space.begin()->first;
	for (const auto& [start, entry]: space) {
		merged.writeStages |= entry.writeStages;
		merged.writeAccess |= entry.writeAccess;
		merged.readStages |= entry.readStages;
		merged.imported = merged.imported || entry.imported;
		merged.group = std::max(merged.group, entry.group);
		merged.groupReadStages |= entry.groupReadStages;
		merged.groupWriteStages |= entry.groupWriteStages;
		merged.groupWriteAccess |= entry.groupWriteAccess;
		// only what every part was made visible to is visible for the whole
	}
	bool first = true;
	for (const auto& [start, entry]: space) {
		if (first) {
			merged.visible = entry.visible;
			first = false;
		} else {
			std::vector<Access> common;
			for (const auto& pair: entry.visible) {
				if (covers(merged.visible, pair.stages, pair.access)) {
					common.push_back(pair);
				}
			}
			merged.visible = std::move(common);
		}
	}
	// what is ordered is not known for the whole
	merged.ordered.clear();
	space.clear();
	space.emplace(begin, std::move(merged));
};

void Indium::HazardTracker::useEntry(Entry& entry, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
	bool write = (access & writeAccesses) != 0;
	bool grouped = _group != 0;

	if (write) {
		bool needInCommandBuffer = (entry.writeStages | entry.readStages) != 0 && !covers(entry.ordered, stages, access);
		bool needImport = entry.imported && !covers(_importVisible, stages, access);
		if (needImport) {
			addBarrier(allCommands, memoryWrite | entry.writeAccess, stages, access);
			_importVisible.push_back(Access { stages, access });
		} else if (needInCommandBuffer) {
			addBarrier(entry.writeStages | entry.readStages, entry.writeAccess, stages, access);
		}
		// everything before is ordered before this write
		entry.writeStages = 0;
		entry.writeAccess = 0;
		entry.readStages = 0;
		entry.visible.clear();
		entry.ordered.clear();
		entry.imported = false;
		if (grouped) {
			if (entry.group != _group) {
				entry.group = _group;
				entry.groupReadStages = 0;
				entry.groupWriteStages = 0;
				entry.groupWriteAccess = 0;
			}
			entry.groupWriteStages |= stages;
			entry.groupWriteAccess |= access;
		} else {
			entry.writeStages = stages;
			entry.writeAccess = access;
		}
	} else {
		bool needInCommandBuffer = entry.writeStages != 0 && !visibleTo(entry, stages, access);
		bool needImport = entry.imported && !covers(_importVisible, stages, access);
		if (needImport) {
			addBarrier(allCommands, memoryWrite | entry.writeAccess, stages, access);
			_importVisible.push_back(Access { stages, access });
			entry.visible.push_back(Access { stages, access });
		} else if (needInCommandBuffer) {
			addBarrier(entry.writeStages, entry.writeAccess, stages, access);
			entry.visible.push_back(Access { stages, access });
		}
		if (grouped) {
			if (entry.group != _group) {
				entry.group = _group;
				entry.groupReadStages = 0;
				entry.groupWriteStages = 0;
				entry.groupWriteAccess = 0;
			}
			entry.groupReadStages |= stages;
		} else {
			entry.readStages |= stages;
			entry.ordered.clear();
		}
	}
	_dirty = true;
};

void Indium::HazardTracker::use(const HazardRange& range, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
	if (!range.valid() || !stages || !access) {
		return;
	}

	auto& space = _spaces[Key { range.object, range.imageHandle }];
	split(space, range.begin);
	if (range.end != UINT64_MAX) {
		split(space, range.end);
	}

	uint64_t cursor = range.begin;
	auto it = space.lower_bound(range.begin);
	while (cursor < range.end) {
		if (it != space.end() && it->first == cursor) {
			useEntry(it->second, stages, access);
			cursor = it->second.end;
			++it;
		} else {
			// memory this command buffer has not seen: anything may have happened to it
			uint64_t gapEnd = it != space.end() ? std::min(it->first, range.end) : range.end;
			Entry fresh;
			fresh.end = gapEnd;
			fresh.imported = true;
			auto inserted = space.emplace_hint(it, cursor, std::move(fresh));
			useEntry(inserted->second, stages, access);
			cursor = gapEnd;
			it = std::next(inserted);
		}
	}

	if (space.size() > maxEntriesPerSpace) {
		collapse(space);
	} else {
		coalesce(space, range.begin, range.end);
	}
};

void Indium::HazardTracker::release(const HazardRange& range) {
	if (!range.valid()) {
		return;
	}
	auto spaceIt = _spaces.find(Key { range.object, range.imageHandle });
	if (spaceIt == _spaces.end()) {
		return;
	}
	auto& space = spaceIt->second;
	split(space, range.begin);
	if (range.end != UINT64_MAX) {
		split(space, range.end);
	}
	for (auto it = space.lower_bound(range.begin); it != space.end() && it->first < range.end; ++it) {
		auto& entry = it->second;
		entry.writeStages = 0;
		entry.writeAccess = 0;
		entry.readStages = 0;
		entry.visible.clear();
		entry.ordered.clear();
		// the barrier that released it ordered everything before it too
		entry.imported = false;
	}
	coalesce(space, range.begin, range.end);
};

void Indium::HazardTracker::record(VkCommandBuffer commandBuffer, const Barriers& barriers) {
	if (barriers.empty()) {
		return;
	}
	VkDependencyInfo info {};
	info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	info.memoryBarrierCount = barriers.size();
	info.pMemoryBarriers = barriers.data();
	DynamicVK::vkCmdPipelineBarrier2(commandBuffer, &info);
};

void Indium::HazardTracker::flush(VkCommandBuffer commandBuffer) {
	if (_pending.empty()) {
		return;
	}
	record(commandBuffer, _pending);
	_pending.clear();
};

Indium::HazardTracker::Barriers Indium::HazardTracker::take() {
	Barriers taken;
	taken.swap(_pending);
	return taken;
};

void Indium::HazardTracker::noteFullBarrier() {
	// nothing is outstanding, and memory seen for the first time later is ordered by the same barrier
	_spaces.clear();
	if (!covers(_importVisible, allCommands, memoryRead | memoryWrite)) {
		_importVisible.push_back(Access { allCommands, memoryRead | memoryWrite });
	}
	_group = 0;
	_dirty = false;
};

void Indium::HazardTracker::fullBarrier(VkCommandBuffer commandBuffer) {
	// what was waiting has to be recorded before it
	flush(commandBuffer);

	VkMemoryBarrier2 barrier {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	barrier.srcStageMask = allCommands;
	barrier.srcAccessMask = memoryWrite;
	barrier.dstStageMask = allCommands;
	barrier.dstAccessMask = memoryRead | memoryWrite;
	record(commandBuffer, { barrier });
	noteFullBarrier();
};

void Indium::HazardTracker::beginUnorderedGroup() {
	_group = _nextGroup++;
};

void Indium::HazardTracker::foldGroup(Entry& entry, const Access* madeVisibleTo) {
	if (entry.group == 0) {
		return;
	}
	if (entry.groupWriteStages) {
		entry.writeStages = entry.groupWriteStages;
		entry.writeAccess = entry.groupWriteAccess;
		entry.readStages = entry.groupReadStages;
		entry.visible.clear();
		entry.ordered.clear();
		if (madeVisibleTo) {
			entry.visible.push_back(*madeVisibleTo);
			entry.ordered.push_back(*madeVisibleTo);
		}
		entry.imported = false;
	} else {
		entry.readStages |= entry.groupReadStages;
		entry.ordered.clear();
	}
	entry.group = 0;
	entry.groupReadStages = 0;
	entry.groupWriteStages = 0;
	entry.groupWriteAccess = 0;
};

void Indium::HazardTracker::endUnorderedGroup() {
	if (_group == 0) {
		return;
	}
	for (auto& [key, space]: _spaces) {
		for (auto& [begin, entry]: space) {
			if (entry.group == _group) {
				foldGroup(entry, nullptr);
			}
		}
	}
	_group = 0;
};

void Indium::HazardTracker::groupBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stages, VkAccessFlags2 access) {
	if (_group == 0) {
		return;
	}
	flush(commandBuffer);

	VkPipelineStageFlags2 srcStages = 0;
	VkAccessFlags2 srcAccess = 0;
	for (auto& [key, space]: _spaces) {
		for (auto& [begin, entry]: space) {
			if (entry.group == _group) {
				srcStages |= entry.groupWriteStages | entry.groupReadStages;
				srcAccess |= entry.groupWriteAccess;
			}
		}
	}
	if (!srcStages) {
		return;
	}
	addBarrier(srcStages, srcAccess, stages, access);
	flush(commandBuffer);

	Access madeVisibleTo { stages, access };
	for (auto& [key, space]: _spaces) {
		for (auto& [begin, entry]: space) {
			if (entry.group == _group) {
				foldGroup(entry, &madeVisibleTo);
			}
		}
	}
	// what the encoder records next is a new group
	_group = _nextGroup++;
};
