// Copyright (C) 2026 Lubos Dolezel

#pragma once

#include <indium/command-encoder.hpp>
#include <indium/render-command-encoder.hpp>
#include <indium/types.hpp>

#include <memory>

namespace Indium {
	/**
	 * A render pass that several render command encoders record, possibly on different threads. Each encoder
	 * returned by renderCommandEncoder() is a pass of its own on the same attachments; its commands run in the order
	 * the encoders were created, whatever the order they were used or ended in. The load actions of the pass
	 * apply once, at its start, and the store actions once, at its end. Every encoder starts with Metal's default
	 * state, not the state the encoder before it left.
	 *
	 * Nothing is recorded into the command buffer before endEncoding(), which has to come after the sub-encoders'.
	 */
	class ParallelRenderCommandEncoder: public CommandEncoder {
	public:
		virtual ~ParallelRenderCommandEncoder() = 0;

		/** A new encoder, whose commands come after those of the encoders made before it. */
		virtual std::shared_ptr<RenderCommandEncoder> renderCommandEncoder() = 0;

		/** Changes the store action of an attachment of the pass; a color index without an attachment is ignored. */
		virtual void setColorStoreAction(StoreAction storeAction, size_t colorAttachmentIndex) = 0;
		virtual void setColorStoreActionOptions(StoreActionOptions options, size_t colorAttachmentIndex) = 0;
		virtual void setDepthStoreAction(StoreAction storeAction) = 0;
		virtual void setDepthStoreActionOptions(StoreActionOptions options) = 0;
		virtual void setStencilStoreAction(StoreAction storeAction) = 0;
		virtual void setStencilStoreActionOptions(StoreActionOptions options) = 0;
	};
};
