#include <algorithm>
#include <chrono>
#include <indium-kit/layer.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <stdexcept>

namespace DynamicVK = Indium::DynamicVK;

IndiumKit::Drawable::~Drawable() {};
IndiumKit::Layer::~Layer() {};

//
// surfaces
//

// The layouts below are those of VkXlibSurfaceCreateInfoKHR and VkWaylandSurfaceCreateInfoKHR
// (the Vulkan specification, VK_KHR_xlib_surface and VK_KHR_wayland_surface), declared here so that
// no window system header is needed.
namespace {
	struct XlibSurfaceCreateInfo {
		VkStructureType sType;
		const void* pNext;
		VkFlags flags;
		void* dpy;
		unsigned long window;
	};

	struct WaylandSurfaceCreateInfo {
		VkStructureType sType;
		const void* pNext;
		VkFlags flags;
		void* display;
		void* surface;
	};
};

VkSurfaceKHR IndiumKit::createXlibSurface(void* display, unsigned long window) {
	if (Indium::globalInstance == VK_NULL_HANDLE || !DynamicVK::vkCreateXlibSurfaceKHR.isAvailable()) {
		return VK_NULL_HANDLE;
	}

	XlibSurfaceCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
	info.dpy = display;
	info.window = window;

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateXlibSurfaceKHR(Indium::globalInstance, &info, nullptr, &surface) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	return surface;
};

VkSurfaceKHR IndiumKit::createWaylandSurface(void* display, void* wlSurface) {
	if (Indium::globalInstance == VK_NULL_HANDLE || !DynamicVK::vkCreateWaylandSurfaceKHR.isAvailable()) {
		return VK_NULL_HANDLE;
	}

	WaylandSurfaceCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	info.display = display;
	info.surface = wlSurface;

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateWaylandSurfaceKHR(Indium::globalInstance, &info, nullptr, &surface) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	return surface;
};

void IndiumKit::destroySurface(VkSurfaceKHR surface) {
	if (surface != VK_NULL_HANDLE && Indium::globalInstance != VK_NULL_HANDLE) {
		DynamicVK::vkDestroySurfaceKHR(Indium::globalInstance, surface, nullptr);
	}
};

bool IndiumKit::canPresent(const std::shared_ptr<Indium::Device>& device, VkSurfaceKHR surface) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device);

	if (!privateDevice || surface == VK_NULL_HANDLE || !privateDevice->graphicsQueueFamilyIndex()) {
		return false;
	}

	// creates the Vulkan device, which is what learns whether it can create swapchains
	privateDevice->device();

	if (!(privateDevice->features() & Indium::PrivateDevice::Feature::Swapchain)) {
		return false;
	}

	VkBool32 supported = VK_FALSE;
	if (DynamicVK::vkGetPhysicalDeviceSurfaceSupportKHR(privateDevice->physicalDevice(), *privateDevice->graphicsQueueFamilyIndex(), surface, &supported) != VK_SUCCESS) {
		return false;
	}
	return supported == VK_TRUE;
};

//
// swapchain
//

IndiumKit::SwapchainState::~SwapchainState() {
	if (!device) {
		return;
	}

	// presentation may still be using the images
	if (swapchain != VK_NULL_HANDLE) {
		// queues are externally synchronized, and vkDeviceWaitIdle touches all of them
		std::lock_guard queueLock(device->graphicsQueueMutex());
		DynamicVK::vkDeviceWaitIdle(device->device());
	}

	// the presentation may still read the images of an extended-range layer, which the wait above covers
	if (extendedRange.commandPool != VK_NULL_HANDLE) {
		DynamicVK::vkDestroyCommandPool(device->device(), extendedRange.commandPool, nullptr);
	}
	if (extendedRange.descriptorPool != VK_NULL_HANDLE) {
		DynamicVK::vkDestroyDescriptorPool(device->device(), extendedRange.descriptorPool, nullptr);
	}

	for (auto& imageView: imageViews) {
		DynamicVK::vkDestroyImageView(device->device(), imageView, nullptr);
	}

	if (swapchain != VK_NULL_HANDLE) {
		DynamicVK::vkDestroySwapchainKHR(device->device(), swapchain, nullptr);
	}
};

//
// drawable
//

IndiumKit::PrivateDrawable::PrivateDrawable(std::shared_ptr<PrivateLayer> layer, std::shared_ptr<SwapchainState> state, uint32_t swapchainImageIndex, std::shared_ptr<Indium::BinarySemaphore> extraWaitSemaphore):
	Indium::PrivateTexture(std::dynamic_pointer_cast<Indium::PrivateDevice>(layer->device())),
	_layer(layer),
	_state(state),
	_swapchainImageIndex(swapchainImageIndex)
{
	if (state->extendedRange.textures.empty()) {
		_extraWaitSemaphore = extraWaitSemaphore;
	} else {
		_acquireSemaphore = extraWaitSemaphore;
	}
};

IndiumKit::PrivateDrawable::~PrivateDrawable() {
	// An acquired image that is never presented cannot be handed back to the swapchain, which would
	// lose one image for good for each such drawable. Replacing the swapchain gives them all back.
	if (!_presented) {
		_layer->markOutdated(_state);
	}
};

std::shared_ptr<Indium::Texture> IndiumKit::PrivateDrawable::texture() {
	if (!_state->extendedRange.textures.empty()) {
		return _state->extendedRange.textures[_swapchainImageIndex];
	}
	return shared_from_this();
};

void IndiumKit::PrivateDrawable::setPresentCallback(std::function<void(bool)> callback) {
	_presentCallback = std::move(callback);
};

VkImageView IndiumKit::PrivateDrawable::imageView() {
	return _state->imageViews[_swapchainImageIndex];
};

VkImage IndiumKit::PrivateDrawable::image() {
	return _state->images[_swapchainImageIndex];
};

Indium::TextureType IndiumKit::PrivateDrawable::textureType() const {
	return Indium::TextureType::e2D;
};

Indium::PixelFormat IndiumKit::PrivateDrawable::pixelFormat() const {
	return _state->pixelFormat;
};

size_t IndiumKit::PrivateDrawable::width() const {
	return _state->width;
};

size_t IndiumKit::PrivateDrawable::height() const {
	return _state->height;
};

size_t IndiumKit::PrivateDrawable::depth() const {
	return 1;
};

size_t IndiumKit::PrivateDrawable::mipmapLevelCount() const {
	return 1;
};

size_t IndiumKit::PrivateDrawable::arrayLength() const {
	return 1;
};

size_t IndiumKit::PrivateDrawable::sampleCount() const {
	return 1;
};

bool IndiumKit::PrivateDrawable::framebufferOnly() const {
	return _state->framebufferOnly;
};

Indium::TextureUsage IndiumKit::PrivateDrawable::usage() const {
	// the swapchain images are always color attachments; unless the layer is framebufferOnly they can also be
	// sampled and written by shaders (see the image usage the swapchain is created with)
	auto usage = Indium::TextureUsage::RenderTarget;
	if (!_state->framebufferOnly) {
		usage = usage | Indium::TextureUsage::ShaderRead | Indium::TextureUsage::ShaderWrite;
	}
	return usage;
};

bool IndiumKit::PrivateDrawable::allowGPUOptimizedContents() const {
	return true;
};

bool IndiumKit::PrivateDrawable::shareable() const {
	return false;
};

Indium::TextureSwizzleChannels IndiumKit::PrivateDrawable::swizzle() const {
	return {
		Indium::TextureSwizzle::Red,
		Indium::TextureSwizzle::Green,
		Indium::TextureSwizzle::Blue,
		Indium::TextureSwizzle::Alpha,
	};
};

VkImageLayout IndiumKit::PrivateDrawable::imageLayout() {
	return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
};

void IndiumKit::PrivateDrawable::present() {
	_presented = true;

	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device());
	std::shared_ptr<Indium::BinarySemaphore> sema;
	if (!_state->extendedRange.textures.empty()) {
		// The work that rendered the texture signals its timeline; the conversion into the swapchain image comes
		// after it (and after the image is free), and presenting after the conversion.
		auto texture = std::dynamic_pointer_cast<Indium::PrivateTexture>(_state->extendedRange.textures[_swapchainImageIndex]);
		uint64_t waitValue = 0, signalValue = 0;
		std::shared_ptr<Indium::BinarySemaphore> unused;
		const auto& timeline = texture->acquire(waitValue, unused, signalValue);
		bool current = _layer->isCurrent(_state);
		sema = _state->extendedRange.presentSemaphores[_swapchainImageIndex];

		VkSemaphoreSubmitInfo waits[2] {};
		uint32_t waitCount = 0;
		waits[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waits[waitCount].semaphore = timeline.semaphore;
		waits[waitCount].value = waitValue;
		waits[waitCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		++waitCount;
		if (_acquireSemaphore) {
			waits[waitCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			waits[waitCount].semaphore = _acquireSemaphore->semaphore;
			waits[waitCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			++waitCount;
		}
		VkSemaphoreSubmitInfo signals[2] {};
		uint32_t signalCount = 0;
		signals[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signals[signalCount].semaphore = timeline.semaphore;
		signals[signalCount].value = signalValue;
		signals[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		++signalCount;
		if (current) {
			signals[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			signals[signalCount].semaphore = sema->semaphore;
			signals[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			++signalCount;
		}
		VkCommandBufferSubmitInfo commands {};
		commands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		commands.commandBuffer = _state->extendedRange.commandBuffers[_swapchainImageIndex];

		VkSubmitInfo2 submit {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.waitSemaphoreInfoCount = waitCount;
		submit.pWaitSemaphoreInfos = waits;
		// a frame of a swapchain that was replaced is dropped; its semaphores still have to be waited for and signaled
		submit.commandBufferInfoCount = current ? 1 : 0;
		submit.pCommandBufferInfos = &commands;
		submit.signalSemaphoreInfoCount = signalCount;
		submit.pSignalSemaphoreInfos = signals;
		{
			std::lock_guard queueLock(privateDevice->graphicsQueueMutex());
			DynamicVK::vkQueueSubmit2(privateDevice->graphicsQueue(), 1, &submit, VK_NULL_HANDLE);
		}
		_acquireSemaphore.reset();
		if (!current) {
			sema = nullptr;
		}
	} else {
		sema = synchronizePresentation();
	}

	VkResult result = VK_ERROR_OUT_OF_DATE_KHR;
	bool waitForDisplay = false;
	uint64_t presentID = 0;

	if (_layer->isCurrent(_state)) {
		waitForDisplay = _layer->waitsForPresents() && _presentCallback;
		VkPresentIdKHR idInfo {};
		if (waitForDisplay) {
			presentID = _layer->nextPresentID();
			idInfo.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
			idInfo.swapchainCount = 1;
			idInfo.pPresentIds = &presentID;
		}

		VkPresentInfoKHR info {};
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = sema ? 1 : 0;
		info.pWaitSemaphores = sema ? &sema->semaphore : nullptr;
		info.swapchainCount = 1;
		info.pSwapchains = &_state->swapchain;
		info.pImageIndices = &_swapchainImageIndex;
		info.pResults = nullptr;
		info.pNext = waitForDisplay ? &idInfo : nullptr;

		std::lock_guard queueLock(privateDevice->graphicsQueueMutex());
		result = DynamicVK::vkQueuePresentKHR(privateDevice->graphicsQueue(), &info);
	} else if (sema) {
		// The swapchain this image came from has been replaced (the window changed size, or a setting
		// changed) since the image was acquired, so the frame is out of date and is dropped: presenting
		// to a replaced swapchain is not reliable. The semaphore still has to be waited for, as it is
		// signaled by the work that drew the frame.
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		VkSubmitInfo submit {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &sema->semaphore;
		submit.pWaitDstStageMask = &stage;

		std::lock_guard queueLock(privateDevice->graphicsQueueMutex());
		DynamicVK::vkQueueSubmit(privateDevice->graphicsQueue(), 1, &submit, VK_NULL_HANDLE);
	}

	// An out-of-date or suboptimal swapchain still presents (or is simply replaced); only the
	// layer has to know, so that the next drawable comes from a new one.
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		_layer->markOutdated(_state);
	}

	if (_presentCallback) {
		auto callback = std::move(_presentCallback);
		_presentCallback = nullptr;
		bool ok = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
		if (ok && waitForDisplay) {
			_layer->callWhenPresented(_state, presentID, std::move(callback));
		} else {
			callback(ok);
		}
	}
};

void IndiumKit::PrivateDrawable::replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) {
	throw std::runtime_error("Cannot use replaceRegion on IndiumKit::Drawable");
};

void IndiumKit::PrivateDrawable::replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) {
	throw std::runtime_error("Cannot use replaceRegion on IndiumKit::Drawable");
};

//
// layer
//

std::shared_ptr<IndiumKit::Layer> IndiumKit::Layer::make(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight) {
	try {
		return std::make_shared<PrivateLayer>(surface, device, framebufferWidth, framebufferHeight);
	} catch (const std::runtime_error&) {
		return nullptr;
	}
};

VkColorSpaceKHR IndiumKit::PrivateLayer::hdrColorSpace(HdrTransferFunction transfer) {
	return IndiumKit::hdrColorSpace(transfer);
};

void IndiumKit::PrivateLayer::applyHdrMetadata(const std::shared_ptr<SwapchainState>& state) {
	if (!state || state->swapchain == VK_NULL_HANDLE || _hdrMetadata.transfer == HdrTransferFunction::None) {
		return;
	}

	// PQ and HLG metadata only describes a swapchain that actually uses that transfer function.
	if (state->colorSpace != hdrColorSpace(_hdrMetadata.transfer)) {
		return;
	}

	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	if (!(privateDevice->features() & Indium::PrivateDevice::Feature::HdrMetadata)
	    || !DynamicVK::vkSetHdrMetadataEXT.isAvailable()) {
		return;
	}

	HdrMetadata complete;
	if (!IndiumKit::completeHdrMetadata(_hdrMetadata, complete)) {
		return;
	}

	VkHdrMetadataEXT metadata = IndiumKit::hdrMetadataToVulkan(complete);
	DynamicVK::vkSetHdrMetadataEXT(privateDevice->device(), 1, &state->swapchain, &metadata);
};

IndiumKit::PrivateLayer::PrivateLayer(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight):
	_device(device),
	_width(framebufferWidth),
	_height(framebufferHeight)
{
	if (!canPresent(device, surface)) {
		throw std::runtime_error("Device cannot present to the surface");
	}

	// the surface is ours only once the layer exists, so that a failure leaves it to the caller
	_surface = surface;
	_state = createSwapchain(VK_NULL_HANDLE);
	if (!_state) {
		_surface = VK_NULL_HANDLE;
		throw std::runtime_error("Cannot create a swapchain for the surface");
	}

	if (waitsForPresents()) {
		_waiter = std::thread(&PrivateLayer::runWaiter, _waiterState, std::dynamic_pointer_cast<Indium::PrivateDevice>(_device));
	}
};

bool IndiumKit::PrivateLayer::waitsForPresents() {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	return !!(privateDevice->features() & Indium::PrivateDevice::Feature::PresentWait) && DynamicVK::vkWaitForPresentKHR.isAvailable();
};

void IndiumKit::PrivateLayer::callWhenPresented(std::shared_ptr<SwapchainState> state, uint64_t id, std::function<void(bool)> callback) {
	{
		std::scoped_lock lock(_waiterState->mutex);
		_waiterState->pending.push_back({ std::move(state), id, std::move(callback) });
	}
	_waiterState->condition.notify_one();
};

void IndiumKit::PrivateLayer::runWaiter(std::shared_ptr<Waiter> waiter, std::shared_ptr<Indium::PrivateDevice> privateDevice) {
	for (;;) {
		PendingPresent pending;
		{
			std::unique_lock lock(waiter->mutex);
			waiter->condition.wait(lock, [&]() { return waiter->stop || !waiter->pending.empty(); });
			if (waiter->pending.empty()) {
				return;
			}
			pending = std::move(waiter->pending.front());
			waiter->pending.pop_front();
		}

		// A presentation engine that is not showing frames (a hidden window) never completes a present; the
		// wait gives up after a while and the present counts as done, as it does without present_wait.
		for (int attempt = 0; attempt < 2; ++attempt) {
			auto result = DynamicVK::vkWaitForPresentKHR(privateDevice->device(), pending.state->swapchain, pending.id, 500000000ULL);
			if (result != VK_TIMEOUT) {
				break;
			}
		}

		pending.callback(true);
	}
};

IndiumKit::PrivateLayer::~PrivateLayer() {
	if (_waiter.joinable()) {
		{
			std::scoped_lock lock(_waiterState->mutex);
			_waiterState->stop = true;
		}
		_waiterState->condition.notify_all();
		if (_waiter.get_id() == std::this_thread::get_id()) {
			// the last reference went in a callback that runs on the waiter itself
			_waiter.detach();
		} else {
			_waiter.join();
		}
	}

	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);

	if (_acquireFence != VK_NULL_HANDLE) {
		if (_acquirePending) {
			// the image's semaphore goes back to the device's pool once it has been signaled
			DynamicVK::vkWaitForFences(privateDevice->device(), 1, &_acquireFence, VK_TRUE, 1000000000ULL);
		}
		DynamicVK::vkDestroyFence(privateDevice->device(), _acquireFence, nullptr);
	}
	_acquiredSemaphore.reset();
	_acquiredState.reset();

	// the swapchains must go before the surface they present to
	_state.reset();

	DynamicVK::vkDestroySurfaceKHR(Indium::globalInstance, _surface, nullptr);
};

// An extended-range layer renders to textures of its own (see SwapchainState::ExtendedRangePresentation), so
// the swapchain only has to show their values. Linear light in 16-bit floats shows all of them, those beyond 0..1
// included, to a display that can; otherwise the values are clamped to what an 8-bit surface shows, for which the
// sRGB formats are written to a swapchain format that applies the curve and the others to one that does not.
bool IndiumKit::PrivateLayer::chooseExtendedRangeTarget(Indium::PixelFormat pixelFormat, const std::vector<VkSurfaceFormatKHR>& surfaceFormats, ExtendedRangeTarget& target) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	if (!privateDevice->extendedRangeEnabled()) {
		return false;
	}
	auto offers = [&](VkFormat format, VkColorSpaceKHR colorSpace) {
		auto found = std::any_of(surfaceFormats.begin(), surfaceFormats.end(), [&](const VkSurfaceFormatKHR& f) {
			return f.format == format && f.colorSpace == colorSpace;
		});
		if (!found) {
			return false;
		}
		// the conversion is a blit into the image
		VkFormatProperties properties {};
		DynamicVK::vkGetPhysicalDeviceFormatProperties(privateDevice->physicalDevice(), format, &properties);
		return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
	};

	if (offers(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)) {
		target = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT, true };
		return true;
	}
	bool srgb = Indium::ExtendedRange::isSRGB(pixelFormat);
	for (VkFormat candidate: srgb ? std::initializer_list<VkFormat> { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB } : std::initializer_list<VkFormat> { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM }) {
		if (offers(candidate, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
			target = { candidate, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, false };
			return true;
		}
	}
	return false;
};

static VkColorSpaceKHR vulkanColorSpace(IndiumKit::ColorSpace colorSpace) {
	switch (colorSpace) {
		case IndiumKit::ColorSpace::SRGB:                    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		case IndiumKit::ColorSpace::ExtendedSRGB:            return VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT;
		case IndiumKit::ColorSpace::ExtendedLinearSRGB:      return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
		case IndiumKit::ColorSpace::ExtendedLinearDisplayP3: return VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT;
		case IndiumKit::ColorSpace::ExtendedLinearITUR2020:  return VK_COLOR_SPACE_BT2020_LINEAR_EXT;
	}
	return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
};

std::vector<VkSurfaceFormatKHR> IndiumKit::PrivateLayer::surfaceFormats() {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	uint32_t count = 0;
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, formats.data());
	formats.resize(count);
	return formats;
};

// Decides how the content of the layer reaches the surface (see PresentationTarget):
//  - an extended-range pixel format always goes through textures of its own (chooseExtendedRangeTarget());
//  - SDR content, and content with an HDR transfer function, is presented as it is in the color space of the
//    transfer function, or in sRGB when the surface cannot present that;
//  - the other color spaces are presented as they are when the surface offers the layer's format in the matching
//    Vulkan color space. Otherwise values with the sRGB primaries are converted for an 8-bit sRGB surface,
//    which clamps them to 0 to 1 and applies the curve to linear ones; what has no such conversion is refused,
//    so that wrong colors are never shown without saying so.
// Linear light that must not exceed the SDR range (extended dynamic range off) takes the conversion first, too,
// since the clamp is what keeps values beyond 0 to 1 from the display.
bool IndiumKit::PrivateLayer::choosePresentationTarget(Indium::PixelFormat pixelFormat, HdrTransferFunction transfer, ColorSpace colorSpace, bool extendedDynamicRange,
                                                       const std::vector<VkSurfaceFormatKHR>& formats, PresentationTarget& target) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	auto offers = [&](VkFormat format, VkColorSpaceKHR space) {
		return std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
			return f.format == format && f.colorSpace == space;
		});
	};

	if (Indium::pixelFormatIsExtendedRange(pixelFormat)) {
		// the format of the texture the drawables render to is not the swapchain's, and neither an HDR transfer function
		// nor a color space applies to values that are shown as they are
		ExtendedRangeTarget extendedRangeTarget {};
		if (!chooseExtendedRangeTarget(pixelFormat, formats, extendedRangeTarget)) {
			return false;
		}
		target = { extendedRangeTarget.format, extendedRangeTarget.colorSpace, true, extendedRangeTarget.linearLight && !Indium::ExtendedRange::isSRGB(pixelFormat) };
		return true;
	}

	VkFormat format = Indium::pixelFormatToVkFormat(pixelFormat);
	if (transfer != HdrTransferFunction::None || colorSpace == ColorSpace::SRGB) {
		// A surface that cannot present the requested HDR transfer function still presents the same pixels
		// as SDR; the metadata is not applied to it (see applyHdrMetadata()).
		for (VkColorSpaceKHR space: { hdrColorSpace(transfer), VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) {
			if (offers(format, space)) {
				target = { format, space, false, false };
				return true;
			}
		}
		return false;
	}

	const VkColorSpaceKHR wanted = vulkanColorSpace(colorSpace);
	const bool linearSRGB = colorSpace == ColorSpace::ExtendedLinearSRGB;
	const bool clamped = linearSRGB && !extendedDynamicRange;
	if (!clamped && offers(format, wanted)) {
		target = { format, wanted, false, false };
		return true;
	}

	if (colorSpace == ColorSpace::ExtendedSRGB || linearSRGB) {
		// Values with the sRGB primaries are converted by a blit from a texture of the layer's format into an 8-bit
		// image: an sRGB one applies the curve to linear values, the others leave encoded values as they are.
		auto features = [&](VkFormat f) {
			VkFormatProperties properties {};
			DynamicVK::vkGetPhysicalDeviceFormatProperties(privateDevice->physicalDevice(), f, &properties);
			return properties.optimalTilingFeatures;
		};
		if (format != VK_FORMAT_UNDEFINED && (features(format) & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
			for (VkFormat candidate: linearSRGB ? std::initializer_list<VkFormat> { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB }
			                                    : std::initializer_list<VkFormat> { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM }) {
				if (offers(candidate, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) && (features(candidate) & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
					target = { candidate, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, true, false };
					return true;
				}
			}
		}
	}

	// the extended range could not be clamped, but the surface can show the values
	if (clamped && offers(format, wanted)) {
		target = { format, wanted, false, false };
		return true;
	}
	// encoded values within 0 to 1 are shown right by an sRGB surface that has the format
	if (colorSpace == ColorSpace::ExtendedSRGB && offers(format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
		target = { format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, false, false };
		return true;
	}
	return false;
};

// Makes the textures the drawables of the swapchain render to, and records for each image the conversion into it: the
// values of the texture are converted to linear light first if the swapchain takes that (`decode`) and are
// blitted into the image then. The blit converts between the formats, clamps for an integer format, and applies the
// curve for an sRGB one.
bool IndiumKit::PrivateLayer::createExtendedRangePresentation(const std::shared_ptr<SwapchainState>& state, bool decode) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	VkDevice vkDevice = privateDevice->device();
	auto& presentation = state->extendedRange;

	Indium::TextureUsage usage = Indium::TextureUsage::RenderTarget | Indium::TextureUsage::ShaderRead;
	if (!state->framebufferOnly) {
		usage = usage | Indium::TextureUsage::ShaderWrite;
	}
	for (size_t i = 0; i < state->images.size(); ++i) {
		auto descriptor = Indium::TextureDescriptor::texture2DDescriptor(state->pixelFormat, state->width, state->height, false);
		descriptor.resourceOptions = Indium::ResourceOptions::StorageModePrivate;
		descriptor.usage = usage;
		auto texture = _device->newTexture(descriptor);
		if (!texture) {
			return false;
		}
		presentation.textures.push_back(texture);
		presentation.presentSemaphores.push_back(privateDevice->getWrappedBinarySemaphore());
	}

	VkCommandPoolCreateInfo poolInfo {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = *privateDevice->graphicsQueueFamilyIndex();
	if (DynamicVK::vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &presentation.commandPool) != VK_SUCCESS) {
		return false;
	}
	presentation.commandBuffers.resize(state->images.size());
	VkCommandBufferAllocateInfo allocateInfo {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = presentation.commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = static_cast<uint32_t>(state->images.size());
	if (DynamicVK::vkAllocateCommandBuffers(vkDevice, &allocateInfo, presentation.commandBuffers.data()) != VK_SUCCESS) {
		return false;
	}

	std::vector<VkDescriptorSet> sets(state->images.size(), VK_NULL_HANDLE);
	if (decode) {
		VkDescriptorPoolSize poolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<uint32_t>(state->images.size()) };
		VkDescriptorPoolCreateInfo descriptorPoolInfo {};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.maxSets = static_cast<uint32_t>(state->images.size());
		descriptorPoolInfo.poolSizeCount = 1;
		descriptorPoolInfo.pPoolSizes = &poolSize;
		if (DynamicVK::vkCreateDescriptorPool(vkDevice, &descriptorPoolInfo, nullptr, &presentation.descriptorPool) != VK_SUCCESS) {
			return false;
		}
		std::vector<VkDescriptorSetLayout> layouts(state->images.size(), privateDevice->extendedRangeKernel().setLayout());
		VkDescriptorSetAllocateInfo setInfo {};
		setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		setInfo.descriptorPool = presentation.descriptorPool;
		setInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		setInfo.pSetLayouts = layouts.data();
		if (DynamicVK::vkAllocateDescriptorSets(vkDevice, &setInfo, sets.data()) != VK_SUCCESS) {
			return false;
		}
	}

	for (size_t i = 0; i < state->images.size(); ++i) {
		auto texture = std::dynamic_pointer_cast<Indium::PrivateTexture>(presentation.textures[i]);
		VkCommandBuffer commandBuffer = presentation.commandBuffers[i];

		VkCommandBufferBeginInfo beginInfo {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		DynamicVK::vkBeginCommandBuffer(commandBuffer, &beginInfo);

		if (decode) {
			VkMemoryBarrier before {};
			before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
			privateDevice->extendedRangeKernel().record(commandBuffer, sets[i], texture->imageView(), static_cast<uint32_t>(state->width), static_cast<uint32_t>(state->height),
				Indium::ExtendedRangeKernel::Mode::EncodedToLinear);
		}

		VkImageMemoryBarrier barriers[2] {};
		for (auto& barrier: barriers) {
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.layerCount = 1;
		}
		barriers[0].image = texture->image();
		barriers[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barriers[1].image = state->images[i];
		barriers[1].srcAccessMask = 0;
		barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

		VkImageBlit blit {};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[1] = { static_cast<int32_t>(state->width), static_cast<int32_t>(state->height), 1 };
		blit.dstSubresource = blit.srcSubresource;
		blit.dstOffsets[1] = blit.srcOffsets[1];
		DynamicVK::vkCmdBlitImage(commandBuffer, texture->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, state->images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

		barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barriers[1].dstAccessMask = 0;
		barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		DynamicVK::vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);

		if (DynamicVK::vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			return false;
		}
	}
	return true;
};

std::shared_ptr<IndiumKit::SwapchainState> IndiumKit::PrivateLayer::createSwapchain(VkSwapchainKHR oldSwapchain) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	auto physicalDevice = privateDevice->physicalDevice();

	VkSurfaceCapabilitiesKHR caps {};
	if (DynamicVK::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, _surface, &caps) != VK_SUCCESS) {
		return nullptr;
	}

	uint32_t count = 0;
	std::vector<VkSurfaceFormatKHR> formats;
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, _surface, &count, nullptr);
	formats.resize(count);
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, _surface, &count, formats.data());
	formats.resize(count);

	std::vector<VkPresentModeKHR> presentModes;
	count = 0;
	DynamicVK::vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, _surface, &count, nullptr);
	presentModes.resize(count);
	DynamicVK::vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, _surface, &count, presentModes.data());
	presentModes.resize(count);

	auto hasMode = [&](VkPresentModeKHR mode) {
		return std::find(presentModes.begin(), presentModes.end(), mode) != presentModes.end();
	};

	PresentationTarget presentation;
	if (!choosePresentationTarget(_pixelFormat, _hdrMetadata.transfer, _colorSpace, _extendedDynamicRange, formats, presentation)) {
		return nullptr;
	}
	const VkFormat format = presentation.format;
	const VkColorSpaceKHR colorSpace = presentation.colorSpace;
	const bool extendedRange = presentation.intermediate;

	// A surface that has a size of its own (an X11 or Wayland window reports the size it has)
	// dictates the images' size; one that reports "undefined" takes whatever is asked for.
	VkExtent2D extent {};
	if (caps.currentExtent.width != UINT32_MAX) {
		extent = caps.currentExtent;
	} else {
		extent.width = std::clamp<uint32_t>(_width, caps.minImageExtent.width, caps.maxImageExtent.width);
		extent.height = std::clamp<uint32_t>(_height, caps.minImageExtent.height, caps.maxImageExtent.height);
	}
	if (extent.width == 0 || extent.height == 0) {
		// a minimized or zero-sized surface cannot have a swapchain
		return nullptr;
	}

	// Vulkan lets the application hold (minImageCount - 1) images less than the swapchain has, as the
	// presentation engine keeps minImageCount - 1 of them for itself, so room for the requested number
	// of drawables is that number plus the engine's share.
	uint32_t imageCount = std::max<uint32_t>(_imageCount, 1) + std::max<uint32_t>(caps.minImageCount, 1) - 1;
	imageCount = std::max(imageCount, caps.minImageCount);
	if (caps.maxImageCount != 0) {
		imageCount = std::min(imageCount, caps.maxImageCount);
	}

	VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (extendedRange) {
		// the images are written by the blit of the conversion, and only drawn to when the drawables are the images
		usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (!(caps.supportedUsageFlags & usage)) {
			return nullptr;
		}
	} else if (!_framebufferOnly) {
		usage |= (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) & caps.supportedUsageFlags;
	}

	VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if (!(caps.supportedCompositeAlpha & compositeAlpha)) {
		for (auto candidate: { VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR }) {
			if (caps.supportedCompositeAlpha & candidate) {
				compositeAlpha = candidate;
				break;
			}
		}
	}

	// FIFO is the only mode every implementation has. Without display sync the frames are not held back
	// for the vertical blank, which IMMEDIATE does (tearing) and MAILBOX does by replacing the queued frame.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!_displaySync) {
		if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
			presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		} else if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR)) {
			presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		}
	}

	VkSwapchainCreateInfoKHR swapchainInfo {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = _surface;
	swapchainInfo.minImageCount = imageCount;
	swapchainInfo.imageFormat = format;
	swapchainInfo.imageColorSpace = colorSpace;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = usage;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = caps.currentTransform;
	swapchainInfo.compositeAlpha = compositeAlpha;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	swapchainInfo.oldSwapchain = oldSwapchain;

	auto state = std::make_shared<SwapchainState>();
	state->device = privateDevice;
	state->pixelFormat = _pixelFormat;
	state->colorSpace = colorSpace;
	state->framebufferOnly = _framebufferOnly;
	state->width = extent.width;
	state->height = extent.height;

	if (DynamicVK::vkCreateSwapchainKHR(privateDevice->device(), &swapchainInfo, nullptr, &state->swapchain) != VK_SUCCESS) {
		state->swapchain = VK_NULL_HANDLE;
		return nullptr;
	}

	applyHdrMetadata(state);

	count = 0;
	DynamicVK::vkGetSwapchainImagesKHR(privateDevice->device(), state->swapchain, &count, nullptr);
	state->images.resize(count);
	DynamicVK::vkGetSwapchainImagesKHR(privateDevice->device(), state->swapchain, &count, state->images.data());
	state->images.resize(count);

	for (auto& image: state->images) {
		VkImageViewCreateInfo viewInfo {};

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView view = VK_NULL_HANDLE;
		if (DynamicVK::vkCreateImageView(privateDevice->device(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
			// the state's destructor frees the views made so far
			return nullptr;
		}
		state->imageViews.push_back(view);
	}

	if (extendedRange) {
		if (!createExtendedRangePresentation(state, presentation.decode)) {
			return nullptr;
		}
	}

	return state;
};

std::shared_ptr<IndiumKit::Drawable> IndiumKit::PrivateLayer::nextDrawable() {
	return nextDrawable(1000000000ULL);
};

std::shared_ptr<IndiumKit::Drawable> IndiumKit::PrivateLayer::nextDrawable(uint64_t timeoutNanoseconds) {
	using Clock = std::chrono::steady_clock;

	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	std::scoped_lock lock(_acquireMutex);

	// A wait without limit is still bounded here, so that the call cannot overflow the clock's range;
	// the caller asked for "as long as it takes", and a year is that for a display.
	const uint64_t limit = std::min<uint64_t>(timeoutNanoseconds, 365ULL * 24 * 3600 * 1000000000ULL);
	const auto deadline = Clock::now() + std::chrono::nanoseconds(limit);
	auto remainingNs = [&]() -> uint64_t {
		auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
		return remaining > 0 ? static_cast<uint64_t>(remaining) : 0;
	};

	if (_acquireFence == VK_NULL_HANDLE) {
		VkFenceCreateInfo fenceInfo {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if (DynamicVK::vkCreateFence(privateDevice->device(), &fenceInfo, nullptr, &_acquireFence) != VK_SUCCESS) {
			return nullptr;
		}
	}

	std::shared_ptr<SwapchainState> state;

	while (!_acquirePending) {
		// the swapchain in use, replaced first when the settings or the surface changed since it was made
		{
			std::scoped_lock stateLock(_stateMutex);
			if (_outdated || !_state) {
				auto replacement = createSwapchain(_state ? _state->swapchain : VK_NULL_HANDLE);
				if (!replacement) {
					return nullptr;
				}
				_state = std::move(replacement);
				_outdated = false;
			}
			state = _state;
		}

		auto binarySemaphore = privateDevice->getWrappedBinarySemaphore();
		uint32_t index;
		auto result = DynamicVK::vkAcquireNextImageKHR(privateDevice->device(), state->swapchain, remainingNs(), binarySemaphore->semaphore, _acquireFence, &index);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// The surface changed under the swapchain (a window system reports a new size a little after the
			// window got it), so make another one for what the surface is now. Nothing was acquired, and
			// the semaphore was not used.
			markOutdated(state);
			if (remainingNs() == 0) {
				return nullptr;
			}
			continue;
		}

		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			// none was available
			return nullptr;
		}

		if (result == VK_SUBOPTIMAL_KHR) {
			markOutdated(state);
		}

		_acquirePending = true;
		_acquiredIndex = index;
		_acquiredSemaphore = std::move(binarySemaphore);
		_acquiredState = state;
	}

	// The presentation engine signals the semaphore when the image is free, and that may be long after the call
	// returned an index (a window that is hidden or minimized gets no frames, so its images are not given back).
	// Work that waited for the semaphore would never start, its command buffer would never complete, and the
	// queue behind it would stall. So the image is handed out once it is usable; if it is not within the time
	// limit, there is no drawable now, and the next call goes on waiting for the same image.
	if (DynamicVK::vkWaitForFences(privateDevice->device(), 1, &_acquireFence, VK_TRUE, remainingNs()) != VK_SUCCESS) {
		return nullptr;
	}
	DynamicVK::vkResetFences(privateDevice->device(), 1, &_acquireFence);
	_acquirePending = false;

	auto acquiredState = std::move(_acquiredState);
	_acquiredState.reset();
	return std::make_shared<IndiumKit::PrivateDrawable>(shared_from_this(), std::move(acquiredState), _acquiredIndex, std::move(_acquiredSemaphore));
};

bool IndiumKit::PrivateLayer::isCurrent(const std::shared_ptr<SwapchainState>& state) {
	std::scoped_lock lock(_stateMutex);
	return state == _state;
};

void IndiumKit::PrivateLayer::markOutdated(const std::shared_ptr<SwapchainState>& state) {
	std::scoped_lock lock(_stateMutex);
	// a stale report about a swapchain that has been replaced already says nothing about the current one
	if (state == _state) {
		_outdated = true;
	}
};

Indium::PixelFormat IndiumKit::PrivateLayer::pixelFormat() {
	std::scoped_lock lock(_stateMutex);
	return _pixelFormat;
};

bool IndiumKit::PrivateLayer::setPixelFormat(Indium::PixelFormat pixelFormat) {
	HdrTransferFunction transfer;
	ColorSpace colorSpace;
	bool extendedDynamicRange;
	{
		std::scoped_lock lock(_stateMutex);
		transfer = _hdrMetadata.transfer;
		colorSpace = _colorSpace;
		extendedDynamicRange = _extendedDynamicRange;
	}

	PresentationTarget target;
	if (!choosePresentationTarget(pixelFormat, transfer, colorSpace, extendedDynamicRange, surfaceFormats(), target)) {
		return false;
	}

	std::scoped_lock lock(_stateMutex);
	if (pixelFormat != _pixelFormat) {
		_pixelFormat = pixelFormat;
		_outdated = true;
	}
	return true;
};

bool IndiumKit::PrivateLayer::setColorSpace(ColorSpace colorSpace, bool extendedDynamicRange) {
	Indium::PixelFormat pixelFormat;
	HdrTransferFunction transfer;
	{
		std::scoped_lock lock(_stateMutex);
		pixelFormat = _pixelFormat;
		transfer = _hdrMetadata.transfer;
	}

	PresentationTarget target;
	if (!choosePresentationTarget(pixelFormat, transfer, colorSpace, extendedDynamicRange, surfaceFormats(), target)) {
		return false;
	}

	std::scoped_lock lock(_stateMutex);
	if (colorSpace != _colorSpace || extendedDynamicRange != _extendedDynamicRange) {
		_colorSpace = colorSpace;
		_extendedDynamicRange = extendedDynamicRange;
		_outdated = true;
	}
	return true;
};

void IndiumKit::PrivateLayer::setFramebufferSize(size_t width, size_t height) {
	std::scoped_lock lock(_stateMutex);
	if (width != _width || height != _height) {
		_width = width;
		_height = height;
		_outdated = true;
	}
};

size_t IndiumKit::PrivateLayer::framebufferWidth() {
	std::scoped_lock lock(_stateMutex);
	return _state ? _state->width : _width;
};

size_t IndiumKit::PrivateLayer::framebufferHeight() {
	std::scoped_lock lock(_stateMutex);
	return _state ? _state->height : _height;
};

void IndiumKit::PrivateLayer::setImageCount(uint32_t count) {
	std::scoped_lock lock(_stateMutex);
	if (count != _imageCount) {
		_imageCount = count;
		_outdated = true;
	}
};

void IndiumKit::PrivateLayer::setFramebufferOnly(bool framebufferOnly) {
	std::scoped_lock lock(_stateMutex);
	if (framebufferOnly != _framebufferOnly) {
		_framebufferOnly = framebufferOnly;
		_outdated = true;
	}
};

void IndiumKit::PrivateLayer::setDisplaySyncEnabled(bool enabled) {
	std::scoped_lock lock(_stateMutex);
	if (enabled != _displaySync) {
		_displaySync = enabled;
		_outdated = true;
	}
};

void IndiumKit::PrivateLayer::setHdrMetadata(const HdrMetadata& metadata) {
	std::scoped_lock lock(_stateMutex);
	bool transferChanged = metadata.transfer != _hdrMetadata.transfer;
	_hdrMetadata = metadata;
	if (transferChanged) {
		_outdated = true;
	} else {
		// Values of the same transfer function take effect on the swapchain already in use.
		applyHdrMetadata(_state);
	}
};

std::shared_ptr<Indium::Device> IndiumKit::PrivateLayer::device() {
	return _device;
};
