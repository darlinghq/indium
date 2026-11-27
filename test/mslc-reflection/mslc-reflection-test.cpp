// Feeds reflection documents that mslc wrote (fixtures/, generated from the probes of the same
// name in mslc's tests/probes) to Iridium::parseMslcReflection. Needs neither LLVM nor Vulkan.
#include <iridium/mslc-reflection.hpp>
#include <indium/device.private.hpp>
#include <indium/library.private.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::string slurp(const std::string& name) {
	std::ifstream in(std::string(FIXTURE_DIR) + "/" + name);
	if (!in) {
		std::fprintf(stderr, "cannot read fixture %s\n", name.c_str());
		std::exit(2);
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static bool parse(const std::string& text, Iridium::OutputInfo& info, std::string& error) {
	return Iridium::parseMslcReflection(text.data(), text.size(), info, error);
}

static const Iridium::FunctionInfo& load(const std::string& fixture, const std::string& entry, Iridium::OutputInfo& info) {
	std::string error;
	if (!parse(slurp(fixture), info, error)) {
		std::fprintf(stderr, "%s rejected: %s\n", fixture.c_str(), error.c_str());
		std::exit(1);
	}
	return info.functionInfos.at(entry);
}

int main() {
	using Iridium::BindingType;

	{
		Iridium::OutputInfo info;
		auto& fn = load("framebuffer_fetch_parameter.json", "framebuffer_fetch_parameter", info);
		CHECK(fn.type == Iridium::FunctionType::Fragment);
		CHECK(fn.bindings.size() == 1);
		CHECK(fn.bindings[0].type == BindingType::InputAttachment);
		CHECK(fn.bindings[0].index == 0);
		CHECK(fn.bindings[0].internalIndex == 0);
	}

	{
		// colour attachment 7 is read through descriptor binding 2, not binding 7
		Iridium::OutputInfo info;
		auto& fn = load("framebuffer_fetch_scalar_kinds.json", "framebuffer_fetch_scalar_kinds", info);
		CHECK(fn.bindings.size() == 3);
		CHECK(fn.bindings[0].index == 2 && fn.bindings[0].internalIndex == 0);
		CHECK(fn.bindings[1].index == 3 && fn.bindings[1].internalIndex == 1);
		CHECK(fn.bindings[2].type == BindingType::InputAttachment && fn.bindings[2].index == 7 && fn.bindings[2].internalIndex == 2);
	}

	{
		Iridium::OutputInfo info;
		auto& fn = load("framebuffer_fetch_after_texture_binding.json", "framebuffer_fetch_after_texture_binding", info);
		CHECK(fn.bindings.size() == 3);
		CHECK(fn.bindings[0].type == BindingType::Texture && fn.bindings[0].index == 0 && fn.bindings[0].internalIndex == 0);
		CHECK(fn.bindings[0].textureAccessType == Iridium::TextureAccessType::Sample);
		CHECK(fn.bindings[1].type == BindingType::Sampler && fn.bindings[1].index == 0 && fn.bindings[1].internalIndex == 1);
		CHECK(fn.bindings[1].embeddedSamplerIndex == SIZE_MAX);
		CHECK(fn.bindings[2].type == BindingType::InputAttachment && fn.bindings[2].internalIndex == 2);
	}

	{
		Iridium::OutputInfo info;
		auto& fn = load("framebuffer_fetch_after_constexpr_sampler.json", "framebuffer_fetch_after_constexpr_sampler", info);
		CHECK(fn.embeddedSamplers.size() == 1);
		CHECK(fn.embeddedSamplers[0].magnificationFilter == Iridium::EmbeddedSampler::Filter::Linear);
		CHECK(fn.embeddedSamplers[0].mipmapFilter == Iridium::EmbeddedSampler::MipFilter::None);
		CHECK(fn.embeddedSamplers[0].usesNormalizedCoordinates);
		CHECK(fn.embeddedSamplers[0].anisotropyLevel == 1);
		CHECK(fn.embeddedSamplers[0].lodMax == 65504.0f);
		CHECK(fn.bindings[1].type == BindingType::Sampler && fn.bindings[1].index == SIZE_MAX);
		CHECK(fn.bindings[1].internalIndex == 1 && fn.bindings[1].embeddedSamplerIndex == 0);
		CHECK(fn.bindings[2].type == BindingType::InputAttachment && fn.bindings[2].internalIndex == 2);
	}

	{
		Iridium::OutputInfo info;
		auto& fn = load("buffer_implicit_vertex_after_stage_in.json", "v", info);
		CHECK(fn.type == Iridium::FunctionType::Vertex);
		CHECK(fn.bindings.size() == 3);
		CHECK(fn.bindings[0].type == BindingType::VertexInput && fn.bindings[0].index == 0);
		CHECK(fn.bindings[1].type == BindingType::Buffer && fn.bindings[1].index == 0);
		CHECK(fn.bindings[2].type == BindingType::Buffer && fn.bindings[2].index == 1);
	}

	// Rejections: each edits a real document so a reader that ignored the field would accept it.
	auto rejects = [](const std::string& fixture, const std::string& from, const std::string& to, const char* why) {
		std::string text = slurp(fixture);
		size_t at = text.find(from);
		if (at == std::string::npos) {
			std::fprintf(stderr, "fixture %s lacks \"%s\"\n", fixture.c_str(), from.c_str());
			std::exit(2);
		}
		text.replace(at, from.size(), to);
		Iridium::OutputInfo info;
		std::string error;
		bool ok = parse(text, info, error);
		if (ok || error.empty()) {
			std::fprintf(stderr, "expected rejection (%s) was accepted\n", why);
			++failures;
		}
	};
	const std::string fb = "framebuffer_fetch_parameter.json";
	rejects(fb, "\"input_attachment_index\": 0", "\"input_attachment_index\": 1", "index mismatch");
	rejects(fb, "\"set\": 1", "\"set\": 0", "wrong set for a fragment stage");
	rejects(fb, "\"stage\": \"fragment\"", "\"stage\": \"vertex\"", "input attachment in a vertex function");
	rejects(fb, "\"reflection_version\": 2", "\"reflection_version\": 3", "version");
	rejects(fb, "\"binding\": 0 }", "\"binding\": -1 }", "negative binding");
	rejects(fb, "}\n\t]\n}", "}\n\t]\n} x", "trailing data");

	{
		auto bad = [&](const std::string& doc, const char* why) {
			Iridium::OutputInfo info;
			std::string error;
			if (parse(doc, info, error)) {
				std::fprintf(stderr, "expected rejection (%s) was accepted\n", why);
				++failures;
			}
		};
		const std::string head = "{\"reflection_version\":2,\"entry_points\":[{\"name\":\"f\",\"stage\":\"fragment\",\"embedded_samplers\":[],\"bindings\":[{\"kind\":\"Buffer\",\"metal_index\":";
		const std::string tail = "}]}]}";
		Iridium::OutputInfo ok;
		std::string error;
		CHECK(parse(head + "1" + tail, ok, error));
		CHECK(parse(head + "1e0" + tail, ok, error));
		bad(head + "0x10" + tail, "hex number");
		bad(head + "+1" + tail, "plus sign");
		bad(head + ".5" + tail, "leading dot");
		bad(head + "01" + tail, "leading zero");
		bad(head + "1." + tail, "trailing dot");
		bad("{\"reflection_version\":2,\"reflection_version\":2,\"entry_points\":[]}", "duplicate key");
	}

	{
		// an unknown kind is skipped, a known unsupported kind is not
		std::string doc = "{\"reflection_version\":2,\"entry_points\":[{\"name\":\"f\",\"stage\":\"fragment\",\"bindings\":[{\"kind\":\"Future\"}],\"embedded_samplers\":[]}]}";
		Iridium::OutputInfo info;
		std::string error;
		CHECK(parse(doc, info, error));
		CHECK(info.functionInfos.at("f").bindings.empty());
		doc.replace(doc.find("Future"), 6, "TextureBuffer");
		CHECK(!parse(doc, info, error));
	}

	{
		// Float-controls requirement checks (#371): 32-bit device requirement and 16-bit module requirement + refusal.
		CHECK(Indium::supportsRequiredDeviceFeatures(VK_API_VERSION_1_3, true, true));
		CHECK(!Indium::supportsRequiredDeviceFeatures(VK_API_VERSION_1_2, true, true));
		CHECK(!Indium::supportsRequiredDeviceFeatures(VK_API_VERSION_1_3, false, true));
		CHECK(!Indium::supportsRequiredDeviceFeatures(VK_API_VERSION_1_3, true, false));

		// 5-word SPIR-V header followed by OpCapability Shader (1) and OpCapability Float16 (9).
		const std::vector<uint32_t> spvNoHalf = {
			0x07230203u, 0x00010500u, 0u, 10u, 0u,
			(2u << 16) | 17u, 1u,
		};
		const std::vector<uint32_t> spvWithHalf = {
			0x07230203u, 0x00010500u, 0u, 10u, 0u,
			(2u << 16) | 17u, 1u,
			(2u << 16) | 17u, 9u,
		};
		const std::vector<uint32_t> spvZeroWordCount = {
			0x07230203u, 0x00010500u, 0u, 10u, 0u,
			0u,
			(2u << 16) | 17u, 9u,
		};

		CHECK(!Indium::spirvDeclaresFloat16(nullptr, 64));
		CHECK(!Indium::spirvDeclaresFloat16(reinterpret_cast<const char*>(spvNoHalf.data()), spvNoHalf.size() * sizeof(uint32_t)));
		CHECK(Indium::spirvDeclaresFloat16(reinterpret_cast<const char*>(spvWithHalf.data()), spvWithHalf.size() * sizeof(uint32_t)));
		CHECK(!Indium::spirvDeclaresFloat16(reinterpret_cast<const char*>(spvZeroWordCount.data()), spvZeroWordCount.size() * sizeof(uint32_t)));

		// Malformed 1-word OpCapability followed by word 9 must not read across instruction boundaries.
		const std::vector<uint32_t> spvOneWordCap = {
			0x07230203u, 0x00010500u, 0u, 10u, 0u,
			(1u << 16) | 17u,
			9u,
		};
		CHECK(!Indium::spirvDeclaresFloat16(reinterpret_cast<const char*>(spvOneWordCap.data()), spvOneWordCap.size() * sizeof(uint32_t)));

		// Unaligned buffer read via memcpy must work without UB.
		std::vector<char> unaligned(1 + spvWithHalf.size() * sizeof(uint32_t), 0);
		std::memcpy(unaligned.data() + 1, spvWithHalf.data(), spvWithHalf.size() * sizeof(uint32_t));
		CHECK(Indium::spirvDeclaresFloat16(unaligned.data() + 1, spvWithHalf.size() * sizeof(uint32_t)));

		// A module without Float16 succeeds even when the device lacks 16-bit signed-zero float control.
		Indium::requireSignedZeroFloat16ForModule(reinterpret_cast<const char*>(spvNoHalf.data()), spvNoHalf.size() * sizeof(uint32_t), false);
		// A module with Float16 succeeds when the device supports 16-bit signed-zero float control.
		Indium::requireSignedZeroFloat16ForModule(reinterpret_cast<const char*>(spvWithHalf.data()), spvWithHalf.size() * sizeof(uint32_t), true);
		// A module with Float16 is refused when the device lacks 16-bit signed-zero float control.
		bool threw = false;
		try {
			Indium::requireSignedZeroFloat16ForModule(reinterpret_cast<const char*>(spvWithHalf.data()), spvWithHalf.size() * sizeof(uint32_t), false);
		} catch (const std::runtime_error& e) {
			threw = std::string(e.what()).find("16-bit signed-zero float control") != std::string::npos;
		}
		CHECK(threw);
	}

	if (failures) {
		std::fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	std::printf("mslc-reflection-test: ok\n");
	return 0;
}
