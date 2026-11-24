// Feeds reflection documents that mslc wrote (fixtures/, generated from the probes of the same
// name in mslc's tests/probes) to Iridium::parseMslcReflection. Needs neither LLVM nor Vulkan.
#include <iridium/mslc-reflection.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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
		// an unknown kind is skipped, a known unsupported kind is not
		std::string doc = "{\"reflection_version\":2,\"entry_points\":[{\"name\":\"f\",\"stage\":\"fragment\",\"bindings\":[{\"kind\":\"Future\"}],\"embedded_samplers\":[]}]}";
		Iridium::OutputInfo info;
		std::string error;
		CHECK(parse(doc, info, error));
		CHECK(info.functionInfos.at("f").bindings.empty());
		doc.replace(doc.find("Future"), 6, "TextureBuffer");
		CHECK(!parse(doc, info, error));
	}

	if (failures) {
		std::fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	std::printf("mslc-reflection-test: ok\n");
	return 0;
}
