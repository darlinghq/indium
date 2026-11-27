#include <indium/library.private.hpp>
#include <indium/device.private.hpp>
#include <indium/sampler.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

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

		for (auto [id, data]: { std::pair<uint32_t, uint32_t> { constant.specializationID, value ? value->bits : 0 }, std::pair<uint32_t, uint32_t> { constant.definedSpecializationID, value ? 1 : 0 } }) {
			VkSpecializationMapEntry entry {};
			entry.constantID = id;
			entry.offset = _specializationData.size() * sizeof(uint32_t);
			entry.size = sizeof(uint32_t);
			_specializationEntries.push_back(entry);
			_specializationData.push_back(data);
		}
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

namespace {
	std::unordered_map<std::string, std::optional<std::set<uint32_t>>> spirvComparisonSamplers(const uint32_t* words, size_t wordCount);
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

	// which sampler bindings each function compares through (FunctionInfo::comparisonSamplerBindings)
	bool samplers = std::any_of(_functionInfos.begin(), _functionInfos.end(), [](const auto& entry) {
		return std::any_of(entry.second.bindings.begin(), entry.second.bindings.end(), [](const BindingDescriptor& binding) {
			return binding.type == BindingType::Sampler;
		});
	});
	if (samplers) {
		auto comparisons = spirvComparisonSamplers(reinterpret_cast<const uint32_t*>(data), dataLength / sizeof(uint32_t));
		for (auto& [name, funcInfo]: _functionInfos) {
			auto found = comparisons.find(name);
			for (const auto& binding: funcInfo.bindings) {
				if (binding.type != BindingType::Sampler) {
					continue;
				}
				if (found == comparisons.end() || !found->second) {
					funcInfo.comparisonSamplersUntraced = true;
				} else if (found->second->count(static_cast<uint32_t>(binding.internalIndex)) != 0) {
					funcInfo.comparisonSamplerBindings.insert(binding.internalIndex);
				}
			}
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

	constexpr uint32_t spirvOpFunction = 54;
	constexpr uint32_t spirvOpFunctionParameter = 55;
	constexpr uint32_t spirvOpFunctionEnd = 56;
	constexpr uint32_t spirvOpFunctionCall = 57;
	constexpr uint32_t spirvOpLoad = 61;
	constexpr uint32_t spirvOpAccessChain = 65;
	constexpr uint32_t spirvOpInBoundsAccessChain = 66;
	constexpr uint32_t spirvOpCopyObject = 83;
	constexpr uint32_t spirvOpSampledImage = 86;
	constexpr uint32_t spirvDecorationBinding = 33;

	// The opcodes that sample or gather with a depth comparison. Each has its sampled image as its third operand.
	bool spirvIsDrefOp(uint32_t opcode) {
		switch (opcode) {
			case 89: // OpImageSampleDrefImplicitLod
			case 90: // OpImageSampleDrefExplicitLod
			case 93: // OpImageSampleProjDrefImplicitLod
			case 94: // OpImageSampleProjDrefExplicitLod
			case 97: // OpImageDrefGather
			case 307: // OpImageSparseSampleDrefImplicitLod
			case 308: // OpImageSparseSampleDrefExplicitLod
			case 311: // OpImageSparseSampleProjDrefImplicitLod
			case 312: // OpImageSparseSampleProjDrefExplicitLod
			case 315: // OpImageSparseDrefGather
				return true;
			default:
				return false;
		}
	};

	std::string spirvLiteralString(const std::vector<uint32_t>& words, size_t first, size_t limit, size_t& end);

	// For each entry point of a module, by name, the Binding of every sampler a depth comparison goes through in it or
	// in a function it calls. A sampler is followed back to its variable through loads, access chains, copies, and
	// from a function parameter to the arguments the entry point's calls pass for it. std::nullopt for an entry point
	// with a comparison whose sampler cannot be followed back to a variable that way (through an OpPhi, say).
	std::unordered_map<std::string, std::optional<std::set<uint32_t>>> spirvComparisonSamplers(const uint32_t* data, size_t wordCount) {
		std::unordered_map<std::string, std::optional<std::set<uint32_t>>> result;
		if (wordCount < spirvHeaderWords) {
			return result;
		}
		std::vector<uint32_t> words(data, data + wordCount);

		struct Call {
			uint32_t callee;
			std::vector<uint32_t> arguments;
		};
		struct FunctionUse {
			std::vector<uint32_t> comparedImages;
			std::vector<Call> calls;
		};
		std::unordered_map<std::string, uint32_t> entryPoints;
		std::unordered_map<uint32_t, uint32_t> bindings;
		std::unordered_map<uint32_t, uint32_t> sources; // the value or pointer an id was loaded or derived from
		std::unordered_map<uint32_t, uint32_t> sampledSamplers; // OpSampledImage result -> its sampler
		std::unordered_map<uint32_t, std::pair<uint32_t, size_t>> parameters; // parameter -> its function, position
		std::unordered_set<uint32_t> variables;
		std::unordered_map<uint32_t, FunctionUse> functions;
		uint32_t currentID = 0;
		size_t parameterCount = 0;
		FunctionUse* current = nullptr;

		for (size_t at = spirvHeaderWords; at < words.size();) {
			uint32_t count = words[at] >> 16;
			uint32_t opcode = words[at] & 0xffff;
			if (count == 0 || at + count > words.size()) {
				return {};
			}
			if (opcode == spirvOpEntryPoint && count >= 4) {
				size_t nameEnd = 0;
				entryPoints[spirvLiteralString(words, at + 3, at + count, nameEnd)] = words[at + 2];
			} else if (opcode == spirvOpDecorate && count == 4 && words[at + 2] == spirvDecorationBinding) {
				bindings[words[at + 1]] = words[at + 3];
			} else if (opcode == spirvOpVariable && count >= 4) {
				variables.insert(words[at + 2]);
			} else if (opcode == spirvOpFunction && count >= 3) {
				currentID = words[at + 2];
				current = &functions[currentID];
				parameterCount = 0;
			} else if (opcode == spirvOpFunctionParameter && count >= 3 && current) {
				parameters[words[at + 2]] = { currentID, parameterCount++ };
			} else if (opcode == spirvOpFunctionEnd) {
				current = nullptr;
			} else if (opcode == spirvOpFunctionCall && count >= 4 && current) {
				current->calls.push_back(Call { words[at + 3], std::vector<uint32_t>(words.begin() + at + 4, words.begin() + at + count) });
			} else if ((opcode == spirvOpLoad || opcode == spirvOpAccessChain || opcode == spirvOpInBoundsAccessChain
				|| opcode == spirvOpCopyObject) && count >= 4) {
				sources[words[at + 2]] = words[at + 3];
			} else if (opcode == spirvOpSampledImage && count == 5) {
				sampledSamplers[words[at + 2]] = words[at + 4];
			} else if (spirvIsDrefOp(opcode) && count >= 5 && current) {
				current->comparedImages.push_back(words[at + 3]);
			}
			at += count;
		}

		for (const auto& [name, entry]: entryPoints) {
			// the functions this entry point runs
			std::set<uint32_t> reachable;
			std::vector<uint32_t> pending { entry };
			while (!pending.empty()) {
				uint32_t function = pending.back();
				pending.pop_back();
				auto use = functions.find(function);
				if (!reachable.insert(function).second || use == functions.end()) {
					continue;
				}
				for (const auto& call: use->second.calls) {
					pending.push_back(call.callee);
				}
			}

			// adds the Bindings of the variables `id` can come from in this entry point; false if one path leads elsewhere
			std::set<uint32_t> samplerBindings;
			auto trace = [&](uint32_t id) {
				std::set<uint32_t> seen;
				std::vector<uint32_t> ids { id };
				while (!ids.empty()) {
					uint32_t at = ids.back();
					ids.pop_back();
					if (!seen.insert(at).second) {
						continue;
					}
					if (variables.count(at) != 0) {
						auto binding = bindings.find(at);
						if (binding == bindings.end()) {
							return false;
						}
						samplerBindings.insert(binding->second);
					} else if (auto source = sources.find(at); source != sources.end()) {
						ids.push_back(source->second);
					} else if (auto parameter = parameters.find(at); parameter != parameters.end()) {
						auto [callee, position] = parameter->second;
						for (uint32_t caller: reachable) {
							auto use = functions.find(caller);
							if (use == functions.end()) {
								continue;
							}
							for (const auto& call: use->second.calls) {
								if (call.callee != callee) {
									continue;
								}
								if (position >= call.arguments.size()) {
									return false;
								}
								ids.push_back(call.arguments[position]);
							}
						}
					} else {
						return false;
					}
				}
				return true;
			};

			bool traced = true;
			for (uint32_t function: reachable) {
				auto use = functions.find(function);
				if (use == functions.end()) {
					continue;
				}
				for (uint32_t image: use->second.comparedImages) {
					// the sampled image, through any copies of it, back to the OpSampledImage that made it
					uint32_t sampled = image;
					for (int step = 0; step < 16 && sampledSamplers.count(sampled) == 0 && sources.count(sampled) != 0; ++step) {
						sampled = sources[sampled];
					}
					auto sampler = sampledSamplers.find(sampled);
					if (sampler == sampledSamplers.end() || !trace(sampler->second)) {
						traced = false;
					}
				}
			}
			result[name] = traced ? std::optional<std::set<uint32_t>>(std::move(samplerBindings)) : std::nullopt;
		}
		return result;
	};

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
