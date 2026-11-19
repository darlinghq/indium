// Copyright (C) 2026 Lubos Dolezel
//
// Heaps (Device::newHeap, Heap, the heap properties of resources):
//  - a heap is one device memory allocation, and the resources made from it take none of their own;
//  - an automatic heap places resources without overlap, counts them in usedSize() and maxAvailableSize(),
//    and takes their ranges back when they are destroyed or made aliasable;
//  - a placement heap places resources at the offsets given (aligned as the size queries say), and resources
//    that overlap share their memory;
//  - the size and alignment queries are the device's own requirements and agree with what the heap counts;
//  - refusals: sparse, Managed and Memoryless heaps, size 0, mismatching options, offsets of the wrong kind,
//    unaligned and out-of-range offsets, a full heap.
// Textures in a Shared heap need a memory type the device allows both buffers and images in; where there is
// none the test says so and skips those checks.

#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/device.private.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace Indium;

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

void note(const char* what) {
	std::printf("SKIP %s\n", what);
}

std::atomic<unsigned> memoryAllocations { 0 };
PFN_vkAllocateMemory originalAllocateMemory;
VKAPI_ATTR VkResult VKAPI_CALL countAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
	++memoryAllocations;
	return originalAllocateMemory(device, info, allocator, memory);
}

constexpr auto privateOptions = ResourceOptions::StorageModePrivate | ResourceOptions::CPUCacheModeDefaultCache;
constexpr auto sharedOptions = ResourceOptions::StorageModeShared | ResourceOptions::CPUCacheModeDefaultCache;

HeapDescriptor heapDescriptor(size_t size, StorageMode storageMode, HeapType type) {
	HeapDescriptor descriptor;
	descriptor.size = size;
	descriptor.storageMode = storageMode;
	descriptor.type = type;
	return descriptor;
}

TextureDescriptor textureDescriptor(size_t width, size_t height, ResourceOptions options) {
	auto descriptor = TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, width, height, false);
	descriptor.resourceOptions = options;
	descriptor.usage = TextureUsage::RenderTarget | TextureUsage::ShaderRead | TextureUsage::PixelFormatView;
	return descriptor;
}

bool isPowerOfTwo(size_t value) {
	return value != 0 && (value & (value - 1)) == 0;
}

size_t alignUp(size_t value, size_t alignment) {
	return (value + alignment - 1) / alignment * alignment;
}

// Clears the texture to a colour with a render pass.
void clear(CommandQueue& queue, const std::shared_ptr<Texture>& texture, double red) {
	RenderPassDescriptor pass {};
	RenderPassColorAttachmentDescriptor attachment {};
	attachment.texture = texture;
	attachment.loadAction = LoadAction::Clear;
	attachment.storeAction = StoreAction::Store;
	attachment.clearColor = ClearColor(red, 0, 1, 1);
	pass.colorAttachments.push_back(attachment);
	auto commandBuffer = queue.commandBuffer();
	commandBuffer->renderCommandEncoder(pass)->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

// The first pixel of a texture, copied to a buffer by the GPU.
uint32_t firstPixel(Device& device, CommandQueue& queue, const std::shared_ptr<Texture>& texture) {
	auto readback = device.newBuffer(256, sharedOptions);
	std::memset(readback->contents(), 0, 256);
	auto commandBuffer = queue.commandBuffer();
	auto blit = commandBuffer->blitCommandEncoder();
	blit->copy(texture, 0, 0, Origin { 0, 0, 0 }, Size { 1, 1, 1 }, readback, 0, 4, 4);
	blit->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	uint32_t pixel;
	std::memcpy(&pixel, readback->contents(), sizeof(pixel));
	return pixel;
}

void fillPattern(Buffer* buffer, uint8_t seed) {
	auto bytes = static_cast<uint8_t*>(buffer->contents());
	for (size_t i = 0; i < buffer->length(); ++i) {
		bytes[i] = static_cast<uint8_t>(seed + i * 7);
	}
}

bool hasPattern(Buffer* buffer, uint8_t seed) {
	auto bytes = static_cast<const uint8_t*>(buffer->contents());
	for (size_t i = 0; i < buffer->length(); ++i) {
		if (bytes[i] != static_cast<uint8_t>(seed + i * 7)) {
			return false;
		}
	}
	return true;
}

void testSizeAndAlign(Device& device) {
	auto limits = static_cast<PrivateDevice&>(device).properties().limits;

	size_t lengths[] = { 1, 16, 255, 1000, 4096, 100000 };
	bool ok = true;
	for (auto length: lengths) {
		auto sa = device.heapBufferSizeAndAlign(length, privateOptions);
		ok = ok && sa.size >= length && isPowerOfTwo(sa.align) && sa.size % limits.bufferImageGranularity == 0 && sa.align >= limits.bufferImageGranularity;
	}
	report(ok, "a buffer's size is at least its length and its alignment a power of two, both covering bufferImageGranularity");

	auto zero = device.heapBufferSizeAndAlign(0, privateOptions);
	report(zero.size == 0 && isPowerOfTwo(zero.align), "a zero-length buffer takes no size but has an alignment");

	auto shared = device.heapBufferSizeAndAlign(1000, sharedOptions);
	report(shared.align >= limits.nonCoherentAtomSize && shared.size % limits.nonCoherentAtomSize == 0, "a host-visible buffer is aligned to nonCoherentAtomSize");
	auto bigger = device.heapBufferSizeAndAlign(100000, privateOptions);
	auto smaller = device.heapBufferSizeAndAlign(1000, privateOptions);
	report(bigger.size > smaller.size, "a longer buffer takes more");

	auto small = device.heapTextureSizeAndAlign(textureDescriptor(8, 8, privateOptions));
	auto large = device.heapTextureSizeAndAlign(textureDescriptor(256, 256, privateOptions));
	report(small.size >= 8 * 8 * 4 && large.size >= 256 * 256 * 4 && large.size > small.size, "a texture takes at least its pixels, more when larger");
	report(isPowerOfTwo(small.align) && isPowerOfTwo(large.align), "a texture's alignment is a power of two");

	auto mipmapped = TextureDescriptor::texture2DDescriptor(PixelFormat::RGBA8Unorm, 256, 256, true);
	mipmapped.resourceOptions = privateOptions;
	mipmapped.usage = TextureUsage::ShaderRead;
	auto withMips = device.heapTextureSizeAndAlign(mipmapped);
	report(withMips.size > large.size, "mip levels add to a texture's size");

	auto invalid = textureDescriptor(8, 8, privateOptions);
	// a usage flag no texture has
	invalid.usage = static_cast<TextureUsage>(1u << 30);
	auto none = device.heapTextureSizeAndAlign(invalid);
	report(none.size == 0 && none.align == 0, "a texture the device cannot make has no size");
}

void testHeapCreation(const std::shared_ptr<Device>& deviceHandle) {
	Device& device = *deviceHandle;

	report(!device.newHeap(heapDescriptor(0, StorageMode::Private, HeapType::Automatic)), "a heap of size 0 is refused");
	report(!device.newHeap(heapDescriptor(1 << 20, StorageMode::Managed, HeapType::Automatic)), "a managed heap is refused");
	report(!device.newHeap(heapDescriptor(1 << 20, StorageMode::Memoryless, HeapType::Automatic)), "a memoryless heap is refused");
	report(!device.newHeap(heapDescriptor(1 << 20, StorageMode::Private, HeapType::Sparse)), "a sparse heap is refused");
	report(!device.newHeap(heapDescriptor(static_cast<size_t>(1) << 50, StorageMode::Private, HeapType::Automatic)), "a heap that cannot be allocated is refused");

	unsigned before = memoryAllocations;
	auto heap = device.newHeap(heapDescriptor(100000, StorageMode::Private, HeapType::Automatic));
	report(heap && memoryAllocations - before == 1, "a heap is one memory allocation");
	if (!heap) {
		return;
	}
	report(heap->size() >= 100000 && heap->size() < 100000 + 65536 && heap->size() % 4096 == 0, "the size is rounded up to whole pages");
	report(heap->currentAllocatedSize() == heap->size(), "the memory is allocated as soon as the heap exists");
	report(heap->usedSize() == 0 && heap->maxAvailableSize(1) == heap->size(), "a new automatic heap is empty");
	report(heap->type() == HeapType::Automatic && heap->storageMode() == StorageMode::Private && heap->cpuCacheMode() == CPUCacheMode::DefaultCache, "the heap reports its descriptor");
	report(heap->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeUntracked, "a default hazard tracking mode is untracked");
	report(heap->device() == deviceHandle, "the heap's device is the device");

	auto descriptor = heapDescriptor(65536, StorageMode::Shared, HeapType::Placement);
	descriptor.hazardTrackingMode = HazardTrackingMode::HazardTrackingModeTracked;
	descriptor.cpuCacheMode = CPUCacheMode::WriteCombined;
	auto placement = device.newHeap(descriptor);
	report(placement && placement->hazardTrackingMode() == HazardTrackingMode::HazardTrackingModeTracked && placement->cpuCacheMode() == CPUCacheMode::WriteCombined
		&& placement->type() == HeapType::Placement && placement->storageMode() == StorageMode::Shared, "a placement heap reports a tracked, write-combined, shared descriptor");
	report(placement && placement->usedSize() == 0 && placement->maxAvailableSize(1) == 0, "a placement heap counts nothing and has nothing available");
}

void testAutomaticHeap(Device& device, CommandQueue& queue) {
	unsigned before = memoryAllocations;
	auto heap = device.newHeap(heapDescriptor(1 << 20, StorageMode::Shared, HeapType::Automatic));
	report(heap != nullptr, "an automatic shared heap");
	if (!heap) {
		return;
	}
	auto saBuffer = device.heapBufferSizeAndAlign(1000, sharedOptions);

	auto a = heap->newBuffer(1000, sharedOptions);
	report(a && a->length() == 1000 && a->contents() != nullptr, "a buffer from a heap");
	if (!a) {
		return;
	}
	report(a->heap() == heap && a->heapOffset() == 0 && a->allocatedSize() == saBuffer.size, "the buffer knows its heap, has no heap offset and allocates what the size query says");
	report(heap->usedSize() == saBuffer.size, "usedSize counts the buffer's size");
	auto afterOne = heap->maxAvailableSize(1);
	report(afterOne == heap->size() - saBuffer.size, "maxAvailableSize at alignment 1 shrinks by that size");
	report(heap->maxAvailableSize(65536) <= afterOne, "a larger alignment never offers more");

	auto b = heap->newBuffer(5000, sharedOptions);
	auto c = heap->newBuffer(1, sharedOptions);
	report(b && c && heap->usedSize() == saBuffer.size + device.heapBufferSizeAndAlign(5000, sharedOptions).size + device.heapBufferSizeAndAlign(1, sharedOptions).size, "usedSize is the sum of the buffers' sizes");
	report(memoryAllocations - before == 1, "the buffers took no memory allocation of their own");

	// no overlap: patterns written through the CPU
	fillPattern(a.get(), 1);
	fillPattern(b.get(), 2);
	fillPattern(c.get(), 3);
	report(hasPattern(a.get(), 1) && hasPattern(b.get(), 2) && hasPattern(c.get(), 3), "buffers of a heap do not overlap");
	auto distance = static_cast<const uint8_t*>(b->contents()) - static_cast<const uint8_t*>(a->contents());
	report(distance != 0 && static_cast<size_t>(distance < 0 ? -distance : distance) >= std::min<size_t>(saBuffer.size, 1000), "buffers are at different places of one memory");

	// and the GPU sees them where they are
	auto commandBuffer = queue.commandBuffer();
	auto blit = commandBuffer->blitCommandEncoder();
	blit->copy(b, 0, a, 0, 1000);
	blit->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
	bool copied = std::memcmp(a->contents(), b->contents(), 1000) == 0 && hasPattern(c.get(), 3);
	for (size_t i = 0; i < 5000; ++i) {
		copied = copied && static_cast<const uint8_t*>(b->contents())[i] == static_cast<uint8_t>(2 + i * 7);
	}
	report(copied, "a blit between two heap buffers copies exactly their bytes");

	// freeing
	b.reset();
	report(heap->usedSize() == saBuffer.size + device.heapBufferSizeAndAlign(1, sharedOptions).size, "destroying a buffer gives its size back");
	c.reset();
	a.reset();
	report(heap->usedSize() == 0 && heap->maxAvailableSize(1) == heap->size() && heap->maxAvailableSize(65536) == heap->size(), "an emptied heap is empty again");

	// filling the heap
	std::vector<std::shared_ptr<Buffer>> all;
	while (auto buffer = heap->newBuffer(100000, sharedOptions)) {
		all.push_back(buffer);
		if (all.size() > 1000) {
			break;
		}
	}
	size_t each = device.heapBufferSizeAndAlign(100000, sharedOptions).size;
	report(all.size() >= (heap->size() - 65536) / alignUp(each, saBuffer.align) && all.size() <= heap->size() / each && all.size() <= 1000, "buffers fill the heap, no more than its size allows");
	report(!heap->newBuffer(heap->size(), sharedOptions), "a buffer that does not fit is refused");
	size_t left = heap->maxAvailableSize(1);
	report(left < each && heap->usedSize() == all.size() * each, "a full heap has less than one buffer left and counts all of them");
	report(!heap->newBuffer(left + 1, sharedOptions), "a buffer one byte over what is available is refused");
	all.clear();
	report(heap->usedSize() == 0, "freeing all of them empties the heap");
	// the largest buffer the empty heap takes
	size_t longest = heap->size();
	while (device.heapBufferSizeAndAlign(longest, sharedOptions).size > heap->size()) {
		--longest;
	}
	auto whole = heap->newBuffer(longest, sharedOptions);
	report(whole && heap->maxAvailableSize(1) < saBuffer.size, "one buffer can take the whole heap");
	whole.reset();

	// refusals
	report(!heap->newBuffer(1000, privateOptions), "a buffer with another storage mode is refused");
	report(!heap->newBuffer(1000, sharedOptions | ResourceOptions::CPUCacheModeWriteCombined), "a buffer with another CPU cache mode is refused");
	report(!heap->newBuffer(0, sharedOptions), "a zero-length buffer is refused in an automatic heap");
	report(!heap->newBuffer(1000, sharedOptions, 0), "a buffer at an offset is refused in an automatic heap");
	report(!heap->newTexture(textureDescriptor(8, 8, sharedOptions), 0), "a texture at an offset is refused in an automatic heap");
	report(!heap->newTexture(textureDescriptor(8, 8, privateOptions)), "a texture with another storage mode is refused");
	report(heap->usedSize() == 0, "refusals leave nothing allocated");
}

void testAutomaticTextures(Device& device, CommandQueue& queue) {
	auto heap = device.newHeap(heapDescriptor(1 << 20, StorageMode::Private, HeapType::Automatic));
	if (!heap) {
		report(false, "an automatic private heap");
		return;
	}
	auto descriptor = textureDescriptor(64, 64, privateOptions);
	auto sa = device.heapTextureSizeAndAlign(descriptor);
	unsigned before = memoryAllocations;
	auto texture = heap->newTexture(descriptor);
	report(texture && texture->width() == 64 && texture->height() == 64 && texture->pixelFormat() == PixelFormat::RGBA8Unorm, "a texture from a heap");
	if (!texture) {
		return;
	}
	report(texture->heap() == heap && texture->heapOffset() == 0 && texture->allocatedSize() == sa.size, "the texture knows its heap and allocates what the size query says");
	report(heap->usedSize() == sa.size && memoryAllocations == before, "usedSize counts the texture's size and it took no allocation");

	auto buffer = heap->newBuffer(4096, privateOptions);
	report(buffer && heap->usedSize() == sa.size + device.heapBufferSizeAndAlign(4096, privateOptions).size, "a texture and a buffer count together");

	// two textures render independently
	auto other = heap->newTexture(descriptor);
	report(other && heap->usedSize() >= 2 * sa.size, "a second texture");
	if (other) {
		clear(queue, texture, 1.0);
		clear(queue, other, 0.0);
		uint32_t first = firstPixel(device, queue, texture);
		uint32_t second = firstPixel(device, queue, other);
		report(first == 0xffff00ffu && second == 0xffff0000u, "render passes into two textures of a heap do not disturb each other");
	}

	// a view is part of its texture
	auto view = texture->newTextureView(PixelFormat::RGBA8Unorm_sRGB);
	report(view && view->heap() == heap && view->heapOffset() == 0, "a texture view shares its texture's heap");

	// multisample and mip levels
	auto multisample = textureDescriptor(32, 32, privateOptions);
	multisample.textureType = TextureType::e2DMultisample;
	multisample.sampleCount = 4;
	multisample.usage = TextureUsage::RenderTarget;
	if (device.supportsTextureSampleCount(4)) {
		report(heap->newTexture(multisample) != nullptr, "a multisample texture from a heap");
	}

	// refused with a texture of another storage or cache mode
	report(!heap->newTexture(textureDescriptor(8, 8, sharedOptions)), "a texture with another storage mode is refused");
	auto writeCombined = textureDescriptor(8, 8, privateOptions | ResourceOptions::CPUCacheModeWriteCombined);
	report(!heap->newTexture(writeCombined), "a texture with another CPU cache mode is refused");

	// a texture larger than the heap
	report(!heap->newTexture(textureDescriptor(4096, 4096, privateOptions)), "a texture larger than the heap is refused");
	report(heap->newTexture(descriptor) != nullptr, "a failed texture leaves the heap usable");

	texture.reset();
	other.reset();
	view.reset();
	buffer.reset();
	report(heap->usedSize() == 0, "destroying the textures and buffers empties the heap");
}

void testSharedHeapTextures(Device& device) {
	auto heap = device.newHeap(heapDescriptor(1 << 20, StorageMode::Shared, HeapType::Automatic));
	if (!heap) {
		report(false, "an automatic shared heap");
		return;
	}
	auto texture = heap->newTexture(textureDescriptor(16, 16, sharedOptions));
	if (!texture) {
		note("textures in a Shared heap (this device allows images only in memory the CPU cannot map)");
		return;
	}
	std::vector<uint32_t> pixels(256), out(256, 0);
	for (size_t i = 0; i < pixels.size(); ++i) {
		pixels[i] = 0x11223300 + static_cast<uint32_t>(i);
	}
	texture->replaceRegion(Region::make2D(0, 0, 16, 16), 0, pixels.data(), 64);
	texture->getBytes(Region::make2D(0, 0, 16, 16), 0, out.data(), 64);
	report(out == pixels, "a texture in a Shared heap holds what replaceRegion wrote");
}

void testAliasable(Device& device, CommandQueue& queue) {
	(void)queue;
	auto half = device.heapBufferSizeAndAlign(1 << 15, privateOptions);
	auto heap = device.newHeap(heapDescriptor(alignUp(2 * half.size, 4096), StorageMode::Private, HeapType::Automatic));
	if (!heap) {
		report(false, "an automatic heap for aliasing");
		return;
	}
	auto a = heap->newBuffer(1 << 15, privateOptions);
	auto b = heap->newBuffer(1 << 15, privateOptions);
	report(a && b && !a->isAliasable() && !b->isAliasable(), "buffers of an automatic heap are not aliasable");
	size_t used = heap->usedSize();
	report(used == 2 * half.size, "two buffers are counted");
	report(!heap->newBuffer(heap->size() - used + 1, privateOptions), "the heap has no room left for more than its remainder");

	a->makeAliasable();
	report(a->isAliasable() && !b->isAliasable(), "makeAliasable marks only that buffer");
	report(heap->usedSize() == half.size, "an aliasable buffer no longer counts");
	report(a->heap() == heap, "an aliasable buffer keeps its heap");
	size_t room = heap->maxAvailableSize(1);
	auto c = heap->newBuffer(1 << 15, privateOptions);
	report(c && room >= half.size && heap->usedSize() == 2 * half.size, "the freed range is reused");
	a->makeAliasable();
	report(heap->usedSize() == 2 * half.size, "making a buffer aliasable twice gives nothing back twice");
	a.reset();
	report(heap->usedSize() == 2 * half.size, "destroying an aliasable buffer gives nothing back again");
	b.reset();
	c.reset();
	report(heap->usedSize() == 0 && heap->maxAvailableSize(1) == heap->size(), "everything is back after the others are destroyed");

	auto plain = device.newBuffer(100, privateOptions);
	plain->makeAliasable();
	report(!plain->isAliasable() && plain->heap() == nullptr && plain->heapOffset() == 0, "a buffer that is not from a heap is never aliasable and has no heap");
}

void testPlacementHeap(Device& device, CommandQueue& queue) {
	for (bool isShared: { true, false }) {
		auto options = isShared ? sharedOptions : privateOptions;
		auto storageMode = isShared ? StorageMode::Shared : StorageMode::Private;
		std::printf("-- placement heap, %s\n", isShared ? "shared" : "private");
		auto heap = device.newHeap(heapDescriptor(1 << 20, storageMode, HeapType::Placement));
		if (!heap) {
			report(false, "a placement heap");
			continue;
		}
		auto sa = device.heapBufferSizeAndAlign(1000, options);

		report(!heap->newBuffer(1000, options), "a buffer without an offset is refused in a placement heap");
		report(!heap->newTexture(textureDescriptor(8, 8, options)), "a texture without an offset is refused in a placement heap");

		unsigned before = memoryAllocations;
		auto a = heap->newBuffer(1000, options, 0);
		report(a && a->heap() == heap && a->heapOffset() == 0 && a->allocatedSize() == sa.size, "a buffer at offset 0");
		report(heap->usedSize() == 0 && heap->maxAvailableSize(1) == 0, "a placement heap's counters stay 0");
		report(a && a->isAliasable(), "resources of a placement heap are aliasable");

		// offsets
		bool aligned = true;
		for (size_t offset = sa.align; offset < heap->size() / 2; offset *= 2) {
			auto x = heap->newBuffer(16, options, offset);
			aligned = aligned && x && x->heapOffset() == offset;
		}
		report(aligned, "buffers at aligned offsets");
		report(!heap->newBuffer(16, options, sa.align + 1), "an offset that is not a multiple of the alignment is refused");
		if (sa.align > 1) {
			report(!heap->newBuffer(16, options, 1), "offset 1 is refused");
		}
		size_t end = heap->size() - sa.size;
		end = end / sa.align * sa.align;
		auto last = heap->newBuffer(1000, options, end);
		report(last && last->heapOffset() == end, "a buffer ending inside the heap");
		report(!heap->newBuffer(1000, options, end + sa.align), "a buffer that ends past the heap is refused");
		report(!heap->newBuffer(1000, options, heap->size() + sa.align), "an offset past the heap is refused");
		auto zeroLength = heap->newBuffer(0, options, 0);
		report(zeroLength && zeroLength->length() == 0, "a zero-length buffer is allowed in a placement heap");
		report(memoryAllocations == before, "none of them took a memory allocation");

		// overlap
		if (isShared) {
			auto b = heap->newBuffer(1000, options, 0);
			report(b && b->isAliasable() && a->contents() == b->contents(), "two buffers at one offset share their memory");
			fillPattern(a.get(), 9);
			report(hasPattern(b.get(), 9), "bytes written through one aliasing buffer are read through the other");
			auto offsetSecond = heap->newBuffer(1000, options, alignUp(sa.size, sa.align));
			fillPattern(offsetSecond.get(), 77);
			report(hasPattern(a.get(), 9) && hasPattern(offsetSecond.get(), 77), "buffers that do not overlap stay apart");
		}

		// the GPU through an aliasing pair of private buffers: fill through one, read through the other
		// (a heap's resources are not hazard tracked unless it asks for it, and the fill and the copy need an order)
		{
			auto trackedDescriptor = heapDescriptor(1 << 20, storageMode, HeapType::Placement);
			trackedDescriptor.hazardTrackingMode = HazardTrackingMode::HazardTrackingModeTracked;
			auto trackedHeap = device.newHeap(trackedDescriptor);
			auto first = trackedHeap ? trackedHeap->newBuffer(4096, options, 4096 * 4) : nullptr;
			auto second = trackedHeap ? trackedHeap->newBuffer(4096, options, 4096 * 4) : nullptr;
			auto readback = device.newBuffer(4096, sharedOptions);
			if (first && second) {
				auto commandBuffer = queue.commandBuffer();
				auto blit = commandBuffer->blitCommandEncoder();
				blit->fillBuffer(first, Range<size_t> { 0, 4096 }, 0xc3);
				blit->copy(second, 0, readback, 0, 4096);
				blit->endEncoding();
				commandBuffer->commit();
				commandBuffer->waitUntilCompleted();
				bool ok = true;
				for (size_t i = 0; i < 4096; ++i) {
					ok = ok && static_cast<const uint8_t*>(readback->contents())[i] == 0xc3;
				}
				report(ok, "the GPU writes through one aliasing buffer and reads through the other");
			} else {
				report(false, "two aliasing buffers");
			}
		}

		// textures
		auto descriptor = textureDescriptor(16, 16, options);
		auto textureSizeAndAlign = device.heapTextureSizeAndAlign(descriptor);
		size_t at = alignUp(1 << 17, textureSizeAndAlign.align);
		auto t1 = heap->newTexture(descriptor, at);
		if (!t1) {
			note(isShared ? "textures in a Shared placement heap (see above)" : "textures in a placement heap");
			if (!isShared) {
				report(false, "a texture at an aligned offset");
			}
			continue;
		}
		report(t1->heap() == heap && t1->heapOffset() == at && t1->allocatedSize() == textureSizeAndAlign.size, "a texture at an aligned offset");
		report(!heap->newTexture(descriptor, at + 1), "an unaligned texture offset is refused");
		report(!heap->newTexture(descriptor, heap->size() - textureSizeAndAlign.size / 2), "a texture that ends past the heap is refused");
		auto t2 = heap->newTexture(descriptor, at);
		report(t2 && t2->isAliasable() && t2->heapOffset() == at, "a second texture at the same offset");
		if (!isShared && t2) {
			clear(queue, t1, 1.0);
			report(firstPixel(device, queue, t1) == 0xffff00ffu, "a texture in a placement heap renders");
			clear(queue, t2, 0.0);
			report(firstPixel(device, queue, t2) == 0xffff0000u, "its aliasing texture renders");
			// same format and size, aliasing: what the second one rendered is what the first one now holds
			report(firstPixel(device, queue, t1) == 0xffff0000u, "pixels rendered through one aliasing texture are read through the other");
		}
	}
}

void testLifetimeAndThreads(Device& device) {
	auto heap = device.newHeap(heapDescriptor(1 << 20, StorageMode::Shared, HeapType::Automatic));
	auto buffer = heap->newBuffer(100, sharedOptions);
	std::memset(buffer->contents(), 7, 100);
	std::weak_ptr<Heap> weak = heap;
	heap.reset();
	report(!weak.expired() && buffer->heap() != nullptr && static_cast<uint8_t*>(buffer->contents())[50] == 7, "a buffer keeps its heap and memory alive");
	buffer.reset();
	report(weak.expired(), "the heap goes with its last resource");

	heap = device.newHeap(heapDescriptor(1 << 22, StorageMode::Shared, HeapType::Automatic));
	std::vector<std::thread> threads;
	std::atomic<unsigned> overlaps { 0 };
	for (unsigned t = 0; t < 4; ++t) {
		threads.emplace_back([&, t]() {
			for (unsigned i = 0; i < 200; ++i) {
				auto x = heap->newBuffer(1000 + t * 100 + i, sharedOptions);
				if (!x) {
					continue;
				}
				fillPattern(x.get(), static_cast<uint8_t>(t * 16 + i));
				std::this_thread::yield();
				if (!hasPattern(x.get(), static_cast<uint8_t>(t * 16 + i))) {
					++overlaps;
				}
				if (i % 3 == 0) {
					x->makeAliasable();
				}
			}
		});
	}
	for (auto& thread: threads) {
		thread.join();
	}
	report(overlaps == 0, "four threads allocating and freeing buffers never overlap");
	report(heap->usedSize() == 0 && heap->maxAvailableSize(1) == heap->size(), "and the heap is empty after them");
}

}

int main() {
	Indium::init(nullptr, 0, false);

	{
		auto device = createSystemDefaultDevice();
		if (!device) {
			std::printf("SKIP no device\n");
			return 0;
		}

		bool keepPollingDevice = true;
		std::thread devicePollingThread([device, &keepPollingDevice]() {
			while (keepPollingDevice) {
				device->pollEvents(UINT64_MAX);
			}
		});

		DynamicVK::vkAllocateMemory.resolve();
		originalAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(DynamicVK::vkAllocateMemory.pointer);
		DynamicVK::vkAllocateMemory.pointer = reinterpret_cast<void*>(countAllocateMemory);

		auto queue = device->newCommandQueue();

		testSizeAndAlign(*device);
		testHeapCreation(device);
		testAutomaticHeap(*device, *queue);
		testAutomaticTextures(*device, *queue);
		testSharedHeapTextures(*device);
		testAliasable(*device, *queue);
		testPlacementHeap(*device, *queue);
		testLifetimeAndThreads(*device);

		keepPollingDevice = false;
		device->wakeupEventLoop();
		devicePollingThread.join();
	}

	Indium::finit();

	std::printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
