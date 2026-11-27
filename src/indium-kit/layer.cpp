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
	_extraWaitSemaphore = extraWaitSemaphore;
};

IndiumKit::PrivateDrawable::~PrivateDrawable() {
	// An acquired image that is never presented cannot be handed back to the swapchain, which would
	// lose one image for good for each such drawable. Replacing the swapchain gives them all back.
	if (!_presented) {
		_layer->markOutdated(_state);
	}
};

std::shared_ptr<Indium::Texture> IndiumKit::PrivateDrawable::texture() {
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

	auto sema = synchronizePresentation();
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device());

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

bool IndiumKit::PrivateLayer::surfaceSupportsFormat(VkFormat format, VkColorSpaceKHR colorSpace) {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	uint32_t count = 0;
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(count);
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, formats.data());
	formats.resize(count);

	return std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
		return f.format == format && f.colorSpace == colorSpace;
	});
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

	VkHdrMetadataEXT metadata = IndiumKit::hdrMetadataToVulkan(_hdrMetadata);
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

	VkFormat format = Indium::pixelFormatToVkFormat(_pixelFormat);
	VkColorSpaceKHR colorSpace = hdrColorSpace(_hdrMetadata.transfer);
	bool formatSupported = std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
		return f.format == format && f.colorSpace == colorSpace;
	});

	// A surface that cannot present the requested HDR transfer function still presents the same pixels
	// as SDR; the metadata is not applied to it (see applyHdrMetadata()).
	if (!formatSupported && colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
		colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		formatSupported = std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
			return f.format == format && f.colorSpace == colorSpace;
		});
	}
	if (!formatSupported) {
		return nullptr;
	}

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
	if (!_framebufferOnly) {
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
		viewInfo.subresourceRange.aspectMask = Indium::pixelFormatToVkImageAspectFlags(_pixelFormat);
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
	VkFormat format = Indium::pixelFormatToVkFormat(pixelFormat);
	HdrTransferFunction transfer;
	{
		std::scoped_lock lock(_stateMutex);
		transfer = _hdrMetadata.transfer;
	}

	VkColorSpaceKHR colorSpace = hdrColorSpace(transfer);
	bool supported = surfaceSupportsFormat(format, colorSpace);
	if (!supported && colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
		supported = surfaceSupportsFormat(format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	}
	if (!supported) {
		return false;
	}

	std::scoped_lock lock(_stateMutex);
	if (pixelFormat != _pixelFormat) {
		_pixelFormat = pixelFormat;
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
