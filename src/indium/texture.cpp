#include <indium/texture.private.hpp>
#include <indium/device.private.hpp>
#include <indium/types.private.hpp>

Indium::Texture::~Texture() {};
Indium::PrivateTexture::~PrivateTexture() {};

std::shared_ptr<Indium::Texture> Indium::Texture::parentTexture() const {
	return nullptr;
};

size_t Indium::Texture::parentRelativeLevel() const {
	return 0;
};

size_t Indium::Texture::parentRelativeSlice() const {
	return 0;
};

std::shared_ptr<Indium::Buffer> Indium::Texture::buffer() const {
	return nullptr;
};

size_t Indium::Texture::bufferOffset() const {
	return 0;
};

size_t Indium::Texture::bufferBytesPerRow() const {
	return 0;
};

std::shared_ptr<Indium::Texture> Indium::PrivateTexture::newTextureView(PixelFormat pixelFormat) {
	return newTextureView(
		pixelFormat,
		textureType(),
		Range<uint32_t> { static_cast<uint32_t>(parentRelativeLevel()), static_cast<uint32_t>(mipmapLevelCount()) },
		Range<uint32_t> { static_cast<uint32_t>(parentRelativeSlice()), static_cast<uint32_t>(arrayLength()) }
	);
};

std::shared_ptr<Indium::Texture> Indium::PrivateTexture::newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<uint32_t>& levels, const Range<uint32_t>& layers) {
	return newTextureView(
		pixelFormat,
		textureType,
		levels,
		layers,
		swizzle()
	);
};

std::shared_ptr<Indium::Texture> Indium::PrivateTexture::newTextureView(PixelFormat pixelFormat, TextureType textureType, const Range<uint32_t>& levels, const Range<uint32_t>& layers, const TextureSwizzleChannels& swizzle) {
	return std::make_shared<Indium::TextureView>(
		shared_from_this(),
		pixelFormat,
		textureType,
		VK_IMAGE_ASPECT_COLOR_BIT, // TODO
		swizzle,
		levels,
		layers
	);
};

Indium::TextureView::TextureView(std::shared_ptr<PrivateTexture> original, PixelFormat pixelFormat, TextureType textureType, VkImageAspectFlags imageAspect, TextureSwizzleChannels swizzle, const Range<uint32_t>& levels, const Range<uint32_t>& layers):
	PrivateTexture(std::dynamic_pointer_cast<PrivateDevice>(original->device())),
	_original(original),
	_pixelFormat(pixelFormat),
	_textureType(textureType),
	_imageAspect(imageAspect),
	_levels(levels),
	_layers(layers),
	_swizzle(swizzle)
{
	auto parentLevelStart = _original->parentRelativeLevel();
	auto parentLevelEnd = parentLevelStart + _original->mipmapLevelCount() - 1;
	auto levelStart = std::min(parentLevelStart + _levels.start, parentLevelEnd);
	auto levelEnd = std::min(levelStart + _levels.length - 1, parentLevelEnd);

	auto parentLayerStart = _original->parentRelativeSlice();
	auto parentLayerEnd = parentLayerStart + _original->arrayLength() - 1;
	auto layerStart = std::min(parentLayerStart + _layers.start, parentLayerEnd);
	auto layerEnd = std::min(layerStart + _layers.length - 1, parentLayerEnd);

	VkImageViewCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = _original->image();
	info.viewType = textureTypeToVkImageViewType(_textureType);
	info.format = pixelFormatToVkFormat(_pixelFormat);
	info.components.r = textureSwizzleToVkComponentSwizzle(_swizzle.red);
	info.components.g = textureSwizzleToVkComponentSwizzle(_swizzle.green);
	info.components.b = textureSwizzleToVkComponentSwizzle(_swizzle.blue);
	info.components.a = textureSwizzleToVkComponentSwizzle(_swizzle.alpha);
	info.subresourceRange.aspectMask = _imageAspect;
	info.subresourceRange.baseMipLevel = levelStart;
	info.subresourceRange.levelCount = levelEnd - levelStart + 1;
	info.subresourceRange.baseArrayLayer = layerStart;
	info.subresourceRange.layerCount = layerEnd - layerStart + 1;

	if (vkCreateImageView(std::dynamic_pointer_cast<PrivateDevice>(_original->device())->device(), &info, nullptr, &_imageView) != VK_SUCCESS) {
		// TODO
		abort();
	}
};

Indium::TextureView::~TextureView() {
	vkDestroyImageView(std::dynamic_pointer_cast<PrivateDevice>(_original->device())->device(), _imageView, nullptr);
};

Indium::PixelFormat Indium::TextureView::pixelFormat() const {
	return _pixelFormat;
};

Indium::TextureType Indium::TextureView::textureType() const {
	return _textureType;
};

size_t Indium::TextureView::width() const {
	return _original->width();
};

size_t Indium::TextureView::height() const {
	return _original->height();
};

size_t Indium::TextureView::depth() const {
	return _original->depth();
};

size_t Indium::TextureView::mipmapLevelCount() const {
	return _levels.length;
};

size_t Indium::TextureView::arrayLength() const {
	return _layers.length;
};

size_t Indium::TextureView::sampleCount() const {
	return _original->sampleCount();
};

bool Indium::TextureView::framebufferOnly() const {
	return _original->framebufferOnly();
};

bool Indium::TextureView::allowGPUOptimizedContents() const {
	return _original->allowGPUOptimizedContents();
};

bool Indium::TextureView::shareable() const {
	return _original->shareable();
};

Indium::TextureSwizzleChannels Indium::TextureView::swizzle() const {
	return _swizzle;
};

std::shared_ptr<Indium::Texture> Indium::TextureView::parentTexture() const {
	return _original;
};

size_t Indium::TextureView::parentRelativeLevel() const {
	return _levels.start;
};

size_t Indium::TextureView::parentRelativeSlice() const {
	return _layers.start;
};

VkImageView Indium::TextureView::imageView() {
	return _imageView;
};

VkImage Indium::TextureView::image() {
	return _original->image();
};

std::shared_ptr<Indium::Device> Indium::TextureView::device() {
	return _original->device();
};

VkImageLayout Indium::TextureView::imageLayout() {
	return _original->imageLayout();
};

const Indium::TimelineSemaphore& Indium::TextureView::acquire(uint64_t& waitValue, std::shared_ptr<BinarySemaphore>& extraWaitSemaphore, uint64_t& signalValue) {
	return _original->acquire(waitValue, extraWaitSemaphore, signalValue);
};

void Indium::TextureView::beginUpdatingPresentationSemaphore(std::shared_ptr<BinarySemaphore> presentationSemaphore) {
	return _original->beginUpdatingPresentationSemaphore(presentationSemaphore);
};

void Indium::TextureView::endUpdatingPresentationSemaphore() {
	return _original->endUpdatingPresentationSemaphore();
};

std::shared_ptr<Indium::BinarySemaphore> Indium::TextureView::synchronizePresentation() {
	return _original->synchronizePresentation();
};

const Indium::TimelineSemaphore& Indium::PrivateTexture::acquire(uint64_t& waitValue, std::shared_ptr<BinarySemaphore>& extraWaitSemaphore, uint64_t& signalValue) {
	std::unique_lock lock(_syncMutex);

	if (_extraWaitSemaphore) {
		extraWaitSemaphore = std::move(_extraWaitSemaphore);
		_extraWaitSemaphore = nullptr;
	}

	waitValue = _syncSemaphore->count;

	++_syncSemaphore->count;
	signalValue = _syncSemaphore->count;

	return *_syncSemaphore;
};

void Indium::PrivateTexture::beginUpdatingPresentationSemaphore(std::shared_ptr<BinarySemaphore> presentationSemaphore) {
	_presentationMutex.lock();
	_presentationSemaphore = presentationSemaphore;
};

void Indium::PrivateTexture::endUpdatingPresentationSemaphore() {
	_presentationMutex.unlock();
};

std::shared_ptr<Indium::BinarySemaphore> Indium::PrivateTexture::synchronizePresentation() {
	std::unique_lock lock(_presentationMutex);

	if (_presentationSemaphore) {
		auto sema = std::move(_presentationSemaphore);
		_presentationSemaphore = nullptr;
		return sema;
	}

	return nullptr;
};

Indium::PrivateTexture::PrivateTexture(std::shared_ptr<PrivateDevice> device):
	_device(device)
{
	_syncSemaphore = device->getWrappedTimelineSemaphore();
};

std::shared_ptr<Indium::Device> Indium::PrivateTexture::device() {
	return _device;
};
