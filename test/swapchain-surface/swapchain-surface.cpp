// Copyright (C) 2026 Lubos Dolezel
//
// Presenting to a native window: a Vulkan surface made for an Xlib window, an IndiumKit::Layer on it, the
// number of drawables the application can hold, resizing, the present callback and the pixel format
// and display sync settings. Needs an X server (DISPLAY); skips without one or without a device that can
// present to it.

#include <indium-kit/layer.hpp>
#include <indium/device.hpp>
#include <indium/init.hpp>
#include <indium/indium.hpp>
#include <indium/texture.hpp>

#include <X11/Xlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

// Draws nothing into the drawable's image beyond clearing it, and presents it.
void clearAndPresent(const std::shared_ptr<Indium::CommandQueue>& queue, const std::shared_ptr<IndiumKit::Drawable>& drawable) {
	auto commandBuffer = queue->commandBuffer();

	Indium::RenderPassDescriptor pass {};
	pass.colorAttachments.emplace_back();
	pass.colorAttachments[0].texture = drawable->texture();
	pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
	pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
	pass.colorAttachments[0].clearColor = { 0.0, 0.5, 1.0, 1.0 };

	auto encoder = commandBuffer->renderCommandEncoder(pass);
	encoder->endEncoding();
	commandBuffer->presentDrawable(drawable);
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();
}

// The next drawable once the surface has the given size. A window system tells Vulkan about a window's new
// size a little after the window has it, so drawables of the old size may come first; they are dropped.
std::shared_ptr<IndiumKit::Drawable> drawableOfSize(const std::shared_ptr<IndiumKit::Layer>& layer, size_t width, size_t height) {
	for (int i = 0; i < 100; ++i) {
		auto drawable = layer->nextDrawable(2000000000ULL);
		if (drawable && drawable->texture()->width() == width && drawable->texture()->height() == height) {
			return drawable;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return nullptr;
}

// Runs the device's event loop, which completes command buffers, for as long as it lives.
struct DevicePoller {
	std::shared_ptr<Indium::Device> device;
	std::atomic<bool> keep { true };
	std::thread thread;

	explicit DevicePoller(std::shared_ptr<Indium::Device> d): device(std::move(d)) {
		thread = std::thread([this]() {
			while (keep) {
				device->pollEvents(UINT64_MAX);
			}
		});
	}

	~DevicePoller() {
		keep = false;
		device->wakeupEventLoop();
		thread.join();
	}
};

}

int main() {
	std::setvbuf(stdout, nullptr, _IOLBF, 0);

	Display* display = XOpenDisplay(nullptr);
	if (!display) {
		std::printf("SKIP no X display\n");
		return 0;
	}

	Indium::init(nullptr, 0, false);

	auto device = Indium::createSystemDefaultDevice();
	if (!device) {
		std::printf("SKIP no device\n");
		return 0;
	}

	auto poller = std::make_unique<DevicePoller>(device);

	Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 160, 120, 0, 0, 0);
	XMapWindow(display, window);
	XSync(display, False);

	VkSurfaceKHR surface = IndiumKit::createXlibSurface(display, window);
	if (surface == VK_NULL_HANDLE) {
		std::printf("SKIP the Vulkan loader cannot present to Xlib windows\n");
		return 0;
	}

	if (!IndiumKit::canPresent(device, surface)) {
		std::printf("SKIP the device cannot present to the window\n");
		IndiumKit::destroySurface(surface);
		return 0;
	}

	// control: no surface, no layer
	report(IndiumKit::Layer::make(VK_NULL_HANDLE, device, 160, 120) == nullptr, "a layer cannot be made without a surface");
	report(!IndiumKit::canPresent(device, VK_NULL_HANDLE), "a device cannot present to no surface");

	auto layer = IndiumKit::Layer::make(surface, device, 160, 120);
	report(layer != nullptr, "a layer is made for the window");
	if (!layer) {
		IndiumKit::destroySurface(surface);
		return 1;
	}

	auto queue = device->newCommandQueue();

	// size and pixel format follow the window
	report(layer->pixelFormat() == Indium::PixelFormat::BGRA8Unorm, "the default pixel format is BGRA8Unorm");
	report(layer->framebufferWidth() == 160 && layer->framebufferHeight() == 120, "the images have the window's size");

	// the present callback runs once, with the result
	{
		auto drawable = layer->nextDrawable();
		report(drawable != nullptr, "a drawable is available");
		if (!drawable) {
			return 1;
		}
		report(drawable->texture()->width() == 160 && drawable->texture()->height() == 120, "the drawable's texture has the layer's size");
		report(drawable->texture()->pixelFormat() == Indium::PixelFormat::BGRA8Unorm, "the drawable's texture has the layer's pixel format");

		// the callback runs on the thread that submits the command buffer, which is not the caller's
		auto calls = std::make_shared<std::atomic<int>>(0);
		auto presented = std::make_shared<std::atomic<bool>>(false);
		drawable->setPresentCallback([calls, presented](bool ok) {
			presented->store(ok);
			calls->fetch_add(1);
		});
		clearAndPresent(queue, drawable);
		for (int i = 0; i < 400 && calls->load() == 0; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		report(calls->load() == 1 && presented->load(), "the present callback ran once and reported success");
	}

	// the application can hold as many drawables as asked for, and no more
	for (uint32_t count: { 2u, 3u }) {
		layer->setImageCount(count);

		std::vector<std::shared_ptr<IndiumKit::Drawable>> held;
		for (uint32_t i = 0; i < count; ++i) {
			auto drawable = layer->nextDrawable(2000000000ULL);
			if (drawable) {
				held.push_back(drawable);
			}
		}
		char what[96];
		std::snprintf(what, sizeof(what), "%u drawables can be held at once", count);
		report(held.size() == count, what);

		auto start = std::chrono::steady_clock::now();
		auto extra = layer->nextDrawable(150000000ULL);
		auto waited = std::chrono::steady_clock::now() - start;
		std::snprintf(what, sizeof(what), "drawable %u is not available while %u are held", count + 1, count);
		report(extra == nullptr, what);
		report(waited >= std::chrono::milliseconds(100), "the wait for it took its timeout");

		// dropped unpresented: the images come back through a new swapchain
		held.clear();
		auto again = layer->nextDrawable(2000000000ULL);
		std::snprintf(what, sizeof(what), "after dropping %u unpresented drawables one is available again", count);
		report(again != nullptr, what);
		if (again) {
			clearAndPresent(queue, again);
		}
	}

	// the window is resized: the images follow it
	{
		XResizeWindow(display, window, 200, 90);
		XSync(display, False);
		layer->setFramebufferSize(200, 90);

		auto drawable = drawableOfSize(layer, 200, 90);
		report(drawable != nullptr, "a drawable has the window's new size after a resize");
		if (drawable) {
			clearAndPresent(queue, drawable);
		}
		report(layer->framebufferWidth() == 200 && layer->framebufferHeight() == 90, "the layer reports the new size");
	}

	// a drawable acquired before a resize still presents
	{
		auto before = layer->nextDrawable(2000000000ULL);
		XResizeWindow(display, window, 120, 100);
		XSync(display, False);
		layer->setFramebufferSize(120, 100);
		auto after = drawableOfSize(layer, 120, 100);
		report(before != nullptr && after != nullptr, "drawables before and after a resize");
		if (before && after) {
			report(before->texture()->width() == 200 && after->texture()->width() == 120, "each keeps the size of its swapchain");
			clearAndPresent(queue, before);
			clearAndPresent(queue, after);
		}
	}

	// settings that rebuild the swapchain keep presenting
	layer->setDisplaySyncEnabled(false);
	{
		auto drawable = layer->nextDrawable(2000000000ULL);
		report(drawable != nullptr, "a drawable is available without display sync");
		if (drawable) {
			clearAndPresent(queue, drawable);
		}
	}
	layer->setDisplaySyncEnabled(true);

	layer->setFramebufferOnly(false);
	{
		auto drawable = layer->nextDrawable(2000000000ULL);
		report(drawable != nullptr && !drawable->texture()->framebufferOnly(), "a drawable of a layer that is not framebuffer only can be sampled and copied");
		if (drawable) {
			clearAndPresent(queue, drawable);
		}
	}

	report(!layer->setPixelFormat(Indium::PixelFormat::Invalid), "a pixel format the surface cannot present is refused");
	layer->setPixelFormat(Indium::PixelFormat::BGRA8Unorm);
	report(layer->pixelFormat() == Indium::PixelFormat::BGRA8Unorm, "the pixel format is back to BGRA8Unorm");

	queue.reset();
	layer.reset();
	XDestroyWindow(display, window);
	XCloseDisplay(display);

	poller.reset();
	device.reset();
	Indium::finit();

	std::printf("%s\n", failures ? "FAILED" : "OK");
	return failures ? 1 : 0;
}
