#include <indium/library.private.hpp>
#include <indium/device.private.hpp>
#include <indium/sampler.hpp>
#include <indium/dynamic-vk.hpp>
#include <indium/texture.hpp>

#include <algorithm>
#include <set>
#include <map>
#include <cstring>

namespace {
	using ImageShapes = std::unordered_map<std::string, std::unordered_map<uint32_t, Indium::ImageShape>>;
	ImageShapes spirvImageShapes(const std::vector<uint32_t>& words);
};

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

Indium::PrivateLibrary::PrivateLibrary(std::shared_ptr<PrivateDevice> device, const char* data, size_t dataLength, std::unordered_map<std::string, FunctionInfo> functionInfos):
	_privateDevice(device),
	_functionInfos(functionInfos)
{
	// the varyings of a fragment function may have to be renumbered for a vertex function (fragmentModuleLinkedTo)
	for (const auto& [name, funcInfo]: _functionInfos) {
		if (funcInfo.functionType == FunctionType::Fragment) {
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

	// the image type of each texture binding, which tells which of several arguments at one texture index a texture can be
	bool hasTextures = false;
	for (const auto& [name, funcInfo]: _functionInfos) {
		for (const auto& binding: funcInfo.bindings) {
			hasTextures = hasTextures || binding.type == BindingType::Texture;
		}
	}
	if (hasTextures) {
		const auto* words = reinterpret_cast<const uint32_t*>(data);
		auto shapes = spirvImageShapes(std::vector<uint32_t>(words, words + dataLength / sizeof(uint32_t)));
		for (auto& [name, funcInfo]: _functionInfos) {
			auto entry = shapes.find(name);
			if (entry == shapes.end()) {
				continue;
			}
			for (const auto& binding: funcInfo.bindings) {
				auto shape = binding.type == BindingType::Texture ? entry->second.find(static_cast<uint32_t>(binding.internalIndex)) : entry->second.end();
				if (shape != entry->second.end()) {
					funcInfo.imageShapes[binding.internalIndex] = shape->second;
				}
			}
		}
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

namespace {
	// The image type of each variable with a Binding that an entry point lists in its interface, by the entry point's name
	// and the binding number. From SPIR-V 1.4 an entry point lists every global variable it uses; an older module lists
	// only its inputs and outputs, so nothing is found for it. A binding two entry points of one name declare with
	// different image types is left out.
	ImageShapes spirvImageShapes(const std::vector<uint32_t>& words) {
		constexpr uint32_t opTypeImage = 25, opTypeSampledImage = 27, opTypePointer = 32, decorationBinding = 33;
		std::vector<std::pair<std::string, std::vector<uint32_t>>> entryPoints;
		std::unordered_map<uint32_t, uint32_t> bindingOf, pointeeOf, typeOf, imageOfSampledImage;
		std::unordered_map<uint32_t, Indium::ImageShape> images;
		for (size_t at = spirvHeaderWords; at < words.size();) {
			uint32_t count = words[at] >> 16;
			uint32_t opcode = words[at] & 0xffff;
			if (count == 0 || at + count > words.size()) {
				return {};
			}
			switch (opcode) {
				case spirvOpEntryPoint:
					if (count >= 4) {
						size_t nameEnd = 0;
						auto name = spirvLiteralString(words, at + 3, at + count, nameEnd);
						entryPoints.emplace_back(name, std::vector<uint32_t>(words.begin() + nameEnd, words.begin() + at + count));
					}
					break;
				case spirvOpDecorate: if (count == 4 && words[at + 2] == decorationBinding) { bindingOf[words[at + 1]] = words[at + 3]; } break;
				case opTypeImage: if (count >= 9) { images[words[at + 1]] = { words[at + 3], words[at + 5] == 1, words[at + 6] == 1 }; } break;
				case opTypeSampledImage: if (count == 3) { imageOfSampledImage[words[at + 1]] = words[at + 2]; } break;
				case opTypePointer: if (count == 4) { pointeeOf[words[at + 1]] = words[at + 3]; } break;
				case spirvOpVariable: if (count >= 4) { typeOf[words[at + 2]] = words[at + 1]; } break;
			}
			at += count;
		}

		ImageShapes result;
		std::set<std::pair<std::string, uint32_t>> conflicting;
		for (const auto& [name, interface]: entryPoints) {
			auto& shapes = result[name];
			for (uint32_t variable: interface) {
				auto binding = bindingOf.find(variable);
				auto type = typeOf.find(variable);
				if (binding == bindingOf.end() || type == typeOf.end()) {
					continue;
				}
				auto pointee = pointeeOf.find(type->second);
				if (pointee == pointeeOf.end()) {
					continue;
				}
				uint32_t imageType = pointee->second;
				if (auto sampled = imageOfSampledImage.find(imageType); sampled != imageOfSampledImage.end()) {
					imageType = sampled->second;
				}
				auto image = images.find(imageType);
				if (image == images.end()) {
					continue;
				}
				auto [known, inserted] = shapes.emplace(binding->second, image->second);
				const auto& shape = known->second;
				if (!inserted && (shape.dim != image->second.dim || shape.arrayed != image->second.arrayed || shape.multisampled != image->second.multisampled)) {
					conflicting.emplace(name, binding->second);
				}
			}
		}
		for (const auto& [name, binding]: conflicting) {
			result[name].erase(binding);
		}
		return result;
	}
};

bool Indium::textureFitsBinding(const FunctionInfo& info, const BindingDescriptor& binding, const Texture& texture) {
	auto declared = info.imageShapes.find(binding.internalIndex);
	if (binding.texelBuffer || declared == info.imageShapes.end()) {
		return true;
	}

	// the image type each texture type is bound as (SPIR-V Dim: 0 is 1D, 1 is 2D, 2 is 3D, 3 is Cube)
	constexpr uint32_t dim1D = 0, dim2D = 1, dim3D = 2, dimCube = 3;
	// a binding that fetches from a cube reads the 2D array of its faces (PrivateTexture::fetchImageView)
	bool fetch = binding.textureAccessType == TextureAccessType::Fetch;
	ImageShape expected;
	switch (texture.textureType()) {
		case TextureType::e1D:                 expected = { dim1D, false, false }; break;
		case TextureType::e1DArray:            expected = { dim1D, true, false }; break;
		case TextureType::e2D:                 expected = { dim2D, false, false }; break;
		case TextureType::e2DArray:            expected = { dim2D, true, false }; break;
		case TextureType::e2DMultisample:      expected = { dim2D, false, true }; break;
		case TextureType::e2DMultisampleArray: expected = { dim2D, true, true }; break;
		case TextureType::eCube:               expected = fetch ? ImageShape { dim2D, true, false } : ImageShape { dimCube, false, false }; break;
		case TextureType::eCubeArray:          expected = fetch ? ImageShape { dim2D, true, false } : ImageShape { dimCube, true, false }; break;
		case TextureType::e3D:                 expected = { dim3D, false, false }; break;
		case TextureType::eTextureBuffer:      return true;
	}
	const auto& shape = declared->second;
	return shape.dim == expected.dim && shape.arrayed == expected.arrayed && shape.multisampled == expected.multisampled;
};

namespace {
	uint32_t floatBits(float value) {
		uint32_t bits;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	/**
	 * Makes the fragment function `entry` clamp the color outputs of `clamps` before it returns: a new entry point calls the
	 * function, then loads each of those outputs (four 32-bit floats), clamps it with FClamp and stores it back. Nothing is
	 * done to an output that is not a vec4 of 32-bit floats. Returns false when the module cannot be parsed.
	 */
	bool clampFragmentOutputs(std::vector<uint32_t>& words, const std::string& entry, const std::vector<Indium::OutputClamp>& clamps) {
		constexpr uint32_t opExtInstImport = 11, opMemoryModel = 14, opExecutionMode = 16, opTypeVoid = 19, opTypeFloat = 22, opTypeVector = 23,
			opTypeFunction = 33, opTypePointer = 32, opConstant = 43, opConstantComposite = 44, opFunction = 54, opFunctionEnd = 56, opFunctionCall = 57,
			opLoad = 61, opStore = 62, opExtInst = 12, opLabel = 248, opReturn = 253, opExecutionModeId = 331;
		constexpr uint32_t storageClassOutput = 3, glslFClamp = 43;

		uint32_t entryFunction = 0;
		std::set<uint32_t> interfaceIDs;
		std::map<uint32_t, uint32_t> locationOf, pointerTarget, variablePointer;
		std::map<uint32_t, std::pair<uint32_t, uint32_t>> vectors;
		std::set<uint32_t> float32;
		uint32_t glsl = 0, voidType = 0, voidFunction = 0;
		size_t memoryModelAt = 0, firstFunctionAt = 0;

		for (size_t at = spirvHeaderWords; at < words.size();) {
			uint32_t count = words[at] >> 16;
			uint32_t opcode = words[at] & 0xffff;
			if (count == 0 || at + count > words.size()) {
				return false;
			}
			switch (opcode) {
				case spirvOpEntryPoint: {
					size_t nameEnd = 0;
					if (count >= 4 && spirvLiteralString(words, at + 3, at + count, nameEnd) == entry) {
						entryFunction = words[at + 2];
						for (size_t i = nameEnd; i < at + count; ++i) {
							interfaceIDs.insert(words[i]);
						}
					}
					break;
				}
				case opExtInstImport: {
					size_t end = 0;
					if (spirvLiteralString(words, at + 2, at + count, end) == "GLSL.std.450") {
						glsl = words[at + 1];
					}
					break;
				}
				case opMemoryModel: memoryModelAt = at; break;
				case opTypeVoid: voidType = words[at + 1]; break;
				case opTypeFloat: if (count >= 3 && words[at + 2] == 32) { float32.insert(words[at + 1]); } break;
				case opTypeVector: if (count == 4) { vectors[words[at + 1]] = { words[at + 2], words[at + 3] }; } break;
				case opTypePointer: if (count == 4 && words[at + 2] == storageClassOutput) { pointerTarget[words[at + 1]] = words[at + 3]; } break;
				case opTypeFunction: if (count == 3 && voidType != 0 && words[at + 2] == voidType) { voidFunction = words[at + 1]; } break;
				case spirvOpVariable: if (count >= 4 && words[at + 3] == storageClassOutput) { variablePointer[words[at + 2]] = words[at + 1]; } break;
				case spirvOpDecorate: if (count == 4 && words[at + 2] == spirvDecorationLocation) { locationOf[words[at + 1]] = words[at + 3]; } break;
				case opFunction: if (firstFunctionAt == 0) { firstFunctionAt = at; } break;
				default: break;
			}
			at += count;
		}
		if (entryFunction == 0 || memoryModelAt == 0 || firstFunctionAt == 0) {
			return false;
		}

		struct Target { uint32_t variable, type; const Indium::OutputClamp* clamp; };
		std::vector<Target> targets;
		for (const auto& [variable, pointer]: variablePointer) {
			auto location = locationOf.find(variable);
			auto target = pointerTarget.find(pointer);
			if (location == locationOf.end() || target == pointerTarget.end() || interfaceIDs.count(variable) == 0) {
				continue;
			}
			auto vector = vectors.find(target->second);
			if (vector == vectors.end() || vector->second.second != 4 || float32.count(vector->second.first) == 0) {
				continue;
			}
			for (const auto& clamp: clamps) {
				if (clamp.location == location->second) {
					targets.push_back({ variable, target->second, &clamp });
				}
			}
		}
		if (targets.empty()) {
			return true;
		}

		uint32_t bound = words[3];
		std::vector<uint32_t> importWords, globals, function;
		auto emit = [](std::vector<uint32_t>& into, uint32_t opcode, std::initializer_list<uint32_t> operands) {
			into.push_back((static_cast<uint32_t>(operands.size()) + 1) << 16 | opcode);
			into.insert(into.end(), operands.begin(), operands.end());
		};
		if (glsl == 0) {
			glsl = bound++;
			const char name[] = "GLSL.std.450";
			importWords.push_back((2 + 4) << 16 | opExtInstImport);
			importWords.push_back(glsl);
			uint32_t packed[4] = {};
			std::memcpy(packed, name, sizeof(name));
			importWords.insert(importWords.end(), packed, packed + 4);
		}
		if (voidType == 0) {
			voidType = bound++;
			emit(globals, opTypeVoid, { voidType });
		}
		if (voidFunction == 0) {
			voidFunction = bound++;
			emit(globals, opTypeFunction, { voidFunction, voidType });
		}

		const uint32_t wrapper = bound++, label = bound++, call = bound++;
		emit(function, opFunction, { voidType, wrapper, 0, voidFunction });
		emit(function, opLabel, { label });
		emit(function, opFunctionCall, { voidType, call, entryFunction });
		for (const auto& target: targets) {
			uint32_t component = vectors[target.type].first;
			uint32_t bounds[2][4];
			for (int side = 0; side < 2; ++side) {
				const auto& values = side == 0 ? target.clamp->low : target.clamp->high;
				for (int i = 0; i < 4; ++i) {
					bounds[side][i] = bound++;
					emit(globals, opConstant, { component, bounds[side][i], floatBits(values[i]) });
				}
			}
			uint32_t low = bound++, high = bound++;
			emit(globals, opConstantComposite, { target.type, low, bounds[0][0], bounds[0][1], bounds[0][2], bounds[0][3] });
			emit(globals, opConstantComposite, { target.type, high, bounds[1][0], bounds[1][1], bounds[1][2], bounds[1][3] });
			uint32_t loaded = bound++, clamped = bound++;
			emit(function, opLoad, { target.type, loaded, target.variable });
			emit(function, opExtInst, { target.type, clamped, glsl, glslFClamp, loaded, low, high });
			emit(function, opStore, { target.variable, clamped });
		}
		emit(function, opReturn, {});
		emit(function, opFunctionEnd, {});

		// the new entry point takes the place of the function in the entry point and its execution modes
		for (size_t at = spirvHeaderWords; at < words.size();) {
			uint32_t count = words[at] >> 16;
			uint32_t opcode = words[at] & 0xffff;
			if (opcode == spirvOpEntryPoint && count >= 4 && words[at + 2] == entryFunction) {
				words[at + 2] = wrapper;
			} else if ((opcode == opExecutionMode || opcode == opExecutionModeId) && count >= 3 && words[at + 1] == entryFunction) {
				words[at + 1] = wrapper;
			}
			at += count;
		}
		words[3] = bound;

		std::vector<uint32_t> result(words.begin(), words.begin() + memoryModelAt);
		result.insert(result.end(), importWords.begin(), importWords.end());
		result.insert(result.end(), words.begin() + memoryModelAt, words.begin() + firstFunctionAt);
		result.insert(result.end(), globals.begin(), globals.end());
		result.insert(result.end(), words.begin() + firstFunctionAt, words.end());
		result.insert(result.end(), function.begin(), function.end());
		words = std::move(result);
		return true;
	}
}

VkShaderModule Indium::PrivateLibrary::fragmentModuleLinkedTo(const std::string& entry, const std::vector<StageInterfaceField>& vertexOutputs, const std::vector<OutputClamp>& clamps) {
	const auto info = _functionInfos.find(entry);
	if (info == _functionInfos.end() || (info->second.stageInterface.empty() && clamps.empty()) || _words.empty()) {
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
	if (unchanged && clamps.empty()) {
		return _shaderModule;
	}
	for (const auto& clamp: clamps) {
		// the clamps are part of what makes a module different
		wanted.push_back(UINT32_MAX);
		wanted.push_back(clamp.location);
		for (auto value: clamp.low) { wanted.push_back(floatBits(value)); }
		for (auto value: clamp.high) { wanted.push_back(floatBits(value)); }
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

	for (size_t at = spirvHeaderWords; at < words.size() && !unchanged;) {
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

	if (!clamps.empty() && !clampFragmentOutputs(words, entry, clamps)) {
		return VK_NULL_HANDLE;
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
