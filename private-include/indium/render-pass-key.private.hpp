#pragma once

// Copyright (C) 2026 Darling Developers

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Indium {
	/**
	 * Everything a VkRenderPassCreateInfo says, as a flat list of words, so identical render passes
	 * can share one VkRenderPass. The key records the exact description rather than Vulkan's notion
	 * of render pass compatibility, because load/store operations and layouts decide what happens
	 * to the attachments' contents, and compatible passes may differ in those.
	 */
	struct RenderPassKey {
		static constexpr size_t maxAttachments = 16;
		static constexpr size_t maxDependencies = 4;
		// flags and counts, 9 words per attachment, 2 per color/resolve/depth reference, 7 per dependency
		std::array<uint32_t, 8 + maxAttachments * 9 + (2 * maxAttachments + 1) * 2 + maxDependencies * 7> words {};
		size_t size = 0;

		bool operator==(const RenderPassKey& other) const {
			return size == other.size && std::equal(words.begin(), words.begin() + size, other.words.begin());
		}

		struct Hash {
			size_t operator()(const RenderPassKey& key) const {
				// FNV-1a over the words
				uint64_t result = 14695981039346656037ull;
				for (size_t i = 0; i < key.size; ++i) {
					result = (result ^ key.words[i]) * 1099511628211ull;
				}
				return static_cast<size_t>(result);
			}
		};
	};

	/**
	 * The key for a render pass with one subpass, as the render command encoder creates them, or
	 * nothing for a description this does not cover (extension structures, several subpasses, input
	 * or preserved attachments, more than RenderPassKey's limits); those are not cached.
	 */
	inline std::optional<RenderPassKey> renderPassKey(const VkRenderPassCreateInfo& info) {
		if (info.pNext || info.subpassCount != 1 || info.attachmentCount > RenderPassKey::maxAttachments || info.dependencyCount > RenderPassKey::maxDependencies) {
			return std::nullopt;
		}

		const auto& subpass = info.pSubpasses[0];
		if (subpass.inputAttachmentCount || subpass.preserveAttachmentCount || subpass.colorAttachmentCount > RenderPassKey::maxAttachments) {
			return std::nullopt;
		}

		RenderPassKey key;
		auto add = [&](uint32_t word) { key.words[key.size++] = word; };
		auto addReference = [&](const VkAttachmentReference& reference) {
			add(reference.attachment);
			add(reference.layout);
		};

		add(info.flags);
		add(info.attachmentCount);
		for (uint32_t i = 0; i < info.attachmentCount; ++i) {
			const auto& attachment = info.pAttachments[i];
			add(attachment.flags);
			add(attachment.format);
			add(attachment.samples);
			add(attachment.loadOp);
			add(attachment.storeOp);
			add(attachment.stencilLoadOp);
			add(attachment.stencilStoreOp);
			add(attachment.initialLayout);
			add(attachment.finalLayout);
		}

		add(subpass.flags);
		add(subpass.pipelineBindPoint);
		add(subpass.colorAttachmentCount);
		for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i) {
			addReference(subpass.pColorAttachments[i]);
		}
		add(subpass.pResolveAttachments != nullptr);
		if (subpass.pResolveAttachments) {
			for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i) {
				addReference(subpass.pResolveAttachments[i]);
			}
		}
		add(subpass.pDepthStencilAttachment != nullptr);
		if (subpass.pDepthStencilAttachment) {
			addReference(*subpass.pDepthStencilAttachment);
		}

		add(info.dependencyCount);
		for (uint32_t i = 0; i < info.dependencyCount; ++i) {
			const auto& dependency = info.pDependencies[i];
			add(dependency.srcSubpass);
			add(dependency.dstSubpass);
			add(dependency.srcStageMask);
			add(dependency.dstStageMask);
			add(dependency.srcAccessMask);
			add(dependency.dstAccessMask);
			add(dependency.dependencyFlags);
		}

		return key;
	}
};
