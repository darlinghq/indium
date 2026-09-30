#include <indium/library.private.hpp>
#include <indium/device.private.hpp>
#include <indium/sampler.hpp>
#include <indium/dynamic-vk.hpp>

Indium::Function::~Function() {};
Indium::Library::~Library() {};

std::shared_ptr<Indium::Device> Indium::PrivateFunction::device() {
	return _privateDevice;
};

Indium::FunctionType Indium::PrivateFunction::functionType() const {
	return _functionInfo.functionType;
};

const std::vector<Indium::ArgumentInfo>& Indium::PrivateFunction::argumentInfo() const {
	return _functionInfo.arguments;
};

std::shared_ptr<Indium::Device> Indium::PrivateLibrary::device() {
	return _privateDevice;
};

Indium::PrivateFunction::PrivateFunction(std::shared_ptr<PrivateLibrary> library, const std::string& name, const FunctionInfo& functionInfo):
	_library(library),
	_privateDevice(library->privateDevice()),
	_functionInfo(functionInfo)
{
	_name = name;
};

Indium::PrivateLibrary::PrivateLibrary(std::shared_ptr<PrivateDevice> device, const char* data, size_t dataLength, std::unordered_map<std::string, FunctionInfo> functionInfos):
	_privateDevice(device),
	_functionInfos(functionInfos)
{
	VkShaderModuleCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = dataLength;
	createInfo.pCode = reinterpret_cast<const uint32_t*>(data);

	if (DynamicVK::vkCreateShaderModule(_privateDevice->device(), &createInfo, nullptr, &_shaderModule) != VK_SUCCESS) {
		// TODO
		abort();
	}

	// create sampler states for embedded samplers
	for (auto& [name, funcInfo]: _functionInfos) {
		for (const auto& embeddedSampler: funcInfo.embeddedSamplers) {
			SamplerDescriptor descriptor {};

			auto translateAddressMode = [](EmbeddedSamplerDescriptor::AddressMode irAddrMode) {
				switch (irAddrMode) {
					case EmbeddedSamplerDescriptor::AddressMode::ClampToZero:
						return SamplerAddressMode::ClampToZero;
					case EmbeddedSamplerDescriptor::AddressMode::ClampToEdge:
						return SamplerAddressMode::ClampToEdge;
					case EmbeddedSamplerDescriptor::AddressMode::Repeat:
						return SamplerAddressMode::Repeat;
					case EmbeddedSamplerDescriptor::AddressMode::MirrorRepeat:
						return SamplerAddressMode::MirrorRepeat;
					case EmbeddedSamplerDescriptor::AddressMode::ClampToBorderColor:
						return SamplerAddressMode::ClampToBorderColor;
					default:
						return SamplerAddressMode::ClampToEdge;
				}
			};

			auto translateFilter = [](EmbeddedSamplerDescriptor::Filter irFilter) {
				switch (irFilter) {
					case EmbeddedSamplerDescriptor::Filter::Nearest:
						return SamplerMinMagFilter::Nearest;
					case EmbeddedSamplerDescriptor::Filter::Linear:
						return SamplerMinMagFilter::Linear;
					default:
						return SamplerMinMagFilter::Nearest;
				}
			};

			auto translateMipFilter = [](EmbeddedSamplerDescriptor::MipFilter irMipFilter) {
				switch (irMipFilter) {
					case EmbeddedSamplerDescriptor::MipFilter::None:
						return SamplerMipFilter::NotMipmapped;
					case EmbeddedSamplerDescriptor::MipFilter::Nearest:
						return SamplerMipFilter::Nearest;
					case EmbeddedSamplerDescriptor::MipFilter::Linear:
						return SamplerMipFilter::Linear;
					default:
						return SamplerMipFilter::NotMipmapped;
				}
			};

			auto translateBorderColor = [](EmbeddedSamplerDescriptor::BorderColor irBorderColor) {
				switch (irBorderColor) {
					case EmbeddedSamplerDescriptor::BorderColor::TransparentBlack:
						return SamplerBorderColor::TransparentBlack;
					case EmbeddedSamplerDescriptor::BorderColor::OpaqueBlack:
						return SamplerBorderColor::OpaqueBlack;
					case EmbeddedSamplerDescriptor::BorderColor::OpaqueWhite:
						return SamplerBorderColor::OpaqueWhite;
					default:
						return SamplerBorderColor::TransparentBlack;
				}
			};

			auto translateCompareFunction = [](EmbeddedSamplerDescriptor::CompareFunction irCompareFunction) {
				switch (irCompareFunction) {
					case EmbeddedSamplerDescriptor::CompareFunction::None:
						return CompareFunction::Never;
					case EmbeddedSamplerDescriptor::CompareFunction::Less:
						return CompareFunction::Less;
					case EmbeddedSamplerDescriptor::CompareFunction::LessEqual:
						return CompareFunction::LessEqual;
					case EmbeddedSamplerDescriptor::CompareFunction::Greater:
						return CompareFunction::Greater;
					case EmbeddedSamplerDescriptor::CompareFunction::GreaterEqual:
						return CompareFunction::GreaterEqual;
					case EmbeddedSamplerDescriptor::CompareFunction::Equal:
						return CompareFunction::Equal;
					case EmbeddedSamplerDescriptor::CompareFunction::NotEqual:
						return CompareFunction::NotEqual;
					case EmbeddedSamplerDescriptor::CompareFunction::Always:
						return CompareFunction::Always;
					case EmbeddedSamplerDescriptor::CompareFunction::Never:
						return CompareFunction::Never;
					default:
						return CompareFunction::Never;
				}
			};

			descriptor.minFilter = translateFilter(embeddedSampler.minificationFilter);
			descriptor.magFilter = translateFilter(embeddedSampler.magnificationFilter);
			descriptor.mipFilter = translateMipFilter(embeddedSampler.mipmapFilter);
			descriptor.maxAnisotropy = embeddedSampler.anisotropyLevel;
			descriptor.sAddressMode = translateAddressMode(embeddedSampler.widthAddressMode);
			descriptor.tAddressMode = translateAddressMode(embeddedSampler.heightAddressMode);
			descriptor.rAddressMode = translateAddressMode(embeddedSampler.depthAddressMode);
			descriptor.borderColor = translateBorderColor(embeddedSampler.borderColor);
			descriptor.normalizedCoordinates = embeddedSampler.usesNormalizedCoordinates;
			descriptor.lodMinClamp = embeddedSampler.lodMin;
			descriptor.lodMaxClamp = embeddedSampler.lodMax;
			descriptor.supportArgumentBuffers = false;
			descriptor.compareFunction = translateCompareFunction(embeddedSampler.compareFunction);

			funcInfo.embeddedSamplerStates.push_back(_privateDevice->newSamplerState(descriptor));
		}
	}
};

Indium::PrivateLibrary::~PrivateLibrary() {
	DynamicVK::vkDestroyShaderModule(_privateDevice->device(), _shaderModule, nullptr);
};

std::shared_ptr<Indium::Function> Indium::PrivateLibrary::newFunction(const std::string& name) {
	// Not operator[]: a missing key would default-construct a FunctionInfo and
	// hand back a Function for a name the library does not have, so the caller
	// could never see a null.
	const auto info = _functionInfos.find(name);
	if (info == _functionInfos.end()) {
		return nullptr;
	}

	return std::make_shared<PrivateFunction>(shared_from_this(), name, info->second);
};
