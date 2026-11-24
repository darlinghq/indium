#include <indium/library.private.hpp>
#include <indium/device.private.hpp>
#include <indium/sampler.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <cstring>
#include <set>

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

const std::vector<Indium::FunctionConstantInfo>& Indium::PrivateFunction::functionConstants() const {
	return _functionInfo.functionConstants;
};

const VkSpecializationInfo* Indium::PrivateFunction::specializationInfo() const {
	return _specializationEntries.empty() ? nullptr : &_specializationInfo;
};

// A specialization constant of `type` with the value `bits`, stored as a scalar of the type's size (a VkBool32 for
// a Bool) at the start of whole words, which the compute pipeline appends to its own.
void Indium::PrivateFunction::addSpecialization(uint32_t id, FunctionConstantType type, uint64_t bits) {
	size_t size = sizeof(uint32_t);
	switch (type) {
		case FunctionConstantType::Char:
		case FunctionConstantType::UChar:
			size = sizeof(uint8_t);
			break;
		case FunctionConstantType::Short:
		case FunctionConstantType::UShort:
		case FunctionConstantType::Half:
			size = sizeof(uint16_t);
			break;
		case FunctionConstantType::Long:
		case FunctionConstantType::ULong:
			size = sizeof(uint64_t);
			break;
		default:
			break;
	}

	VkSpecializationMapEntry entry {};
	entry.constantID = id;
	entry.offset = _specializationData.size() * sizeof(uint32_t);
	entry.size = size;
	_specializationEntries.push_back(entry);

	_specializationData.resize(_specializationData.size() + (size + sizeof(uint32_t) - 1) / sizeof(uint32_t), 0);
	auto* destination = reinterpret_cast<uint8_t*>(_specializationData.data()) + entry.offset;
	uint8_t byte = static_cast<uint8_t>(bits);
	uint16_t half = static_cast<uint16_t>(bits);
	uint32_t word = static_cast<uint32_t>(bits);
	const void* source = size == sizeof(uint8_t) ? static_cast<const void*>(&byte)
		: size == sizeof(uint16_t) ? static_cast<const void*>(&half)
		: size == sizeof(uint32_t) ? static_cast<const void*>(&word) : static_cast<const void*>(&bits);
	memcpy(destination, source, size);
};

bool Indium::PrivateFunction::specialize(const FunctionConstantValues& values, std::string& errorMessage) {
	_specializationEntries.clear();
	_specializationData.clear();

	// A set of values is commonly shared by several functions, each of which declares only the constants
	// it uses, so a value for a constant this function does not have is not an error. A value of another
	// type than the constant it names is.
	for (const auto& constant: _functionInfo.functionConstants) {
		auto byIndex = values.byIndex().find(constant.index);
		auto byName = values.byName().find(constant.name);
		if ((byIndex != values.byIndex().end() && byIndex->second.type != constant.type) ||
			(byName != values.byName().end() && byName->second.type != constant.type)) {
			errorMessage = "the value for function constant " + std::to_string(constant.index) + " ('" + constant.name + "') has another type than the constant";
			return false;
		}
	}

	// by name and by index may both name a constant; the index wins
	for (const auto& constant: _functionInfo.functionConstants) {
		const FunctionConstantValues::Value* value = nullptr;
		auto byIndex = values.byIndex().find(constant.index);
		if (byIndex != values.byIndex().end()) {
			value = &byIndex->second;
		} else {
			auto byName = values.byName().find(constant.name);
			if (byName != values.byName().end()) {
				value = &byName->second;
			}
		}

		addSpecialization(constant.specializationID, constant.type, value ? value->bits : 0);
		addSpecialization(constant.definedSpecializationID, FunctionConstantType::Bool, value ? 1 : 0);
	}

	_specializationInfo.mapEntryCount = _specializationEntries.size();
	_specializationInfo.pMapEntries = _specializationEntries.data();
	_specializationInfo.dataSize = _specializationData.size() * sizeof(uint32_t);
	_specializationInfo.pData = _specializationData.data();
	return true;
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
	// the varyings of a fragment function may have to be renumbered for a vertex function (fragmentModuleLinkedTo)
	for (const auto& [name, funcInfo]: _functionInfos) {
		if (funcInfo.functionType == FunctionType::Fragment && !funcInfo.stageInterface.empty()) {
			const auto* words = reinterpret_cast<const uint32_t*>(data);
			_words.assign(words, words + dataLength / sizeof(uint32_t));
			break;
		}
	}

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
	for (auto& [key, module]: _linkedModules) {
		DynamicVK::vkDestroyShaderModule(_privateDevice->device(), module, nullptr);
	}
	DynamicVK::vkDestroyShaderModule(_privateDevice->device(), _shaderModule, nullptr);
};

namespace {
	constexpr uint32_t spirvOpEntryPoint = 15;
	constexpr uint32_t spirvOpVariable = 59;
	constexpr uint32_t spirvOpDecorate = 71;
	constexpr uint32_t spirvDecorationLocation = 30;
	constexpr uint32_t spirvStorageClassInput = 1;
	constexpr size_t spirvHeaderWords = 5;

	// The words of a literal string operand, from `first`, as a std::string; `end` is set past its last word.
	std::string spirvLiteralString(const std::vector<uint32_t>& words, size_t first, size_t limit, size_t& end) {
		std::string text;
		size_t at = first;
		for (; at < limit; ++at) {
			bool terminated = false;
			for (int byte = 0; byte < 4; ++byte) {
				char c = static_cast<char>((words[at] >> (8 * byte)) & 0xff);
				if (c == 0) {
					terminated = true;
					break;
				}
				text.push_back(c);
			}
			if (terminated) {
				++at;
				break;
			}
		}
		end = at;
		return text;
	}
};

VkShaderModule Indium::PrivateLibrary::fragmentModuleLinkedTo(const std::string& entry, const std::vector<StageInterfaceField>& vertexOutputs) {
	const auto info = _functionInfos.find(entry);
	if (info == _functionInfos.end() || info->second.stageInterface.empty() || _words.empty()) {
		return _shaderModule;
	}
	const auto& inputs = info->second.stageInterface;

	// the Location each input has to have: its partner's, and past all of the outputs' for an input no output
	// has (what it reads is undefined, as in Metal, but it must not take the place of a paired one)
	uint32_t nextFree = 0;
	for (const auto& output: vertexOutputs) {
		nextFree = std::max(nextFree, output.location + 1);
	}
	std::vector<uint32_t> wanted;
	bool unchanged = true;
	for (const auto& input: inputs) {
		uint32_t location = 0;
		auto partner = std::find_if(vertexOutputs.begin(), vertexOutputs.end(), [&](const auto& output) {
			return output.name == input.name && output.type == input.type;
		});
		location = partner != vertexOutputs.end() ? partner->location : nextFree++;
		unchanged = unchanged && location == input.location;
		wanted.push_back(location);
	}
	if (unchanged) {
		return _shaderModule;
	}

	std::lock_guard lock(_linkedMutex);
	auto key = std::make_pair(entry, wanted);
	if (auto existing = _linkedModules.find(key); existing != _linkedModules.end()) {
		return existing->second;
	}

	std::vector<uint32_t> words = _words;
	std::set<uint32_t> interfaceIDs;
	std::set<uint32_t> inputVariables;
	for (size_t at = spirvHeaderWords; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (count == 0 || at + count > words.size()) {
			return VK_NULL_HANDLE;
		}
		if (opcode == spirvOpEntryPoint && count >= 4) {
			size_t nameEnd = 0;
			auto name = spirvLiteralString(words, at + 3, at + count, nameEnd);
			if (name == entry) {
				for (size_t i = nameEnd; i < at + count; ++i) {
					interfaceIDs.insert(words[i]);
				}
			}
		} else if (opcode == spirvOpVariable && count >= 4 && words[at + 3] == spirvStorageClassInput) {
			inputVariables.insert(words[at + 2]);
		}
		at += count;
	}

	for (size_t at = spirvHeaderWords; at < words.size();) {
		uint32_t count = words[at] >> 16;
		uint32_t opcode = words[at] & 0xffff;
		if (opcode == spirvOpDecorate && count == 4 && words[at + 2] == spirvDecorationLocation
			&& interfaceIDs.count(words[at + 1]) != 0 && inputVariables.count(words[at + 1]) != 0) {
			for (size_t i = 0; i < inputs.size(); ++i) {
				if (inputs[i].location == words[at + 3]) {
					words[at + 3] = wanted[i];
					break;
				}
			}
		}
		at += count;
	}

	VkShaderModuleCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = words.size() * sizeof(uint32_t);
	createInfo.pCode = words.data();
	VkShaderModule module = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateShaderModule(_privateDevice->device(), &createInfo, nullptr, &module) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}
	_linkedModules[key] = module;
	return module;
};

std::shared_ptr<Indium::Function> Indium::PrivateLibrary::newFunction(const std::string& name) {
	// Not operator[]: a missing key would default-construct a FunctionInfo and
	// hand back a Function for a name the library does not have, so the caller
	// could never see a null.
	const auto info = _functionInfos.find(name);
	if (info == _functionInfos.end()) {
		return nullptr;
	}

	auto function = std::make_shared<PrivateFunction>(shared_from_this(), name, info->second);
	std::string message;
	function->specialize(FunctionConstantValues {}, message);
	return function;
};

std::shared_ptr<Indium::Function> Indium::PrivateLibrary::newFunction(const std::string& name, const FunctionConstantValues& values, std::string* errorMessage) {
	const auto info = _functionInfos.find(name);
	if (info == _functionInfos.end()) {
		return nullptr;
	}

	auto function = std::make_shared<PrivateFunction>(shared_from_this(), name, info->second);
	std::string message;
	if (!function->specialize(values, message)) {
		if (errorMessage) {
			*errorMessage = message;
		}
		return nullptr;
	}
	return function;
};
