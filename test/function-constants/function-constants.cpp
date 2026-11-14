// Copyright (C) 2026 Lubos Dolezel
//
// Function constants reach the SPIR-V as specialization constants. Translates function-constants.metallib
// (built from shadersrc/function-constants.metal with Apple's Metal compiler), and checks the reflection of
// the four constants and that the module declares a value and a "defined" specialization constant, with
// the SpecIds the reflection names, for each. Needs no GPU. Usage: function-constants <metallib>

#include "../../src/iridium/air.cpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace {

unsigned failures = 0;

void check(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

constexpr uint32_t opDecorate = 71, opSpecConstantTrue = 48, opSpecConstantFalse = 49, opSpecConstant = 50;
constexpr uint32_t decorationSpecId = 1;

}

int main(int argc, char** argv) {
	if (argc < 2 || !Iridium::DynamicLLVM::init()) {
		return 2;
	}

	std::ifstream file(argv[1], std::ios::binary);
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});

	Iridium::OutputInfo info;
	std::vector<uint32_t> words;
	try {
		Iridium::AIR::Library library(data.data(), data.size());
		Iridium::SPIRV::Builder builder;
		library.buildModule(builder, info);
		size_t size = 0;
		auto bytes = static_cast<uint32_t*>(builder.finalize(size));
		words.assign(bytes, bytes + size / 4);
		std::free(bytes);
	} catch (const std::exception& e) {
		std::printf("FAIL translation: %s\n", e.what());
		return 1;
	}
	check(true, "the library translates");

	std::set<uint32_t> specIds;
	unsigned specConstants = 0, specBooleans = 0;
	for (size_t offset = 5; offset < words.size();) {
		uint32_t count = words[offset] >> 16, opcode = words[offset] & 0xffff;
		if (count == 0 || offset + count > words.size()) {
			std::printf("FAIL malformed SPIR-V\n");
			return 1;
		}
		if (opcode == opDecorate && count == 4 && words[offset + 2] == decorationSpecId) {
			specIds.insert(words[offset + 3]);
		} else if (opcode == opSpecConstant) {
			++specConstants;
		} else if (opcode == opSpecConstantTrue || opcode == opSpecConstantFalse) {
			++specBooleans;
		}
		offset += count;
	}

	struct Expected {
		const char* name;
		Iridium::FunctionConstantInfo::Type type;
		size_t index;
	} expected[] = {
		{ "use_scale", Iridium::FunctionConstantInfo::Type::Bool, 0 },
		{ "mode", Iridium::FunctionConstantInfo::Type::Int, 1 },
		{ "factor", Iridium::FunctionConstantInfo::Type::Float, 2 },
		{ "opt", Iridium::FunctionConstantInfo::Type::UInt, 3 },
	};

	// a function's reflection lists the constants it uses, not every constant of the shader
	for (auto [function, count]: { std::pair<const char*, size_t> { "fc_kernel", 4 }, std::pair<const char*, size_t> { "fc_simple", 2 } }) {
		auto& constants = info.functionInfos[function].functionConstants;
		bool ok = constants.size() == count;
		for (size_t i = 0; ok && i < 4; ++i) {
			auto found = std::find_if(constants.begin(), constants.end(), [&](const Iridium::FunctionConstantInfo& c) { return c.index == expected[i].index; });
			if (found == constants.end()) {
				ok = count == 2 && (i == 0 || i == 3);
				continue;
			}
			ok = found->name == expected[i].name && found->type == expected[i].type
				&& found->specializationID == 3 + expected[i].index && found->definedSpecializationID == 3 + 65536 + expected[i].index
				&& specIds.count(found->specializationID) && specIds.count(found->definedSpecializationID);
		}
		check(ok, (std::string(function) + ": the constants it uses, with their names, types, indices and declared SpecIds").c_str());
	}

	check(specConstants >= 6 && specBooleans >= 5, "numeric constants are OpSpecConstant, the boolean and the defined flags are OpSpecConstantFalse");
	check(specIds.size() == 3 + 8, "the threadgroup size and the four constants with their defined flags have SpecIds");

	// the optional buffer is still a buffer argument of the function
	auto& arguments = info.functionInfos["fc_kernel"].arguments;
	check(arguments.size() == 2 && arguments[1].index == 1 && arguments[1].name == "extra" && arguments[1].type == Iridium::BindingType::Buffer,
		"an argument that exists only for a constant is reflected as the buffer it is");

	return failures ? 1 : 0;
}
