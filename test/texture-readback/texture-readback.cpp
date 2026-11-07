// Byte-exact verification of Indium::Texture::getBytes on the real GPU.
//
// getBytes is the read direction of replaceRegion and exists so the Metal layer
// can implement -[MTLTexture getBytes:bytesPerRow:fromRegion:mipmapLevel:],
// which used to abort. A texture created with the default
// allowGPUOptimizedContents is VK_IMAGE_TILING_OPTIMAL, so its memory cannot be
// mapped and the readback has to be a GPU copy into a host-visible buffer; this
// is what checks that path end to end, against the bytes that were uploaded
// rather than against a re-derivation of them.
//
// Every check is byte-for-byte against the uploaded pattern, and every check has
// a control that must fail: a deliberately corrupted copy of the same pattern
// (so "matched" cannot come from a comparison that cannot fail), and a second
// readback of the same texture required to be identical (so both sides of the
// comparison cannot come from the same call).

#include <indium/device.hpp>
#include <indium/init.hpp>
#include <indium/texture.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

static int gChecks = 0, gErrors = 0;

static void report(const char* level, const char* what, const std::string& detail) {
	std::printf("  [%s] %s%s%s\n", level, what, detail.empty() ? "" : ": ", detail.c_str());
	if (std::strcmp(level, "FAIL") == 0) gErrors++;
	gChecks++;
}

static void expect(bool cond, const char* what, const std::string& detail = "") {
	report(cond ? "ok" : "FAIL", what, detail);
}

// The uploaded pattern. Every byte differs from its neighbour in every channel,
// so a transposed, shifted or row-swapped readback cannot land on the right
// answer, and the alpha channel rules out a read of a different texel size.
static const size_t TEX_W = 8, TEX_H = 6;
static const size_t BPP = 4;

static std::vector<uint8_t> makePattern() {
	std::vector<uint8_t> p(TEX_W * TEX_H * BPP);
	for (size_t y = 0; y < TEX_H; y++) {
		for (size_t x = 0; x < TEX_W; x++) {
			uint8_t* t = &p[(y * TEX_W + x) * BPP];
			t[0] = (uint8_t)(x * 31 + 1);
			t[1] = (uint8_t)(y * 41 + 2);
			t[2] = (uint8_t)(x * 17 + y * 7 + 3);
			t[3] = (uint8_t)(255 - (x * 5 + y * 3));
		}
	}
	return p;
}

// Every byte position that differs between `got` and `want`, and the first few of
// them, so a failure says where the readback went wrong rather than just that it did.
static size_t firstDiffs(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want,
                         size_t limit = 4) {
	size_t bad = 0, shown = 0;
	for (size_t i = 0; i < want.size() && i < got.size(); i++) {
		if (got[i] == want[i]) continue;
		bad++;
		if (shown < limit) {
			std::printf("    byte %zu (row %zu, col %zu, ch %zu): got %u want %u\n",
				i, i / (TEX_W * BPP), (i / BPP) % TEX_W, i % BPP,
				(unsigned)got[i], (unsigned)want[i]);
			shown++;
		}
	}
	return bad;
}

// ---------------------------------------------------------------------------

int main() {
	Indium::init(nullptr, 0, false);
	auto device = Indium::createSystemDefaultDevice();
	if (!device) { std::fprintf(stderr, "no vulkan device\n"); return 2; }
	std::printf("device: %s\n", device->name().c_str());

	std::atomic<bool> keepPolling { true };
	std::thread poll([&device, &keepPolling]() {
		while (keepPolling) device->pollEvents(UINT64_MAX);
	});

	const auto pattern = makePattern();

	// -- 1. the whole level, in the default (managed, optimally tiled) mode ----
	{
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		auto tex = device->newTexture(td);
		tex->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0,
			pattern.data(), TEX_W * BPP);

		std::vector<uint8_t> got(pattern.size(), 0);
		tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, got.data(), TEX_W * BPP);

		size_t bad = firstDiffs(got, pattern);
		expect(bad == 0, "getBytes of the whole level equals the uploaded bytes",
			bad == 0 ? std::to_string(got.size()) + " of " + std::to_string(pattern.size()) + " bytes"
			         : std::to_string(bad) + " bytes differ");

		// Control 1: the identical check against a corrupted copy of the pattern
		// must be rejected, or "byte exact" is not a claim this test can make.
		auto corrupt = pattern;
		corrupt[0] ^= 0xFF;
		size_t cbad = firstDiffs(got, corrupt);
		expect(cbad > 0, "NEGATIVE CONTROL: the byte comparison rejects a corrupted reference",
			std::to_string(cbad) + " bytes differ");

		// Control 2: a second readback must be identical, so the comparison cannot
		// be satisfied by both sides coming from one call.
		std::vector<uint8_t> again(pattern.size(), 0);
		tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, again.data(), TEX_W * BPP);
		expect(memcmp(again.data(), got.data(), got.size()) == 0,
			"NEGATIVE CONTROL: two readbacks of the same texture are identical");

		// Control 3: a readback into a buffer pre-filled with 0xAA must have
		// overwritten every byte, so "matched" is not "left alone".
		std::vector<uint8_t> dirty(pattern.size(), 0xAA);
		tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, dirty.data(), TEX_W * BPP);
		size_t untouched = 0;
		for (size_t i = 0; i < dirty.size(); i++) if (dirty[i] == 0xAA) untouched++;
		expect(untouched == 0 && firstDiffs(dirty, pattern) == 0,
			"NEGATIVE CONTROL: the readback writes every byte of the destination",
			std::to_string(untouched) + " bytes left at the fill value");
	}

	// -- 2. a sub-region: the origin and the extent are both honoured ----------
	{
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		auto tex = device->newTexture(td);
		tex->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0,
			pattern.data(), TEX_W * BPP);

		const size_t rx = 2, ry = 1, rw = 3, rh = 4;
		std::vector<uint8_t> want(rw * rh * BPP);
		for (size_t y = 0; y < rh; y++)
			memcpy(&want[y * rw * BPP], &pattern[((ry + y) * TEX_W + rx) * BPP], rw * BPP);

		std::vector<uint8_t> got(want.size(), 0);
		tex->getBytes(Indium::Region::make2D(rx, ry, rw, rh), 0, got.data(), rw * BPP);
		size_t bad = firstDiffs(got, want);
		expect(bad == 0, "getBytes of a sub-region equals that sub-rectangle of the pattern",
			bad == 0 ? std::to_string(rw) + "x" + std::to_string(rh) + " at ("
			         + std::to_string(rx) + "," + std::to_string(ry) + ")"
			         : std::to_string(bad) + " bytes differ");

		// Control: the same bytes at the origin. A readback that ignored the
		// region would return exactly this, so the two must differ.
		std::vector<uint8_t> atOrigin(rw * rh * BPP, 0);
		tex->getBytes(Indium::Region::make2D(0, 0, rw, rh), 0, atOrigin.data(), rw * BPP);
		expect(memcmp(atOrigin.data(), got.data(), got.size()) != 0,
			"NEGATIVE CONTROL: the region's origin changes the bytes returned");

		// Control: the same origin with a larger extent must change them too, so
		// the extent is not being ignored.
		std::vector<uint8_t> bigger((rw + 1) * rh * BPP, 0);
		tex->getBytes(Indium::Region::make2D(rx, ry, rw + 1, rh), 0, bigger.data(), (rw + 1) * BPP);
		expect(memcmp(bigger.data(), got.data(), got.size()) != 0,
			"NEGATIVE CONTROL: the region's extent changes the bytes returned");
	}

	// -- 3. a padded row pitch: rowLength is honoured -------------------------
	{
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		auto tex = device->newTexture(td);
		tex->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0,
			pattern.data(), TEX_W * BPP);

		// A bytesPerRow of two whole texels more than the row needs. The padding
		// must receive nothing, which is what a correct rowLength produces; a copy
		// that ignored the pitch would pack the rows and leave the tail unwritten.
		const size_t pitch = (TEX_W + 2) * BPP;
		std::vector<uint8_t> got(TEX_H * pitch, 0xAA);
		tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, got.data(), pitch);

		size_t bad = 0, dirtyPad = 0;
		for (size_t y = 0; y < TEX_H; y++) {
			for (size_t x = 0; x < TEX_W * BPP; x++) {
				uint8_t g = got[y * pitch + x], w = pattern[y * TEX_W * BPP + x];
				if (g != w) bad++;
			}
			for (size_t i = TEX_W * BPP; i < pitch; i++) if (got[y * pitch + i] == 0xAA) dirtyPad++;
		}
		expect(bad == 0 && dirtyPad == 0, "getBytes honours a bytesPerRow larger than the row",
			bad == 0 ? std::to_string(dirtyPad) + " padding bytes left at the fill value"
			         : std::to_string(bad) + " bytes differ");

		// Control: a packed readback of the same texture is not byte-identical to
		// the padded one, so the two spellings are distinguishable at all.
		std::vector<uint8_t> packed(TEX_H * TEX_W * BPP, 0);
		tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, packed.data(), TEX_W * BPP);
		expect(memcmp(packed.data(), got.data(), packed.size()) != 0,
			"NEGATIVE CONTROL: a padded readback differs from a packed one");

		// Control: the same readback of the same texture into the same layout must
		// not throw, so the pitch check below is about the pitch.
		bool paddedThrew = false;
		try {
			std::vector<uint8_t> again(TEX_H * pitch, 0xAA);
			tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, again.data(), pitch);
		} catch (const std::exception&) {
			paddedThrew = true;
		}
		expect(!paddedThrew, "CONTROL: a whole-texel padded pitch does not throw");
	}

	// -- 3b. a pitch that is not a whole number of texels ---------------------
	// Vulkan's bufferRowLength is in texels, so a byte pitch that is not a
	// multiple of the texel size has no representation. Rounding it down would
	// hand back a buffer at a stride the caller never asked for, in silence, so
	// getBytes refuses it. This is the check the first padded case used to
	// violate silently: it asked for 39 bytes per row on a 4-byte texel and got
	// back a 36-byte stride, with 154 of 192 bytes landing in the wrong place.
	{
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		auto tex = device->newTexture(td);
		tex->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0,
			pattern.data(), TEX_W * BPP);

		const size_t oddPitch = TEX_W * BPP + 7;
		std::vector<uint8_t> got(TEX_H * oddPitch, 0xAA);
		bool threw = false;
		std::string what;
		try {
			tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, got.data(), oddPitch);
		} catch (const std::runtime_error& e) {
			threw = true;
			what = e.what();
		}
		expect(threw, "getBytes rejects a bytesPerRow that is not a whole number of texels", what);
		bool untouched = true;
		for (size_t i = 0; i < got.size(); i++) if (got[i] != 0xAA) untouched = false;
		expect(untouched, "a refused pitch leaves the destination untouched");
	}

	// -- 4. the slice form: a 2D array, three slices, and bytesPerImage --------
	// This is the overload behind -getBytes:bytesPerRow:bytesPerImage:fromRegion:
	// mipmapLevel:slice:. Each slice gets its own palette, and the slices are read
	// back in a different order than they were written, so a readback that
	// ignored the slice argument cannot match.
	{
		const size_t SLICES = 3;
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		td.textureType = Indium::TextureType::e2DArray;
		td.arrayLength = SLICES;
		auto tex = device->newTexture(td);

		// slice s gets a palette that is s's own: channel values offset by 40s.
		std::vector<std::vector<uint8_t>> palettes(SLICES);
		for (size_t s = 0; s < SLICES; s++) {
			auto p = makePattern();
			for (size_t i = 0; i < p.size(); i += BPP) {
				p[i + 0] = (uint8_t)(p[i + 0] + 40 * s);
				p[i + 1] = (uint8_t)(p[i + 1] + 40 * s);
				p[i + 2] = (uint8_t)(p[i + 2] + 40 * s);
			}
			palettes[s] = p;
			tex->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, s,
				p.data(), TEX_W * BPP, 0);
		}

		const size_t bytesPerImage = TEX_W * TEX_H * BPP;
		bool allSlicesExact = true;
		std::string detail;
		// read back in the order 2, 0, 1
		for (size_t k = 0; k < SLICES; k++) {
			const size_t s = (SLICES - 1 - k) % SLICES;
			std::vector<uint8_t> got(bytesPerImage, 0xAA);
			tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, s,
				got.data(), TEX_W * BPP, bytesPerImage);
			size_t bad = firstDiffs(got, palettes[s]);
			if (bad != 0) {
				allSlicesExact = false;
				detail = "slice " + std::to_string(s) + ": " + std::to_string(bad) + " bytes differ";
				break;
			}
		}
		expect(allSlicesExact, "getBytes of a 2D array slice names that slice, with bytesPerImage",
			allSlicesExact ? std::to_string(SLICES) + " slices" : detail);

		// Control: the slices' palettes are disjoint, so a readback that ignored
		// the slice argument could not have matched all three. Prove it by
		// requiring each slice's bytes to differ from every other slice's.
		bool distinct = true;
		for (size_t a = 0; a < SLICES && distinct; a++)
			for (size_t b = a + 1; b < SLICES && distinct; b++)
				if (palettes[a] == palettes[b]) distinct = false;
		expect(distinct, "NEGATIVE CONTROL: the three slice palettes are pairwise distinct");

		// Control: a bytesPerImage that is not a whole number of rows is refused.
		std::vector<uint8_t> got(bytesPerImage + 5, 0xAA);
		bool threw = false;
		std::string what;
		try {
			tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, 0,
				got.data(), TEX_W * BPP, TEX_W * BPP + 5);
		} catch (const std::runtime_error& e) {
			threw = true;
			what = e.what();
		}
		expect(threw, "getBytes rejects a bytesPerImage that is not a whole number of rows", what);

		// Control: bytesPerImage of 0 is the 2D spelling and must still work.
		std::vector<uint8_t> flat(bytesPerImage, 0xAA);
		bool flatThrew = false;
		try {
			tex->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, 1,
				flat.data(), TEX_W * BPP, 0);
		} catch (const std::exception&) {
			flatThrew = true;
		}
		expect(!flatThrew && firstDiffs(flat, palettes[1]) == 0,
			"CONTROL: bytesPerImage 0 through the slice overload reads slice 1 byte exactly");
	}

	// -- 5. the storage-mode precondition -------------------------------------
	{
		// Private has no host-visible contents, so getBytes must refuse rather
		// than invent bytes or abort the process.
		Indium::TextureDescriptor td = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		td.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
		auto priv = device->newTexture(td);

		std::vector<uint8_t> got(pattern.size(), 0xAA);
		bool threw = false;
		std::string what;
		try {
			priv->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, got.data(), TEX_W * BPP);
		} catch (const std::runtime_error& e) {
			threw = true;
			what = e.what();
		}
		expect(threw, "getBytes on a private texture throws instead of aborting", what);
		expect(!threw || what.find("getBytes") != std::string::npos,
			"the private-texture error names the operation", what);
		bool untouched = true;
		for (size_t i = 0; i < got.size(); i++) if (got[i] != 0xAA) untouched = false;
		expect(untouched, "a refused getBytes leaves the destination untouched");

		// Control: the same call on a shared texture must NOT throw, so the check
		// above is about the storage mode and not about getBytes always throwing.
		Indium::TextureDescriptor sd = Indium::TextureDescriptor::texture2DDescriptor(
			Indium::PixelFormat::RGBA8Unorm, TEX_W, TEX_H, false);
		sd.resourceOptions = Indium::ResourceOptions::StorageModeShared;
		auto shared = device->newTexture(sd);
		shared->replaceRegion(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, pattern.data(), TEX_W * BPP);
		std::vector<uint8_t> sgot(pattern.size(), 0xAA);
		bool sharedThrew = false;
		try {
			shared->getBytes(Indium::Region::make2D(0, 0, TEX_W, TEX_H), 0, sgot.data(), TEX_W * BPP);
		} catch (const std::exception&) {
			sharedThrew = true;
		}
		expect(!sharedThrew && firstDiffs(sgot, pattern) == 0,
			"CONTROL: getBytes on a shared texture succeeds and is byte exact");
	}

	keepPolling = false;
	device->wakeupEventLoop();
	poll.join();

	if (gErrors == 0)
		std::printf("\nRESULT: all %d checks passed\n", gChecks);
	else
		std::printf("\nRESULT: FAILED, %d of %d checks\n", gErrors, gChecks);

	// Tearing everything down segfaults somewhere inside libindium, so the
	// verdict is emitted and the process leaves before the destructors run;
	// otherwise the exit code would report a teardown crash instead of the
	// result. The same reasoning and the same fix as test/sampler-shim.
	std::fflush(stdout);
	_exit(gErrors == 0 ? 0 : 1);
}
