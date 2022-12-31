#include <indium/library.private.hpp>
#include <indium/device.private.hpp>
#include <indium/sampler.hpp>
#include <indium/dynamic-vk.hpp>

Indium::Function::~Function() {};
Indium::Library::~Library() {};

std::shared_ptr<Indium::Device> Indium::PrivateFunction::device() {
	return _privateDevice;
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

			auto translateAddressMode = [](Iridium::EmbeddedSampler::AddressMode irAddrMode) {
				switch (irAddrMode) {
					case Iridium::EmbeddedSampler::AddressMode::ClampToZero:
						return SamplerAddressMode::ClampToZero;
					case Iridium::EmbeddedSampler::AddressMode::ClampToEdge:
						return SamplerAddressMode::ClampToEdge;
					case Iridium::EmbeddedSampler::AddressMode::Repeat:
						return SamplerAddressMode::Repeat;
					case Iridium::EmbeddedSampler::AddressMode::MirrorRepeat:
						return SamplerAddressMode::MirrorRepeat;
					case Iridium::EmbeddedSampler::AddressMode::ClampToBorderColor:
						return SamplerAddressMode::ClampToBorderColor;
					default:
						return SamplerAddressMode::ClampToEdge;
				}
			};

			auto translateFilter = [](Iridium::EmbeddedSampler::Filter irFilter) {
				switch (irFilter) {
					case Iridium::EmbeddedSampler::Filter::Nearest:
						return SamplerMinMagFilter::Nearest;
					case Iridium::EmbeddedSampler::Filter::Linear:
						return SamplerMinMagFilter::Linear;
					default:
						return SamplerMinMagFilter::Nearest;
				}
			};

			auto translateMipFilter = [](Iridium::EmbeddedSampler::MipFilter irMipFilter) {
				switch (irMipFilter) {
					case Iridium::EmbeddedSampler::MipFilter::None:
						return SamplerMipFilter::NotMipmapped;
					case Iridium::EmbeddedSampler::MipFilter::Nearest:
						return SamplerMipFilter::Nearest;
					case Iridium::EmbeddedSampler::MipFilter::Linear:
						return SamplerMipFilter::Linear;
					default:
						return SamplerMipFilter::NotMipmapped;
				}
			};

			auto translateBorderColor = [](Iridium::EmbeddedSampler::BorderColor irBorderColor) {
				switch (irBorderColor) {
					case Iridium::EmbeddedSampler::BorderColor::TransparentBlack:
						return SamplerBorderColor::TransparentBlack;
					case Iridium::EmbeddedSampler::BorderColor::OpaqueBlack:
						return SamplerBorderColor::OpaqueBlack;
					case Iridium::EmbeddedSampler::BorderColor::OpaqueWhite:
						return SamplerBorderColor::OpaqueWhite;
					default:
						return SamplerBorderColor::TransparentBlack;
				}
			};

			auto translateCompareFunction = [](Iridium::EmbeddedSampler::CompareFunction irCompareFunction) {
				switch (irCompareFunction) {
					case Iridium::EmbeddedSampler::CompareFunction::None:
						return CompareFunction::Never;
					case Iridium::EmbeddedSampler::CompareFunction::Less:
						return CompareFunction::Less;
					case Iridium::EmbeddedSampler::CompareFunction::LessEqual:
						return CompareFunction::LessEqual;
					case Iridium::EmbeddedSampler::CompareFunction::Greater:
						return CompareFunction::Greater;
					case Iridium::EmbeddedSampler::CompareFunction::GreaterEqual:
						return CompareFunction::GreaterEqual;
					case Iridium::EmbeddedSampler::CompareFunction::Equal:
						return CompareFunction::Equal;
					case Iridium::EmbeddedSampler::CompareFunction::NotEqual:
						return CompareFunction::NotEqual;
					case Iridium::EmbeddedSampler::CompareFunction::Always:
						return CompareFunction::Always;
					case Iridium::EmbeddedSampler::CompareFunction::Never:
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
	return std::make_shared<PrivateFunction>(shared_from_this(), name, _functionInfos[name]);
};
