// Copyright (C) 2026 Lubos Dolezel
//
// The pipeline cache that survives the process, and the translation cache:
//  - SHA-256 against the FIPS 180-4 example messages;
//  - the cache file format: every field of the identity, the checksum and the blob's own header are checked;
//  - a second process finds what the first one saved (a restart), and what it cannot trust is dropped:
//    other identity, truncated, bit-flipped, empty, oversized and writable-by-others files, a directory
//    others can write to, a link in place of the file;
//  - the size cap; processes saving at the same moment leave one valid file and no temporary files;
//  - a library seen twice is translated once.
// The processes are this executable run again with "child" as its first argument.

#include "compute-lib.h"

#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/pipeline-cache.private.hpp>
#include <indium/sha256.private.hpp>
#include <indium/translation-cache.private.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

std::string hexOf(const Indium::Sha256Digest& digest) {
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for (auto byte: digest) {
		out += digits[byte >> 4];
		out += digits[byte & 15];
	}
	return out;
}

std::string sha256Hex(const std::string& message) {
	return hexOf(Indium::Sha256::of(message.data(), message.size()));
}

std::vector<uint8_t> readAll(const std::string& path) {
	std::vector<uint8_t> bytes;
	if (auto file = std::fopen(path.c_str(), "rb")) {
		uint8_t buffer[4096];
		size_t count;
		while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
			bytes.insert(bytes.end(), buffer, buffer + count);
		}
		std::fclose(file);
	}
	return bytes;
}

void writeAll(const std::string& path, const std::vector<uint8_t>& bytes, mode_t mode = 0600) {
	unlink(path.c_str());
	if (auto file = std::fopen(path.c_str(), "wb")) {
		std::fwrite(bytes.data(), 1, bytes.size(), file);
		std::fclose(file);
		chmod(path.c_str(), mode);
	}
}

bool exists(const std::string& path) {
	struct stat info;
	return lstat(path.c_str(), &info) == 0;
}

std::vector<std::string> listDirectory(const std::string& path) {
	std::vector<std::string> names;
	if (auto dir = opendir(path.c_str())) {
		while (auto entry = readdir(dir)) {
			std::string name = entry->d_name;
			if (name != "." && name != "..") {
				names.push_back(name);
			}
		}
		closedir(dir);
	}
	return names;
}

// The one cache file in `directory`, or an empty string
std::string cacheFileIn(const std::string& directory) {
	std::string found;
	for (const auto& name: listDirectory(directory)) {
		if (name.size() > 6 && name.compare(name.size() - 6, 6, ".indpc") == 0) {
			found = directory + "/" + name;
		}
	}
	return found;
}

std::string temporaryName(const char* tag) {
	std::string pattern = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/indium-pc-" + tag + "-XXXXXX";
	std::vector<char> buffer(pattern.begin(), pattern.end());
	buffer.push_back('\0');
	return mkdtemp(buffer.data()) ? std::string(buffer.data()) : std::string();
}

void removeTree(const std::string& path) {
	for (const auto& name: listDirectory(path)) {
		std::string child = path + "/" + name;
		struct stat info;
		if (lstat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
			removeTree(child);
		} else {
			unlink(child.c_str());
		}
	}
	rmdir(path.c_str());
}

// ---- child ---------------------------------------------------------------------------------------------

// Creates a device, makes a compute pipeline for a threadgroup width, uses it, and shuts down the way an
// application that releases its device does (which saves the cache). Prints what the cache loaded.
// `startAt` is a time in milliseconds since the epoch to wait for before shutting down, so that several
// children save at the same moment.
int runChild(unsigned width, uint64_t startAt) {
	Indium::init(nullptr, 0, false);
	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			return 77;
		}
		auto queue = device->newCommandQueue();

		std::atomic<bool> polling { true };
		std::thread poller([&] {
			while (polling) {
				device->pollEvents(UINT64_MAX);
			}
		});

		auto library = device->newLibrary(compute_lib, compute_lib_len);
		auto state = device->newComputePipelineState(library->newFunction("add_arrays"));
		auto buffer = device->newBuffer(4096, Indium::ResourceOptions::StorageModeShared);
		auto commandBuffer = queue->commandBuffer();
		auto encoder = commandBuffer->computeCommandEncoder();
		encoder->setComputePipelineState(state);
		encoder->setBuffer(buffer, 0, 0);
		encoder->setBuffer(buffer, 0, 1);
		encoder->setBuffer(buffer, 0, 2);
		encoder->dispatchThreads(Indium::Size { 64, 1, 1 }, Indium::Size { width, 1, 1 });
		encoder->endEncoding();
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();

		auto& cache = std::dynamic_pointer_cast<Indium::PrivateDevice>(device)->pipelineCache();
		const char* status = cache.loadStatus() == Indium::PipelineCache::LoadStatus::Loaded ? "loaded" :
			cache.loadStatus() == Indium::PipelineCache::LoadStatus::Rejected ? "rejected" : "none";
		std::printf("status=%s bytes=%zu persistent=%d\n", status, cache.loadedBytes(), cache.persistent() ? 1 : 0);
		std::fflush(stdout);

		if (startAt) {
			auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			if (static_cast<uint64_t>(now) < startAt) {
				std::this_thread::sleep_for(std::chrono::milliseconds(startAt - now));
			}
		}

		polling = false;
		device->wakeupEventLoop();
		poller.join();
	}
	Indium::finit();
	std::printf("done\n");
	return 0;
}

struct ChildResult {
	bool ran = false;
	bool done = false;
	std::string status;
	size_t bytes = 0;
	bool persistent = false;
};

// Runs children started together; each gets its width and a shared start time.
std::vector<ChildResult> runChildren(const std::vector<unsigned>& widths, bool synchronized = false) {
	std::string executable = Indium::PipelineCache::currentExecutablePath();
	uint64_t startAt = 0;
	if (synchronized) {
		startAt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 1500;
	}

	std::vector<FILE*> pipes;
	for (auto width: widths) {
		std::string command = "exec '" + executable + "' child " + std::to_string(width) + " " + std::to_string(startAt) + " 2>&1";
		pipes.push_back(popen(command.c_str(), "r"));
	}

	std::vector<ChildResult> results;
	for (auto pipe: pipes) {
		ChildResult result;
		if (pipe) {
			char line[512];
			while (std::fgets(line, sizeof(line), pipe)) {
				std::string text = line;
				if (text.rfind("status=", 0) == 0) {
					result.ran = true;
					result.status = text.substr(7, text.find(' ') - 7);
					auto bytes = text.find("bytes=");
					result.bytes = std::strtoull(text.c_str() + bytes + 6, nullptr, 10);
					result.persistent = text.find("persistent=1") != std::string::npos;
				} else if (text.rfind("done", 0) == 0) {
					result.done = true;
				}
			}
			pclose(pipe);
		}
		results.push_back(result);
	}
	return results;
}

ChildResult runChild1(unsigned width) {
	return runChildren({ width })[0];
}

// ---- parent --------------------------------------------------------------------------------------------

void testSha256() {
	// FIPS 180-4 / NIST CSRC "SHA-256 example" messages
	report(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc");
	report(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of the empty message");
	report(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 of the two-block message");
	Indium::Sha256 millionA;
	std::string block(1000, 'a');
	for (int i = 0; i < 1000; ++i) {
		millionA.update(block.data(), block.size());
	}
	report(hexOf(millionA.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of a million a, fed in pieces");
}

Indium::PipelineCacheIdentity sampleIdentity() {
	Indium::PipelineCacheIdentity identity;
	identity.vendorID = 0x10de;
	identity.deviceID = 0x2504;
	identity.driverVersion = 0x1234;
	for (size_t i = 0; i < identity.pipelineCacheUUID.size(); ++i) {
		identity.pipelineCacheUUID[i] = static_cast<uint8_t>(i + 1);
		identity.deviceUUID[i] = static_cast<uint8_t>(0x80 + i);
	}
	return identity;
}

// What a driver's blob looks like: the Vulkan header followed by data
std::vector<uint8_t> sampleBlob(const Indium::PipelineCacheIdentity& identity, size_t extra = 64) {
	std::vector<uint8_t> blob(32 + extra, 0xab);
	uint32_t headerSize = 32, version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
	std::memcpy(&blob[0], &headerSize, 4);
	std::memcpy(&blob[4], &version, 4);
	std::memcpy(&blob[8], &identity.vendorID, 4);
	std::memcpy(&blob[12], &identity.deviceID, 4);
	std::memcpy(&blob[16], identity.pipelineCacheUUID.data(), 16);
	return blob;
}

void testFileFormat() {
	auto identity = sampleIdentity();
	auto blob = sampleBlob(identity);
	auto file = Indium::PipelineCache::encodeFile(identity, blob.data(), blob.size());

	std::vector<uint8_t> decoded;
	report(Indium::PipelineCache::decodeFile(identity, file.data(), file.size(), decoded) && decoded == blob, "a file decodes to the blob it was made from");

	auto rejects = [&](const char* what, const std::vector<uint8_t>& bytes, const Indium::PipelineCacheIdentity& expected) {
		std::vector<uint8_t> out;
		report(!Indium::PipelineCache::decodeFile(expected, bytes.data(), bytes.size(), out), what);
	};

	auto other = identity;
	other.vendorID ^= 1;
	rejects("another vendor ID is refused", file, other);
	other = identity;
	other.deviceID ^= 1;
	rejects("another device ID is refused", file, other);
	other = identity;
	other.driverVersion ^= 1;
	rejects("another driver version is refused", file, other);
	other = identity;
	other.pipelineCacheUUID[5] ^= 1;
	rejects("another pipeline cache UUID is refused", file, other);
	other = identity;
	other.deviceUUID[5] ^= 1;
	rejects("another device UUID is refused", file, other);

	auto flipped = file;
	flipped.back() ^= 1;
	rejects("a flipped payload bit is refused", flipped, identity);
	flipped = file;
	flipped[0] ^= 1;
	rejects("a wrong magic is refused", flipped, identity);
	rejects("a truncated file is refused", std::vector<uint8_t>(file.begin(), file.end() - 1), identity);
	rejects("a file shorter than its wrapper is refused", std::vector<uint8_t>(file.begin(), file.begin() + 40), identity);
	rejects("an empty file is refused", {}, identity);
	auto longer = file;
	longer.push_back(0);
	rejects("trailing bytes are refused", longer, identity);

	// the checksum is valid but the driver's own header belongs to another device
	auto foreign = blob;
	foreign[12] ^= 1;
	auto foreignFile = Indium::PipelineCache::encodeFile(identity, foreign.data(), foreign.size());
	rejects("a blob whose own header names another device is refused", foreignFile, identity);
	auto badVersion = blob;
	badVersion[4] = 2;
	auto badVersionFile = Indium::PipelineCache::encodeFile(identity, badVersion.data(), badVersion.size());
	rejects("a blob with an unknown header version is refused", badVersionFile, identity);
	std::vector<uint8_t> tiny(8, 0);
	auto tinyFile = Indium::PipelineCache::encodeFile(identity, tiny.data(), tiny.size());
	rejects("a blob shorter than the Vulkan header is refused", tinyFile, identity);
}

void testTranslationCache(Indium::Device* device) {
	auto& cache = Indium::TranslationCache::shared();
	size_t missesBefore = cache.misses(), hitsBefore = cache.hits(), entriesBefore = cache.entryCount();

	auto first = device->newLibrary(compute_lib, compute_lib_len);
	auto second = device->newLibrary(compute_lib, compute_lib_len);
	report(first && second, "a library is made twice from the same data");
	report(cache.misses() == missesBefore + 1 && cache.hits() == hitsBefore + 1, "the second library came from the translation cache");
	report(cache.entryCount() == entriesBefore + 1, "one entry holds both");
	report(first->newFunction("add_arrays") && second->newFunction("add_arrays"), "both libraries have the function");

	// the same library with one byte changed is another key; garbage is refused and not remembered
	std::vector<uint8_t> garbage(256, 0x5a);
	entriesBefore = cache.entryCount();
	report(device->newLibrary(garbage.data(), garbage.size()) == nullptr, "data that is not AIR gives no library");
	report(device->newLibrary(garbage.data(), garbage.size()) == nullptr, "and does so again");
	report(cache.entryCount() == entriesBefore, "a failed translation is not cached");

	// eviction: a small budget keeps the most recently used entries
	Indium::TranslationCache small(100);
	auto entry = [](size_t size) {
		auto made = std::make_shared<Indium::TranslationCache::Entry>();
		made->spirv.assign(size, 1);
		return std::shared_ptr<const Indium::TranslationCache::Entry>(made);
	};
	auto keyA = Indium::Sha256::of("a", 1), keyB = Indium::Sha256::of("b", 1), keyC = Indium::Sha256::of("c", 1);
	small.insert(keyA, entry(40));
	small.insert(keyB, entry(40));
	small.find(keyA);
	small.insert(keyC, entry(40));
	report(small.find(keyA) && small.find(keyC) && !small.find(keyB), "the least recently used entry is evicted first");
	report(small.insert(Indium::Sha256::of("big", 3), entry(1000)) != nullptr && small.entryCount() == 2, "an entry above the budget is returned but not kept");
}

void testDirectCache(Indium::PrivateDevice& device) {
	auto directory = temporaryName("direct");
	auto identity = sampleIdentity();
	VkPhysicalDeviceProperties properties = device.properties();
	identity.vendorID = properties.vendorID;
	identity.deviceID = properties.deviceID;
	identity.driverVersion = properties.driverVersion;
	std::copy(std::begin(properties.pipelineCacheUUID), std::end(properties.pipelineCacheUUID), identity.pipelineCacheUUID.begin());
	identity.deviceUUID = device.deviceUUID();

	{
		Indium::PipelineCache cache(device.device(), identity, directory, "a.indpc");
		report(cache.persistent() && cache.loadStatus() == Indium::PipelineCache::LoadStatus::NoFile, "a new cache has no file to load");
		report(!cache.save(), "nothing changed, so nothing is written");
		cache.noteChanged();
		report(cache.save() && exists(directory + "/a.indpc"), "a changed cache is written");
	}

	{
		Indium::PipelineCache cache(device.device(), identity, directory, "a.indpc");
		report(cache.loadStatus() == Indium::PipelineCache::LoadStatus::Loaded && cache.loadedBytes() >= 32, "a cache loads what the last one saved");
	}

	// another identity's file under this name is dropped, whatever its checksum says
	{
		auto other = identity;
		other.deviceID ^= 0x100;
		auto blob = sampleBlob(other);
		writeAll(directory + "/b.indpc", Indium::PipelineCache::encodeFile(other, blob.data(), blob.size()));
		Indium::PipelineCache cache(device.device(), identity, directory, "b.indpc");
		report(cache.loadStatus() == Indium::PipelineCache::LoadStatus::Rejected && !exists(directory + "/b.indpc"), "a cache file of another device is dropped");
		report(cache.handle() != VK_NULL_HANDLE, "and the cache still works");
	}

	// a cache that would pass the size cap is not saved
	{
		Indium::PipelineCache cache(device.device(), identity, directory, "c.indpc", 16);
		cache.noteChanged();
		report(!cache.save() && !exists(directory + "/c.indpc"), "a file above the size cap is not written");
	}
	{
		auto blob = sampleBlob(identity, 4096);
		writeAll(directory + "/d.indpc", Indium::PipelineCache::encodeFile(identity, blob.data(), blob.size()));
		Indium::PipelineCache cache(device.device(), identity, directory, "d.indpc", 1024);
		report(cache.loadStatus() == Indium::PipelineCache::LoadStatus::Rejected && !exists(directory + "/d.indpc"), "a file above the size cap is dropped, not read");
	}

	// an unusable location is memory only
	{
		Indium::PipelineCache cache(device.device(), identity, "", "e.indpc");
		cache.noteChanged();
		report(!cache.persistent() && !cache.save() && cache.handle() != VK_NULL_HANDLE, "without a directory the cache is kept in memory");
	}
	{
		auto open = temporaryName("open");
		chmod(open.c_str(), 0777);
		Indium::PipelineCache cache(device.device(), identity, open, "f.indpc");
		report(!cache.persistent(), "a directory others can write to is not used");
		removeTree(open);
	}
	{
		auto elsewhere = temporaryName("link");
		std::string link = directory + "/link-dir";
		symlink(elsewhere.c_str(), link.c_str());
		Indium::PipelineCache cache(device.device(), identity, link, "g.indpc");
		report(!cache.persistent(), "a link in place of the directory is not used");
		removeTree(elsewhere);
	}

	removeTree(directory);
}

void testRestart(const std::string& directory) {
	setenv("INDIUM_PIPELINE_CACHE_DIR", directory.c_str(), 1);
	removeTree(directory);

	auto first = runChild1(64);
	report(first.ran && first.done && first.persistent && first.status == "none", "the first run starts without a cache file");
	auto file = cacheFileIn(directory);
	auto saved = readAll(file);
	report(!file.empty() && saved.size() > 96, "the first run saved a cache file");

	auto second = runChild1(64);
	report(second.done && second.status == "loaded" && second.bytes > 0 && second.bytes == saved.size() - 96, "a restart loads the saved blob");

	// the file is only ever read and written by its owner
	struct stat info;
	stat(file.c_str(), &info);
	report((info.st_mode & 0777) == 0600, "the cache file is owner-only");
	stat(directory.c_str(), &info);
	report((info.st_mode & 0777) == 0700, "the cache directory is owner-only");

	// each way a file can be wrong is dropped, the process carries on, and the next run has a good file again
	auto expectRejected = [&](const char* what, const std::vector<uint8_t>& bytes, mode_t mode = 0600) {
		writeAll(file, bytes, mode);
		auto result = runChild1(64);
		report(result.done && result.status == "rejected", what);
		auto after = runChild1(64);
		report(after.done && after.status == "loaded", "  the cache file was rewritten");
	};

	auto good = saved;
	auto truncated = good;
	truncated.resize(good.size() / 2);
	expectRejected("a truncated cache file is ignored", truncated);
	auto flipped = good;
	flipped[flipped.size() - 10] ^= 0x40;
	expectRejected("a cache file with a flipped bit is ignored", flipped);
	auto otherVendor = good;
	otherVendor[12] ^= 1;
	expectRejected("a cache file made for another vendor is ignored", otherVendor);
	auto otherDriver = good;
	otherDriver[20] ^= 1;
	expectRejected("a cache file made for another driver version is ignored", otherDriver);
	expectRejected("an empty cache file is ignored", {});
	expectRejected("a cache file of garbage is ignored", std::vector<uint8_t>(good.size(), 0x5a));
	expectRejected("a cache file writable by the group is ignored", good, 0660);
	std::vector<uint8_t> huge(Indium::PipelineCache::defaultMaxFileSize + 1000, 0);
	expectRejected("a cache file above the size cap is ignored", huge);

	// a link in the file's place is never followed
	{
		auto target = temporaryName("target");
		writeAll(target + "/real", good);
		unlink(file.c_str());
		symlink((target + "/real").c_str(), file.c_str());
		auto result = runChild1(64);
		report(result.done && result.status != "loaded", "a link in place of the cache file is not followed");
		removeTree(target);
		unlink(file.c_str());
		runChild1(64);
	}

	// a directory others can write to turns the disk cache off, and the run still works
	chmod(directory.c_str(), 0777);
	auto open = runChild1(64);
	report(open.done && !open.persistent, "a directory others can write to is not used");
	chmod(directory.c_str(), 0700);
}

void testConcurrentWriters(const std::string& directory) {
	setenv("INDIUM_PIPELINE_CACHE_DIR", directory.c_str(), 1);
	removeTree(directory);

	runChild1(64);
	auto single = readAll(cacheFileIn(directory)).size();
	removeTree(directory);

	// the grid is 64 threads wide, so the widths are divisors of it; two processes make the same pipeline
	std::vector<unsigned> widths = { 1, 2, 4, 8, 16, 32, 64, 64 };
	auto results = runChildren(widths, true);
	bool allDone = true;
	for (const auto& result: results) {
		allDone = allDone && result.ran && result.done;
	}
	report(allDone, "eight processes using one cache file all finish");

	bool noTemporaries = true;
	for (const auto& name: listDirectory(directory)) {
		noTemporaries = noTemporaries && name.find(".tmp.") == std::string::npos;
	}
	report(noTemporaries, "no temporary file is left behind");

	auto after = runChild1(8);
	auto size = readAll(cacheFileIn(directory)).size();
	report(after.done && after.status == "loaded", "the file they left is valid");
	report(size >= single, "the file holds at least what one process saved");
}

}

int main(int argc, char** argv) {
	if (argc >= 4 && std::strcmp(argv[1], "child") == 0) {
		return runChild(static_cast<unsigned>(std::atoi(argv[2])), std::strtoull(argv[3], nullptr, 10));
	}

	testSha256();
	testFileFormat();

	// this process's own device keeps its cache apart from the files the other tests make
	auto own = temporaryName("own");
	setenv("INDIUM_PIPELINE_CACHE_DIR", own.c_str(), 1);

	Indium::init(nullptr, 0, false);
	{
		auto device = Indium::createSystemDefaultDevice();
		if (!device) {
			std::printf("no device\n");
			return 77;
		}
		device->newCommandQueue();
		testTranslationCache(device.get());
		testDirectCache(*std::dynamic_pointer_cast<Indium::PrivateDevice>(device));
	}
	Indium::finit();

	auto restart = temporaryName("restart");
	testRestart(restart);
	auto concurrent = temporaryName("concurrent");
	testConcurrentWriters(concurrent);

	removeTree(own);
	removeTree(restart);
	removeTree(concurrent);
	std::printf("%u failure(s)\n", failures);
	return failures ? 1 : 0;
}
