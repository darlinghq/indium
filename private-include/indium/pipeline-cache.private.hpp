// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include <indium/sha256.private.hpp>

namespace Indium {
	/**
	 * What a pipeline cache blob is valid for: the device and driver that produced it.
	 */
	struct PipelineCacheIdentity {
		uint32_t vendorID = 0;
		uint32_t deviceID = 0;
		uint32_t driverVersion = 0;
		std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUUID {};
		std::array<uint8_t, VK_UUID_SIZE> deviceUUID {};
	};

	/**
	 * A VkPipelineCache that outlives the process.
	 *
	 * The blob is kept in one file per device, driver and executable in the user's cache directory. The file
	 * is untrusted input to the driver, so it is only used when:
	 *  - the directory and the file belong to the current user and cannot be written by anyone else,
	 *  - the file is no larger than the size cap and its checksum matches,
	 *  - the identity in the file (vendor, device, driver version, cache UUID, device UUID) matches the device
	 *    and so does the identity in the blob's own Vulkan header.
	 * A file that fails any of this is deleted and the cache starts empty.
	 *
	 * Writing goes through a temporary file that is renamed over the old one, under an advisory lock, and
	 * merges what other processes saved since this one loaded, so two processes using the same file keep each
	 * other's pipelines. The file never grows past the size cap: a cache that would be bigger is not saved.
	 */
	class PipelineCache {
	public:
		enum class LoadStatus {
			NoFile,    // there was nothing to load (or persistence is off)
			Loaded,    // the blob was handed to the driver
			Rejected,  // the file failed validation and was removed
		};

		static constexpr size_t defaultMaxFileSize = 32 * 1024 * 1024;

		/**
		 * `directory` is where the file lives; it is created (owner-only) if missing. An empty directory, or one
		 * that cannot be made safe, gives a cache that is only kept in memory. `fileName` is the name inside it.
		 */
		PipelineCache(VkDevice device, const PipelineCacheIdentity& identity, const std::string& directory, const std::string& fileName, size_t maxFileSize = defaultMaxFileSize);
		~PipelineCache();

		PipelineCache(const PipelineCache&) = delete;
		PipelineCache& operator=(const PipelineCache&) = delete;

		VkPipelineCache handle() const { return _cache; };

		/**
		 * Pipelines were added to the cache. The file is written at most every couple of seconds, from the
		 * calling thread, and again when the cache is destroyed or the process exits.
		 */
		void noteChanged();

		/**
		 * Writes the file now if anything changed since the last write. Returns whether a file was written.
		 */
		bool save();

		LoadStatus loadStatus() const { return _loadStatus; };
		size_t loadedBytes() const { return _loadedBytes; };
		bool persistent() const { return !_path.empty(); };
		const std::string& path() const { return _path; };

		/**
		 * The directory the cache files go in: INDIUM_PIPELINE_CACHE_DIR if set ("" and "off" turn the disk
		 * cache off), else a directory under the user's cache directory (confstr(_CS_DARWIN_USER_CACHE_DIR) on
		 * Darwin, falling back to ~/Library/Caches; XDG_CACHE_HOME or ~/.cache elsewhere). Returns an empty string if there is none.
		 */
		static std::string defaultDirectory();

		/**
		 * The name of the file for a device and driver, used by one executable.
		 */
		static std::string fileNameFor(const PipelineCacheIdentity& identity, const std::string& executablePath);
		static std::string currentExecutablePath();

		/**
		 * Serializes the file format: the wrapper (checksum and identity) around the driver's blob.
		 */
		static std::vector<uint8_t> encodeFile(const PipelineCacheIdentity& identity, const uint8_t* blob, size_t blobSize);

		/**
		 * Checks a file's bytes and returns the blob they carry, or false if anything is wrong.
		 */
		static bool decodeFile(const PipelineCacheIdentity& identity, const uint8_t* bytes, size_t size, std::vector<uint8_t>& blob);

	private:
		VkDevice _device;
		PipelineCacheIdentity _identity;
		std::string _path;
		size_t _maxFileSize;
		VkPipelineCache _cache = VK_NULL_HANDLE;

		LoadStatus _loadStatus = LoadStatus::NoFile;
		size_t _loadedBytes = 0;

		std::mutex _mutex;
		bool _dirty = false;
		bool _housekeepingDone = false;
		Sha256Digest _diskDigest {};  // of the blob the file held when last read or written
		bool _haveDiskDigest = false;
		std::chrono::steady_clock::time_point _lastSave;

		bool readFile(std::vector<uint8_t>& blob, bool& rejected);
		bool getData(VkPipelineCache cache, std::vector<uint8_t>& blob);
		bool saveLocked();
		void removeStaleFiles();
	};
};
