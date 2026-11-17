// Copyright (C) 2026 Lubos Dolezel

#include <indium/pipeline-cache.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__APPLE__)
	#include <mach-o/dyld.h>
#endif

namespace {

constexpr char fileMagic[8] = { 'I', 'N', 'D', 'P', 'C', 'v', '1', '\0' };
constexpr size_t wrapperSize = 96;
constexpr size_t vulkanHeaderSize = 16 + VK_UUID_SIZE;
constexpr auto saveInterval = std::chrono::seconds(2);
constexpr time_t staleTemporaryAge = 60 * 60;
constexpr time_t staleCacheAge = 30 * 24 * 60 * 60;

void put32(uint8_t* out, uint32_t value) {
	for (int i = 0; i < 4; ++i) {
		out[i] = static_cast<uint8_t>(value >> (i * 8));
	}
}

void put64(uint8_t* out, uint64_t value) {
	for (int i = 0; i < 8; ++i) {
		out[i] = static_cast<uint8_t>(value >> (i * 8));
	}
}

uint32_t get32(const uint8_t* in) {
	uint32_t value = 0;
	for (int i = 0; i < 4; ++i) {
		value |= uint32_t(in[i]) << (i * 8);
	}
	return value;
}

uint64_t get64(const uint8_t* in) {
	uint64_t value = 0;
	for (int i = 0; i < 8; ++i) {
		value |= uint64_t(in[i]) << (i * 8);
	}
	return value;
}

std::string hex(const uint8_t* bytes, size_t count) {
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for (size_t i = 0; i < count; ++i) {
		out += digits[bytes[i] >> 4];
		out += digits[bytes[i] & 15];
	}
	return out;
}

// mkdir -p, owner-only for what it creates
bool makeDirectories(const std::string& path) {
	std::string partial;
	size_t position = 0;
	while (position <= path.size()) {
		size_t slash = path.find('/', position);
		if (slash == std::string::npos) {
			slash = path.size();
		}
		partial = path.substr(0, slash);
		if (!partial.empty() && mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
			return false;
		}
		position = slash + 1;
	}
	return true;
}

// The directory must be a real directory (not a link) that only this user can change.
bool directoryIsPrivate(const std::string& path) {
	struct stat info;
	if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
		return false;
	}
	return info.st_uid == geteuid() && (info.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

// All the registered caches are flushed when the process exits, for applications that never release their device.
std::mutex& registryMutex() {
	static auto* mutex = new std::mutex;
	return *mutex;
}

std::vector<Indium::PipelineCache*>& registry() {
	static auto* caches = new std::vector<Indium::PipelineCache*>;
	return *caches;
}

void flushAll() {
	std::scoped_lock lock(registryMutex());
	for (auto cache: registry()) {
		cache->save();
	}
}

void registerCache(Indium::PipelineCache* cache) {
	std::scoped_lock lock(registryMutex());
	static bool hooked = [] {
		std::atexit(flushAll);
		return true;
	}();
	(void)hooked;
	registry().push_back(cache);
}

void unregisterCache(Indium::PipelineCache* cache) {
	std::scoped_lock lock(registryMutex());
	auto& caches = registry();
	caches.erase(std::remove(caches.begin(), caches.end(), cache), caches.end());
}

}

std::string Indium::PipelineCache::defaultDirectory() {
	if (auto override = getenv("INDIUM_PIPELINE_CACHE_DIR")) {
		std::string value = override;
		if (value.empty() || value == "off") {
			return "";
		}
		return value;
	}

	std::string root;
#if defined(_CS_DARWIN_USER_CACHE_DIR)
	char buffer[1024];
	size_t length = confstr(_CS_DARWIN_USER_CACHE_DIR, buffer, sizeof(buffer));
	if (length > 1 && length <= sizeof(buffer)) {
		root = buffer;
		while (root.size() > 1 && root.back() == '/') {
			root.pop_back();
		}
	}
#endif
	if (root.empty()) {
		auto home = getenv("HOME");
#if defined(__APPLE__)
		// confstr has no answer when the system has no per-user cache directory for this process
		if (home && home[0] == '/') {
			root = std::string(home) + "/Library/Caches";
		}
#else
		if (auto xdg = getenv("XDG_CACHE_HOME"); xdg && xdg[0] == '/') {
			root = xdg;
		} else if (home && home[0] == '/') {
			root = std::string(home) + "/.cache";
		}
#endif
	}
	if (root.empty()) {
		return "";
	}
	return root + "/org.darlinghq.indium/pipelines";
};

std::string Indium::PipelineCache::currentExecutablePath() {
#if defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	std::string path(size, '\0');
	if (_NSGetExecutablePath(path.data(), &size) != 0) {
		return "";
	}
	path.resize(strlen(path.c_str()));
	return path;
#else
	char buffer[4096];
	ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (length <= 0) {
		return "";
	}
	return std::string(buffer, length);
#endif
};

std::string Indium::PipelineCache::fileNameFor(const PipelineCacheIdentity& identity, const std::string& executablePath) {
	Sha256 hash;
	auto word = [&hash](uint32_t value) {
		uint8_t bytes[4];
		put32(bytes, value);
		hash.update(bytes, 4);
	};
	word(identity.vendorID);
	word(identity.deviceID);
	word(identity.driverVersion);
	hash.update(identity.pipelineCacheUUID.data(), identity.pipelineCacheUUID.size());
	hash.update(identity.deviceUUID.data(), identity.deviceUUID.size());
	hash.update(executablePath.data(), executablePath.size());
	auto digest = hash.finish();
	return hex(digest.data(), 16) + ".indpc";
};

std::vector<uint8_t> Indium::PipelineCache::encodeFile(const PipelineCacheIdentity& identity, const uint8_t* blob, size_t blobSize) {
	std::vector<uint8_t> out(wrapperSize + blobSize);
	memcpy(out.data(), fileMagic, 8);
	put32(&out[8], wrapperSize);
	put32(&out[12], identity.vendorID);
	put32(&out[16], identity.deviceID);
	put32(&out[20], identity.driverVersion);
	memcpy(&out[24], identity.pipelineCacheUUID.data(), VK_UUID_SIZE);
	memcpy(&out[40], identity.deviceUUID.data(), VK_UUID_SIZE);
	put64(&out[56], blobSize);
	auto digest = Sha256::of(blob, blobSize);
	memcpy(&out[64], digest.data(), digest.size());
	if (blobSize) {
		memcpy(&out[wrapperSize], blob, blobSize);
	}
	return out;
};

bool Indium::PipelineCache::decodeFile(const PipelineCacheIdentity& identity, const uint8_t* bytes, size_t size, std::vector<uint8_t>& blob) {
	if (size < wrapperSize || memcmp(bytes, fileMagic, 8) != 0 || get32(bytes + 8) != wrapperSize) {
		return false;
	}
	if (get32(bytes + 12) != identity.vendorID || get32(bytes + 16) != identity.deviceID || get32(bytes + 20) != identity.driverVersion) {
		return false;
	}
	if (memcmp(bytes + 24, identity.pipelineCacheUUID.data(), VK_UUID_SIZE) != 0 || memcmp(bytes + 40, identity.deviceUUID.data(), VK_UUID_SIZE) != 0) {
		return false;
	}
	uint64_t payloadSize = get64(bytes + 56);
	if (payloadSize != size - wrapperSize || payloadSize < vulkanHeaderSize) {
		return false;
	}
	const uint8_t* payload = bytes + wrapperSize;
	auto digest = Sha256::of(payload, payloadSize);
	if (memcmp(digest.data(), bytes + 64, digest.size()) != 0) {
		return false;
	}

	// the driver's own header: length, version (1), vendor, device and cache UUID
	uint32_t headerLength = 0, headerVersion = 0, vendor = 0, device = 0;
	memcpy(&headerLength, payload, 4);
	memcpy(&headerVersion, payload + 4, 4);
	memcpy(&vendor, payload + 8, 4);
	memcpy(&device, payload + 12, 4);
	if (headerLength < vulkanHeaderSize || headerLength > payloadSize || headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
		return false;
	}
	if (vendor != identity.vendorID || device != identity.deviceID || memcmp(payload + 16, identity.pipelineCacheUUID.data(), VK_UUID_SIZE) != 0) {
		return false;
	}

	blob.assign(payload, payload + payloadSize);
	return true;
};

Indium::PipelineCache::PipelineCache(VkDevice device, const PipelineCacheIdentity& identity, const std::string& directory, const std::string& fileName, size_t maxFileSize):
	_device(device),
	_identity(identity),
	_maxFileSize(maxFileSize),
	_lastSave(std::chrono::steady_clock::now())
{
	if (!directory.empty() && makeDirectories(directory) && directoryIsPrivate(directory)) {
		_path = directory + "/" + fileName;
	}

	std::vector<uint8_t> blob;
	if (persistent()) {
		bool rejected = false;
		_loadStatus = readFile(blob, rejected) ? LoadStatus::Loaded : (rejected ? LoadStatus::Rejected : LoadStatus::NoFile);
	}

	VkPipelineCacheCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	if (_loadStatus == LoadStatus::Loaded) {
		info.initialDataSize = blob.size();
		info.pInitialData = blob.data();
	}
	if (DynamicVK::vkCreatePipelineCache(_device, &info, nullptr, &_cache) != VK_SUCCESS) {
		// whatever the driver objected to is not worth keeping
		if (_loadStatus == LoadStatus::Loaded) {
			_loadStatus = LoadStatus::Rejected;
			unlink(_path.c_str());
			_haveDiskDigest = false;
			info.initialDataSize = 0;
			info.pInitialData = nullptr;
			if (DynamicVK::vkCreatePipelineCache(_device, &info, nullptr, &_cache) != VK_SUCCESS) {
				_cache = VK_NULL_HANDLE;
			}
		} else {
			_cache = VK_NULL_HANDLE;
		}
	}
	if (_loadStatus == LoadStatus::Loaded) {
		_loadedBytes = blob.size();
		_diskDigest = Sha256::of(blob.data(), blob.size());
		_haveDiskDigest = true;
	}

	if (persistent()) {
		registerCache(this);
	}
};

Indium::PipelineCache::~PipelineCache() {
	if (persistent()) {
		unregisterCache(this);
		save();
	}
	if (_cache) {
		DynamicVK::vkDestroyPipelineCache(_device, _cache, nullptr);
	}
};

// Returns true if a valid file was read. A file that is there but not valid is deleted and
// reported through `rejected`.
bool Indium::PipelineCache::readFile(std::vector<uint8_t>& blob, bool& rejected) {
	rejected = false;
	int fd = open(_path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		return false;
	}

	bool valid = false;
	struct stat info;
	if (fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & (S_IWGRP | S_IWOTH)) == 0 &&
		info.st_size >= static_cast<off_t>(wrapperSize) && static_cast<uint64_t>(info.st_size) <= _maxFileSize + wrapperSize) {
		std::vector<uint8_t> bytes(info.st_size);
		size_t done = 0;
		while (done < bytes.size()) {
			ssize_t count = read(fd, bytes.data() + done, bytes.size() - done);
			if (count < 0 && errno == EINTR) {
				continue;
			}
			if (count <= 0) {
				break;
			}
			done += count;
		}
		valid = done == bytes.size() && decodeFile(_identity, bytes.data(), bytes.size(), blob);
	}
	close(fd);

	if (!valid) {
		unlink(_path.c_str());
		rejected = true;
		return false;
	}
	// a file in use is not stale
	utimes(_path.c_str(), nullptr);
	return true;
};

bool Indium::PipelineCache::getData(VkPipelineCache cache, std::vector<uint8_t>& blob) {
	for (int attempt = 0; attempt < 3; ++attempt) {
		size_t size = 0;
		if (DynamicVK::vkGetPipelineCacheData(_device, cache, &size, nullptr) != VK_SUCCESS) {
			return false;
		}
		blob.resize(size);
		VkResult result = DynamicVK::vkGetPipelineCacheData(_device, cache, &size, blob.data());
		if (result == VK_SUCCESS) {
			blob.resize(size);
			return true;
		}
		if (result != VK_INCOMPLETE) {
			return false;
		}
	}
	return false;
};

void Indium::PipelineCache::noteChanged() {
	std::scoped_lock lock(_mutex);
	_dirty = true;
	if (persistent() && std::chrono::steady_clock::now() - _lastSave >= saveInterval) {
		saveLocked();
	}
};

bool Indium::PipelineCache::save() {
	std::scoped_lock lock(_mutex);
	return saveLocked();
};

bool Indium::PipelineCache::saveLocked() {
	if (!_dirty || !persistent() || !_cache) {
		return false;
	}
	_dirty = false;
	_lastSave = std::chrono::steady_clock::now();

	int lockFd = open((_path + ".lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (lockFd < 0) {
		return false;
	}
	while (flock(lockFd, LOCK_EX) != 0 && errno == EINTR) {
	}

	bool written = false;
	do {
		// pick up what other processes saved since this one read the file
		std::vector<uint8_t> diskBlob;
		bool rejected = false;
		if (readFile(diskBlob, rejected)) {
			auto digest = Sha256::of(diskBlob.data(), diskBlob.size());
			if (!_haveDiskDigest || digest != _diskDigest) {
				VkPipelineCacheCreateInfo info {};
				info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				info.initialDataSize = diskBlob.size();
				info.pInitialData = diskBlob.data();
				VkPipelineCache other = VK_NULL_HANDLE;
				if (DynamicVK::vkCreatePipelineCache(_device, &info, nullptr, &other) == VK_SUCCESS) {
					DynamicVK::vkMergePipelineCaches(_device, _cache, 1, &other);
					DynamicVK::vkDestroyPipelineCache(_device, other, nullptr);
				}
			}
		}

		std::vector<uint8_t> blob;
		if (!getData(_cache, blob) || blob.size() < vulkanHeaderSize || blob.size() > _maxFileSize) {
			break;
		}
		auto bytes = encodeFile(_identity, blob.data(), blob.size());

		static std::atomic<uint32_t> counter { 0 };
		std::string temporary = _path + ".tmp." + std::to_string(getpid()) + "." + std::to_string(counter++);
		int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd < 0) {
			break;
		}
		size_t done = 0;
		bool ok = true;
		while (done < bytes.size()) {
			ssize_t count = write(fd, bytes.data() + done, bytes.size() - done);
			if (count < 0 && errno == EINTR) {
				continue;
			}
			if (count <= 0) {
				ok = false;
				break;
			}
			done += count;
		}
		ok = (close(fd) == 0) && ok;
		if (!ok || rename(temporary.c_str(), _path.c_str()) != 0) {
			unlink(temporary.c_str());
			break;
		}
		_diskDigest = Sha256::of(blob.data(), blob.size());
		_haveDiskDigest = true;
		written = true;
	} while (false);

	flock(lockFd, LOCK_UN);
	close(lockFd);

	if (written && !_housekeepingDone) {
		_housekeepingDone = true;
		removeStaleFiles();
	}
	return written;
};

// Temporary files left by a process that died mid-write, and files for devices, drivers or
// executables that have not been used for a month.
void Indium::PipelineCache::removeStaleFiles() {
	auto slash = _path.rfind('/');
	std::string directory = _path.substr(0, slash);
	std::string ownName = _path.substr(slash + 1);

	DIR* dir = opendir(directory.c_str());
	if (!dir) {
		return;
	}
	time_t now = time(nullptr);
	while (auto entry = readdir(dir)) {
		std::string name = entry->d_name;
		if (name == ownName) {
			continue;
		}
		bool temporary = name.find(".indpc.tmp.") != std::string::npos;
		bool cacheFile = name.size() > 6 && name.compare(name.size() - 6, 6, ".indpc") == 0;
		if (!temporary && !cacheFile) {
			continue;
		}
		struct stat info;
		if (fstatat(dirfd(dir), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode)) {
			continue;
		}
		if (now - info.st_mtime > (temporary ? staleTemporaryAge : staleCacheAge)) {
			unlinkat(dirfd(dir), name.c_str(), 0);
		}
	}
	closedir(dir);
};
