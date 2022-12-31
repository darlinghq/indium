#include <iridium/iridium.hpp>

#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " <input> [output]" << std::endl;
		return 1;
	}

	std::filesystem::path inPath = argv[1];
	std::filesystem::path outPathSPV;

	if (argc > 2) {
		outPathSPV = argv[2];
	} else {
		outPathSPV = inPath;
		outPathSPV.replace_extension("spv");
	}

	std::filesystem::path outPathJSON = outPathSPV;
	outPathJSON += ".json";

	std::ifstream inFile(inPath, std::ios::binary | std::ios::ate);
	std::ofstream outFileSPV(outPathSPV, std::ios::binary);
	std::ofstream outFileJSON(outPathJSON);

	auto inFileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	// check the first 4 (magic) bytes first

	char magic[4];
	if (!inFile.read(magic, 4)) {
		std::cerr << "Failed to read input file" << std::endl;
		return 1;
	}

	if (magic[0] != 'M' || magic[1] != 'T' || magic[2] != 'L' || magic[3] != 'B') {
		std::cerr << "Invalid input file (not a Metal library)" << std::endl;
		return 1;
	}

	// okay, now read the entire file

	inFile.seekg(0, std::ios::beg);
	auto buffer = new char[inFileSize];
	if (!inFile.read(buffer, inFileSize)) {
		std::cerr << "Failed to read input file" << std::endl;
		return 1;
	}

	if (!Iridium::init()) {
		std::cerr << "Failed to initialize Iridium" << std::endl;
		return 1;
	}

	size_t outputSize = 0;
	Iridium::OutputInfo outputInfo;
	auto result = Iridium::translate(buffer, inFileSize, outputSize, outputInfo);
	bool ok = outputSize == 0 || !!result;

	if (ok) {
		outFileSPV.write(static_cast<const char*>(result), outputSize);
		free(result);

		outFileJSON << "{" << std::endl;
		outFileJSON << "\t\"function-infos\": {" << std::endl;
		for (auto it = outputInfo.functionInfos.begin(); it != outputInfo.functionInfos.end(); ++it) {
			char funcTypeCode = 'u';

			if (it->second.type == Iridium::FunctionType::Fragment) {
				funcTypeCode = 'f';
			} else if (it->second.type == Iridium::FunctionType::Vertex) {
				funcTypeCode = 'v';
			} else if (it->second.type == Iridium::FunctionType::Kernel) {
				funcTypeCode = 'k';
			}

			outFileJSON << "\t\t\"" << funcTypeCode << '@' << it->first << "\": {" << std::endl;

			outFileJSON << "\t\t\t\"bindings\": [" << std::endl;
			for (size_t i = 0; i < it->second.bindings.size(); ++i) {
				const auto& binding = it->second.bindings[i];
				outFileJSON << "\t\t\t\t{" << std::endl;

				outFileJSON << "\t\t\t\t\t\"type\": \"";
				if (binding.type == Iridium::BindingType::Buffer) {
					outFileJSON << "buffer";
				} else if (binding.type == Iridium::BindingType::Texture) {
					outFileJSON << "texture";
				} else if (binding.type == Iridium::BindingType::Sampler) {
					outFileJSON << "sampler";
				} else if (binding.type == Iridium::BindingType::VertexInput) {
					outFileJSON << "vertex-input";
				} else {
					outFileJSON << "undefined";
				}
				outFileJSON << "\"," << std::endl;

				outFileJSON << "\t\t\t\t\t\"index\": " << (binding.index == SIZE_MAX ? "-1" : std::to_string(binding.index));

				if (binding.type != Iridium::BindingType::VertexInput) {
					outFileJSON << "," << std::endl;
					outFileJSON << "\t\t\t\t\t\"internal-index\": " << std::to_string(binding.internalIndex) << (binding.type == Iridium::BindingType::Buffer ? "" : ",") << std::endl;
				} else {
					outFileJSON << std::endl;
				}

				if (binding.type == Iridium::BindingType::Texture) {
					outFileJSON << "\t\t\t\t\t\"texture-access-type\": \"";
					if (binding.textureAccessType == Iridium::TextureAccessType::Sample) {
						outFileJSON << "sample";
					} else if (binding.textureAccessType == Iridium::TextureAccessType::Read) {
						outFileJSON << "read";
					} else if (binding.textureAccessType == Iridium::TextureAccessType::Write) {
						outFileJSON << "write";
					} else if (binding.textureAccessType == Iridium::TextureAccessType::ReadWrite) {
						outFileJSON << "read-write";
					} else {
						outFileJSON << "undefined";
					}
					outFileJSON << "\"" << std::endl;
				}

				if (binding.type == Iridium::BindingType::Sampler) {
					outFileJSON << "\t\t\t\t\t\"embedded-sampler-index\": " << std::to_string(binding.embeddedSamplerIndex) << std::endl;
				}

				outFileJSON << "\t\t\t\t}" << ((i + 1 == it->second.bindings.size()) ? "" : ",") << std::endl;
			}
			outFileJSON << "\t\t\t]," << std::endl;

			outFileJSON << "\t\t\t\"embedded-samplers\": [" << std::endl;
			for (size_t i = 0; i < it->second.embeddedSamplers.size(); ++i) {
				const auto& sampler = it->second.embeddedSamplers[i];
				outFileJSON << "\t\t\t\t{" << std::endl;

				auto addressModeToString = [](Iridium::EmbeddedSampler::AddressMode addressMode) {
					switch (addressMode) {
						case Iridium::EmbeddedSampler::AddressMode::ClampToZero:
							return "clamp-to-zero";
						case Iridium::EmbeddedSampler::AddressMode::ClampToEdge:
							return "clamp-to-edge";
						case Iridium::EmbeddedSampler::AddressMode::Repeat:
							return "repeat";
						case Iridium::EmbeddedSampler::AddressMode::MirrorRepeat:
							return "mirror-repeat";
						case Iridium::EmbeddedSampler::AddressMode::ClampToBorderColor:
							return "clamp-to-border-color";
						default:
							return "undefined";
					}
				};

				auto filterToString = [](Iridium::EmbeddedSampler::Filter filter) {
					switch (filter) {
						case Iridium::EmbeddedSampler::Filter::Nearest:
							return "nearest";
						case Iridium::EmbeddedSampler::Filter::Linear:
							return "linear";
						default:
							return "undefined";
					}
				};

				auto mipFilterToString = [](Iridium::EmbeddedSampler::MipFilter mipFilter) {
					switch (mipFilter) {
						case Iridium::EmbeddedSampler::MipFilter::None:
							return "none";
						case Iridium::EmbeddedSampler::MipFilter::Nearest:
							return "nearest";
						case Iridium::EmbeddedSampler::MipFilter::Linear:
							return "linear";
						default:
							return "undefined";
					}
				};

				auto compareFunctionToString = [](Iridium::EmbeddedSampler::CompareFunction compareFunction) {
					switch (compareFunction) {
						case Iridium::EmbeddedSampler::CompareFunction::None:
							return "none";
						case Iridium::EmbeddedSampler::CompareFunction::Less:
							return "less";
						case Iridium::EmbeddedSampler::CompareFunction::LessEqual:
							return "less-equal";
						case Iridium::EmbeddedSampler::CompareFunction::Greater:
							return "greater";
						case Iridium::EmbeddedSampler::CompareFunction::GreaterEqual:
							return "greater-equal";
						case Iridium::EmbeddedSampler::CompareFunction::Equal:
							return "equal";
						case Iridium::EmbeddedSampler::CompareFunction::NotEqual:
							return "not-equal";
						case Iridium::EmbeddedSampler::CompareFunction::Always:
							return "always";
						case Iridium::EmbeddedSampler::CompareFunction::Never:
							return "never";
						default:
							return "undefined";
					}
				};

				auto borderColorToString = [](Iridium::EmbeddedSampler::BorderColor borderColor) {
					switch (borderColor) {
						case Iridium::EmbeddedSampler::BorderColor::TransparentBlack:
							return "transparent-black";
						case Iridium::EmbeddedSampler::BorderColor::OpaqueBlack:
							return "opaque-black";
						case Iridium::EmbeddedSampler::BorderColor::OpaqueWhite:
							return "opaque-white";
						default:
							return "undefined";
					}
				};

				outFileJSON << "\t\t\t\t\t\"width-address-mode\": \"" << addressModeToString(sampler.widthAddressMode) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"height-address-mode\": \"" << addressModeToString(sampler.heightAddressMode) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"depth-address-mode\": \"" << addressModeToString(sampler.depthAddressMode) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"magnification-filter\": \"" << filterToString(sampler.magnificationFilter) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"minification-filter\": \"" << filterToString(sampler.minificationFilter) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"mipmap-filter\": \"" << mipFilterToString(sampler.mipmapFilter) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"uses-normalized-coordinates\": " << (sampler.usesNormalizedCoordinates ? "true" : "false") << "," << std::endl;
				outFileJSON << "\t\t\t\t\t\"compare-function\": \"" << compareFunctionToString(sampler.compareFunction) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"anisotropy-level\": " << std::to_string(sampler.anisotropyLevel) << "," << std::endl;
				outFileJSON << "\t\t\t\t\t\"border-color\": \"" << borderColorToString(sampler.borderColor) << "\"," << std::endl;
				outFileJSON << "\t\t\t\t\t\"lod-min\": " << std::to_string(sampler.lodMin) << "," << std::endl;
				outFileJSON << "\t\t\t\t\t\"lod-max\": " << std::to_string(sampler.lodMax) << std::endl;

				outFileJSON << "\t\t\t\t}" << ((i + 1 == it->second.embeddedSamplers.size()) ? "" : ",") << std::endl;
			}
			outFileJSON << "\t\t\t]" << std::endl;

			outFileJSON << "\t\t}" << ((std::next(it) == outputInfo.functionInfos.end()) ? "" : ",") << std::endl;
		}
		outFileJSON << "\t}" << std::endl;
		outFileJSON << "}" << std::endl;
	} else {
		std::cerr << "Failed to translate library" << std::endl;
	}

	Iridium::finit();

	delete[] buffer;

	return ok ? 0 : 1;
};
