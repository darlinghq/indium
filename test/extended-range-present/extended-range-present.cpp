// Copyright (C) 2026 Lubos Dolezel
//
// Presenting the extended-range pixel formats to a native window: a layer in BGRA10_XR, BGRA10_XR_sRGB,
// BGR10_XR or BGR10_XR_sRGB shows what the values rendered to its drawables mean. The window of this test is
// read back from the X server, whose surface is an 8-bit one, so what a display that cannot show more than 0
// to 1 shows is checked: the gamma encoded values of BGRA10_XR and BGR10_XR as they are, the linear values of the
// sRGB formats through the sRGB curve, everything outside the range clamped, and the colors of the same
// clear in the 8-bit formats unchanged. Needs an X server (DISPLAY); skips without one or without a device that
// can present to it.

#include <indium-kit/layer.hpp>
#include <indium/device.hpp>
#include <indium/init.hpp>
#include <indium/indium.hpp>
#include <indium/texture.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

unsigned failures = 0;

void report(bool ok, const std::string& what, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
	failures += !ok;
}

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

double srgbEncode(double linear) {
	linear = std::fmin(std::fmax(linear, 0.0), 1.0);
	return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

// the 8-bit value of a channel of 0 to 1
int eightBit(double value) {
	return static_cast<int>(std::lround(std::fmin(std::fmax(value, 0.0), 1.0) * 255.0));
}

// Clears a drawable to `color`, presents it, and reads the middle of the window back.
bool presentAndRead(Display* display, Window window, const std::shared_ptr<IndiumKit::Layer>& layer, const std::shared_ptr<Indium::CommandQueue>& queue,
	Indium::ClearColor color, unsigned long& pixel, bool& presented) {
	// the first drawables may have the size of before the format changed
	for (int attempt = 0; attempt < 3; ++attempt) {
		auto drawable = layer->nextDrawable(2000000000ULL);
		if (!drawable) {
			return false;
		}
		std::atomic<int> callbackResult { -1 };
		drawable->setPresentCallback([&](bool ok) { callbackResult = ok ? 1 : 0; });
		auto commandBuffer = queue->commandBuffer();
		Indium::RenderPassDescriptor pass {};
		pass.colorAttachments.emplace_back();
		pass.colorAttachments[0].texture = drawable->texture();
		pass.colorAttachments[0].loadAction = Indium::LoadAction::Clear;
		pass.colorAttachments[0].storeAction = Indium::StoreAction::Store;
		pass.colorAttachments[0].clearColor = color;
		auto encoder = commandBuffer->renderCommandEncoder(pass);
		encoder->endEncoding();
		commandBuffer->presentDrawable(drawable);
		commandBuffer->commit();
		commandBuffer->waitUntilCompleted();
		for (int i = 0; i < 200 && callbackResult == -1; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		presented = callbackResult == 1;
	}
	// a window system shows the frame a moment after it was handed over
	for (int i = 0; i < 20; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		XSync(display, False);
	}
	XImage* image = XGetImage(display, window, 80, 60, 1, 1, AllPlanes, ZPixmap);
	if (!image) {
		return false;
	}
	pixel = XGetPixel(image, 0, 0);
	XDestroyImage(image);
	return true;
}

struct Case {
	Indium::PixelFormat format;
	const char* name;
	bool srgb;
	bool alpha;
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
	if (surface == VK_NULL_HANDLE || !IndiumKit::canPresent(device, surface)) {
		std::printf("SKIP the device cannot present to the window\n");
		return 0;
	}
	auto layer = IndiumKit::Layer::make(surface, device, 160, 120);
	if (!layer) {
		std::printf("FAIL no layer\n");
		return 1;
	}
	auto queue = device->newCommandQueue();

	XWindowAttributes attributes;
	XGetWindowAttributes(display, window, &attributes);
	Visual* visual = attributes.visual;
	auto channel = [&](unsigned long pixel, unsigned long mask) {
		int shift = 0;
		while (!((mask >> shift) & 1)) {
			++shift;
		}
		return static_cast<int>((pixel & mask) >> shift);
	};

	// the 8-bit formats the colors are compared with
	struct Color { double r, g, b; };
	const Color inRange { 0.2, 0.4, 0.8 };
	const Color outOfRange { 1.5, -0.5, 0.6 };

	const Case cases[] = {
		{ Indium::PixelFormat::BGRA8Unorm, "BGRA8Unorm", false, true },
		{ Indium::PixelFormat::BGRA10_XR, "BGRA10_XR", false, true },
		{ Indium::PixelFormat::BGRA10_XR_sRGB, "BGRA10_XR_sRGB", true, true },
		{ Indium::PixelFormat::BGR10_XR, "BGR10_XR", false, false },
		{ Indium::PixelFormat::BGR10_XR_sRGB, "BGR10_XR_sRGB", true, false },
	};

	for (const auto& test: cases) {
		bool accepted = layer->setPixelFormat(test.format);
		report(accepted, std::string(test.name) + ": the layer takes the pixel format");
		if (!accepted) {
			continue;
		}
		report(layer->pixelFormat() == test.format, std::string(test.name) + ": the layer has the pixel format");

		for (int variant = 0; variant < 2; ++variant) {
			const Color& color = variant == 0 ? inRange : outOfRange;
			unsigned long pixel = 0;
			bool presented = false;
			bool read = presentAndRead(display, window, layer, queue, Indium::ClearColor(color.r, color.g, color.b, 1.0), pixel, presented);
			report(read && presented, std::string(test.name) + (variant == 0 ? ": a frame is presented" : ": a frame outside the range is presented"));
			if (!read) {
				continue;
			}
			int red = channel(pixel, visual->red_mask), green = channel(pixel, visual->green_mask), blue = channel(pixel, visual->blue_mask);
			// what the 8-bit surface shows: the values (a code only moves a value by a thousandth), through the curve for
			// the sRGB formats, outside 0 to 1 clamped
			auto shown = [&](double value) { return eightBit(test.srgb ? srgbEncode(value) : value); };
			bool ok = std::abs(red - shown(color.r)) <= 1 && std::abs(green - shown(color.g)) <= 1 && std::abs(blue - shown(color.b)) <= 1;
			report(ok, std::string(test.name) + (variant == 0 ? ": the colors of the clear are shown" : ": values outside 0 to 1 are shown clamped"),
				"window " + std::to_string(red) + " " + std::to_string(green) + " " + std::to_string(blue)
				+ ", expected " + std::to_string(shown(color.r)) + " " + std::to_string(shown(color.g)) + " " + std::to_string(shown(color.b)));
		}
	}

	layer.reset();
	poller.reset();
	std::printf("RESULT failures=%u\n", failures);
	return failures ? 1 : 0;
}
