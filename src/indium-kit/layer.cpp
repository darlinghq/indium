#include <indium-kit/layer.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <stdexcept>

namespace DynamicVK = Indium::DynamicVK;

IndiumKit::Drawable::~Drawable() {};
IndiumKit::Layer::~Layer() {};

IndiumKit::PrivateDrawable::PrivateDrawable(std::shared_ptr<PrivateLayer> layer, VkSwapchainKHR swapchain, uint32_t swapchainImageIndex, VkImageView imageView, VkImage image, std::shared_ptr<Indium::BinarySemaphore> extraWaitSemaphore, size_t width, size_t height, Indium::PixelFormat pixelFormat):
	Indium::PrivateTexture(std::dynamic_pointer_cast<Indium::PrivateDevice>(layer->device())),
	_layer(layer),
	_swapchain(swapchain),
	_swapchainImageIndex(swapchainImageIndex),
	_imageView(imageView),
	_image(image),
	_width(width),
	_height(height),
	_pixelFormat(pixelFormat)
{
	_extraWaitSemaphore = extraWaitSemaphore;
};

IndiumKit::PrivateDrawable::~PrivateDrawable() {};

std::shared_ptr<Indium::Texture> IndiumKit::PrivateDrawable::texture() {
	return shared_from_this();
};

VkImageView IndiumKit::PrivateDrawable::imageView() {
	return _imageView;
};

VkImage IndiumKit::PrivateDrawable::image() {
	return _image;
};

Indium::TextureType IndiumKit::PrivateDrawable::textureType() const {
	return Indium::TextureType::e2D;
};

Indium::PixelFormat IndiumKit::PrivateDrawable::pixelFormat() const {
	return _pixelFormat;
};

size_t IndiumKit::PrivateDrawable::width() const {
	return _width;
};

size_t IndiumKit::PrivateDrawable::height() const {
	return _height;
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
	return true;
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
	auto sema = synchronizePresentation();
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device());

	VkPresentInfoKHR info {};
	info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	info.waitSemaphoreCount = sema ? 1 : 0;
	info.pWaitSemaphores = sema ? &sema->semaphore : nullptr;
	info.swapchainCount = 1;
	info.pSwapchains = &_swapchain;
	info.pImageIndices = &_swapchainImageIndex;
	info.pResults = nullptr;

	if (DynamicVK::vkQueuePresentKHR(privateDevice->graphicsQueue(), &info) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

void IndiumKit::PrivateDrawable::replaceRegion(Indium::Region region, size_t mipmapLevel, const void* bytes, size_t bytesPerRow) {
	throw std::runtime_error("Cannot use replaceRegion on IndiumKit::Drawable");
};

void IndiumKit::PrivateDrawable::replaceRegion(Indium::Region region, size_t mipmapLevel, size_t slice, const void* bytes, size_t bytesPerRow, size_t bytesPerImage) {
	throw std::runtime_error("Cannot use replaceRegion on IndiumKit::Drawable");
};

std::shared_ptr<IndiumKit::Layer> IndiumKit::Layer::make(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight) {
	return std::make_shared<PrivateLayer>(surface, device, framebufferWidth, framebufferHeight);
};

IndiumKit::PrivateLayer::PrivateLayer(VkSurfaceKHR surface, std::shared_ptr<Indium::Device> device, size_t framebufferWidth, size_t framebufferHeight):
	_surface(surface),
	_device(device),
	_width(framebufferWidth),
	_height(framebufferHeight)
{
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(device);

	if (!(privateDevice->features() & Indium::PrivateDevice::Feature::Swapchain)) {
		throw std::runtime_error("Device does not support swapchains");
	}

	VkSurfaceCapabilitiesKHR surfaceCaps;

	DynamicVK::vkGetPhysicalDeviceSurfaceCapabilitiesKHR(privateDevice->physicalDevice(), _surface, &surfaceCaps);

	std::vector<VkPresentModeKHR> presentModes;
	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	uint32_t count;

	DynamicVK::vkGetPhysicalDeviceSurfacePresentModesKHR(privateDevice->physicalDevice(), _surface, &count, nullptr);
	presentModes.resize(count);
	DynamicVK::vkGetPhysicalDeviceSurfacePresentModesKHR(privateDevice->physicalDevice(), _surface, &count, presentModes.data());

	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, nullptr);
	surfaceFormats.reserve(count);
	DynamicVK::vkGetPhysicalDeviceSurfaceFormatsKHR(privateDevice->physicalDevice(), _surface, &count, surfaceFormats.data());

	VkSwapchainCreateInfoKHR swapchainInfo {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = _surface;
	swapchainInfo.minImageCount = 5;
	swapchainInfo.imageFormat = Indium::pixelFormatToVkFormat(_pixelFormat);
	swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchainInfo.imageExtent.width = _width;
	swapchainInfo.imageExtent.height = _height;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = surfaceCaps.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	// TODO: allow vsync to be configured
	swapchainInfo.presentMode = /*(std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) ? VK_PRESENT_MODE_MAILBOX_KHR :*/ VK_PRESENT_MODE_FIFO_KHR;
	swapchainInfo.clipped = VK_TRUE;

	if (DynamicVK::vkCreateSwapchainKHR(privateDevice->device(), &swapchainInfo, nullptr, &_swapchain) != VK_SUCCESS) {
		// TODO
		abort();
	}

	DynamicVK::vkGetSwapchainImagesKHR(privateDevice->device(), _swapchain, &count, nullptr);
	_images.resize(count);
	DynamicVK::vkGetSwapchainImagesKHR(privateDevice->device(), _swapchain, &count, _images.data());

	for (auto& image: _images) {
		VkImageViewCreateInfo viewInfo {};

		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = Indium::pixelFormatToVkFormat(_pixelFormat);
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.subresourceRange.aspectMask = Indium::pixelFormatToVkImageAspectFlags(_pixelFormat);
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		if (DynamicVK::vkCreateImageView(privateDevice->device(), &viewInfo, nullptr, &_imageViews.emplace_back()) != VK_SUCCESS) {
			// TODO
			abort();
		}
	}
};

IndiumKit::PrivateLayer::~PrivateLayer() {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);

	for (auto& imageView: _imageViews) {
		DynamicVK::vkDestroyImageView(privateDevice->device(), imageView, nullptr);
	}

	DynamicVK::vkDestroySwapchainKHR(privateDevice->device(), _swapchain, nullptr);

	DynamicVK::vkDestroySurfaceKHR(Indium::globalInstance, _surface, nullptr);
};

std::shared_ptr<IndiumKit::Drawable> IndiumKit::PrivateLayer::nextDrawable() {
	auto privateDevice = std::dynamic_pointer_cast<Indium::PrivateDevice>(_device);
	auto binarySemaphore = privateDevice->getWrappedBinarySemaphore();
	uint32_t index;

	auto result = DynamicVK::vkAcquireNextImageKHR(privateDevice->device(), _swapchain, /* 1 sec = 1e9 nsec */ 1000000000ULL, binarySemaphore->semaphore, nullptr, &index);

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		// none was available
		return nullptr;
	}

	return std::make_shared<IndiumKit::PrivateDrawable>(shared_from_this(), _swapchain, index, _imageViews[index], _images[index], binarySemaphore, _width, _height, _pixelFormat);
};

Indium::PixelFormat IndiumKit::PrivateLayer::pixelFormat() {
	return _pixelFormat;
};

void IndiumKit::PrivateLayer::setPixelFormat(Indium::PixelFormat pixelFormat) {
	// TODO
	abort();
};

std::shared_ptr<Indium::Device> IndiumKit::PrivateLayer::device() {
	return _device;
};
