// Copyright (C) 2026 Lubos Dolezel

#include <indium/translation-cache.private.hpp>

Indium::TranslationCache::TranslationCache(size_t budgetBytes):
	_budget(budgetBytes)
{};

Indium::TranslationCache& Indium::TranslationCache::shared() {
	static auto* cache = new TranslationCache;
	return *cache;
};

std::shared_ptr<const Indium::TranslationCache::Entry> Indium::TranslationCache::find(const Sha256Digest& key) {
	std::scoped_lock lock(_mutex);
	auto found = _slots.find(key);
	if (found == _slots.end()) {
		++_misses;
		return nullptr;
	}
	++_hits;
	_recent.splice(_recent.begin(), _recent, found->second.position);
	return found->second.entry;
};

std::shared_ptr<const Indium::TranslationCache::Entry> Indium::TranslationCache::insert(const Sha256Digest& key, std::shared_ptr<const Entry> entry) {
	std::scoped_lock lock(_mutex);
	if (auto found = _slots.find(key); found != _slots.end()) {
		// another thread translated the same library first
		_recent.splice(_recent.begin(), _recent, found->second.position);
		return found->second.entry;
	}

	size_t size = entry->spirv.size();
	if (size > _budget) {
		return entry;
	}
	_recent.push_front(key);
	_slots.emplace(key, Slot { entry, _recent.begin() });
	_size += size;

	while (_size > _budget && _recent.size() > 1) {
		auto oldest = _slots.find(_recent.back());
		_size -= oldest->second.entry->spirv.size();
		_slots.erase(oldest);
		_recent.pop_back();
	}
	return entry;
};

size_t Indium::TranslationCache::hits() const {
	std::scoped_lock lock(_mutex);
	return _hits;
};

size_t Indium::TranslationCache::misses() const {
	std::scoped_lock lock(_mutex);
	return _misses;
};

size_t Indium::TranslationCache::entryCount() const {
	std::scoped_lock lock(_mutex);
	return _slots.size();
};
