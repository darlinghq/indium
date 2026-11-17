#pragma once

#include <indium/types.hpp>

#include <memory>

namespace Indium {
	class Device;
	class Heap;

	class Resource {
	public:
		virtual ~Resource() = 0;

		virtual std::shared_ptr<Device> device() = 0;

		/** The heap the resource was made from, `nullptr` for any other resource. */
		virtual std::shared_ptr<Heap> heap();

		/** Where the resource starts in a placement heap; 0 for every other resource, an automatic heap's included. */
		virtual size_t heapOffset();

		/** The bytes of device memory the resource takes (0 where the resource has none of its own). */
		virtual size_t allocatedSize();

		/**
		 * Tells an automatic heap that the resource's memory may be given to other resources. The resource stays
		 * valid, but its contents are no longer kept. Has no effect on any other resource.
		 */
		virtual void makeAliasable();

		/** Whether the resource's memory may be shared with other resources: always true in a placement heap. */
		virtual bool isAliasable();

		/**
		 * Whether Indium orders the commands that use the resource (HazardTrackingModeTracked) or leaves that to
		 * the application's fences, events and memory barriers (HazardTrackingModeUntracked). Never Default: the
		 * default has been resolved, to the mode of the resource's heap, or to tracked.
		 */
		virtual HazardTrackingMode hazardTrackingMode() const;

		// TODO: other properties
	};
};
