// Copyright (C) 2026 Darling Developers
//
// RenderPassKey decides which render passes share one cached VkRenderPass. Two descriptions may
// only share a key when every field the encoder sets is equal: load and store operations and
// layouts decide what happens to attachment contents, so a pass that differs in any of them must
// get its own VkRenderPass. Descriptions the key does not cover must not be cached at all.

#include <indium/render-pass-key.private.hpp>

#include <array>
#include <cstdio>
#include <unordered_map>

namespace {

unsigned failures = 0;

void report(bool ok, const char* what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	failures += !ok;
}

}

int main() {
	VkAttachmentDescription attachments[2] {};
	for (auto& attachment: attachments) {
		attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	}
	VkAttachmentReference colors[] { { 0, VK_IMAGE_LAYOUT_GENERAL }, { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_GENERAL } };
	VkAttachmentReference resolves[] { { 1, VK_IMAGE_LAYOUT_GENERAL }, { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_GENERAL } };
	VkSubpassDescription subpass {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 2;
	subpass.pColorAttachments = colors;
	VkSubpassDependency dependency {};
	dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkRenderPassCreateInfo info {};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 1;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dependency;

	auto original = Indium::renderPassKey(info);
	report(original.has_value(), "a single-subpass description has a key");
	if (!original) {
		return 1;
	}

	auto differs = [&](const char* what) {
		auto key = Indium::renderPassKey(info);
		report(key && !(*key == *original), what);
	};
	auto change = [&](auto& field, auto value, const char* what) {
		auto saved = field;
		field = value;
		differs(what);
		field = saved;
	};

	auto& a = attachments[0];
	change(a.format, VK_FORMAT_B8G8R8A8_UNORM, "format changes the key");
	change(a.samples, VK_SAMPLE_COUNT_4_BIT, "sample count changes the key");
	change(a.loadOp, VK_ATTACHMENT_LOAD_OP_CLEAR, "load op changes the key");
	change(a.storeOp, VK_ATTACHMENT_STORE_OP_DONT_CARE, "store op changes the key");
	change(a.stencilLoadOp, VK_ATTACHMENT_LOAD_OP_CLEAR, "stencil load op changes the key");
	change(a.stencilStoreOp, VK_ATTACHMENT_STORE_OP_DONT_CARE, "stencil store op changes the key");
	change(a.initialLayout, VK_IMAGE_LAYOUT_UNDEFINED, "initial layout changes the key");
	change(a.finalLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "final layout changes the key");
	change(colors[1].attachment, 0u, "a filled color slot changes the key");
	change(colors[0].layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, "a reference layout changes the key");
	change(dependency.dependencyFlags, static_cast<VkDependencyFlags>(VK_DEPENDENCY_BY_REGION_BIT), "dependency flags change the key");
	change(dependency.dstAccessMask, static_cast<VkAccessFlags>(VK_ACCESS_SHADER_READ_BIT), "dependency access changes the key");
	change(info.dependencyCount, 0u, "the dependency count changes the key");

	{
		VkAttachmentReference depth { 0, VK_IMAGE_LAYOUT_GENERAL };
		subpass.pDepthStencilAttachment = &depth;
		differs("a depth attachment changes the key");
		subpass.pDepthStencilAttachment = nullptr;
	}
	{
		info.attachmentCount = 2;
		auto withoutResolve = Indium::renderPassKey(info);
		subpass.pResolveAttachments = resolves;
		auto withResolve = Indium::renderPassKey(info);
		report(withoutResolve && withResolve && !(*withoutResolve == *withResolve), "resolve attachments change the key");
		resolves[0].attachment = VK_ATTACHMENT_UNUSED;
		auto otherResolve = Indium::renderPassKey(info);
		report(otherResolve && !(*otherResolve == *withResolve), "a different resolve target changes the key");
		resolves[0].attachment = 1;
		subpass.pResolveAttachments = nullptr;
		info.attachmentCount = 1;
	}

	{
		// equal content at other addresses gives the same key, and works as a hash map key
		VkAttachmentDescription copy = attachments[0];
		info.pAttachments = &copy;
		auto key = Indium::renderPassKey(info);
		report(key && *key == *original, "equal descriptions at other addresses share a key");
		std::unordered_map<Indium::RenderPassKey, int, Indium::RenderPassKey::Hash> cache;
		cache.emplace(*original, 42);
		report(key && cache.count(*key) && cache.at(*key) == 42, "the key finds its entry in a hash map");
		info.pAttachments = attachments;
	}

	{
		VkRenderPassMultiviewCreateInfo multiview {};
		multiview.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
		info.pNext = &multiview;
		report(!Indium::renderPassKey(info), "an extension structure is not cached");
		info.pNext = nullptr;

		VkAttachmentReference input { 0, VK_IMAGE_LAYOUT_GENERAL };
		subpass.inputAttachmentCount = 1;
		subpass.pInputAttachments = &input;
		report(!Indium::renderPassKey(info), "input attachments are not cached");
		subpass.inputAttachmentCount = 0;
		subpass.pInputAttachments = nullptr;

		uint32_t preserved = 0;
		subpass.preserveAttachmentCount = 1;
		subpass.pPreserveAttachments = &preserved;
		report(!Indium::renderPassKey(info), "preserved attachments are not cached");
		subpass.preserveAttachmentCount = 0;
		subpass.pPreserveAttachments = nullptr;

		VkSubpassDescription two[2] { subpass, subpass };
		info.subpassCount = 2;
		info.pSubpasses = two;
		report(!Indium::renderPassKey(info), "several subpasses are not cached");
		info.subpassCount = 1;
		info.pSubpasses = &subpass;
	}

	{
		// the largest description the key accepts fits, one more attachment does not
		std::array<VkAttachmentDescription, Indium::RenderPassKey::maxAttachments + 1> many {};
		std::array<VkAttachmentReference, Indium::RenderPassKey::maxAttachments> references {};
		std::array<VkSubpassDependency, Indium::RenderPassKey::maxDependencies> dependencies {};
		VkAttachmentReference depth { 0, VK_IMAGE_LAYOUT_GENERAL };
		info.attachmentCount = Indium::RenderPassKey::maxAttachments;
		info.pAttachments = many.data();
		subpass.colorAttachmentCount = references.size();
		subpass.pColorAttachments = references.data();
		subpass.pResolveAttachments = references.data();
		subpass.pDepthStencilAttachment = &depth;
		info.dependencyCount = dependencies.size();
		info.pDependencies = dependencies.data();
		auto largest = Indium::renderPassKey(info);
		report(largest && largest->size == largest->words.size(), "the largest accepted description fills the key exactly");
		info.attachmentCount = Indium::RenderPassKey::maxAttachments + 1;
		report(!Indium::renderPassKey(info), "more attachments than the key holds are not cached");
		info.attachmentCount = 1;
		info.dependencyCount = Indium::RenderPassKey::maxDependencies + 1;
		report(!Indium::renderPassKey(info), "more dependencies than the key holds are not cached");
	}

	std::printf("%s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
