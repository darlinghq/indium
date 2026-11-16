// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace Indium {
	class Device;

	/**
	 * A counter that command buffers of one device signal and wait on, in the order the values say. Its value
	 * starts at 0 and only goes up. A command buffer's wait on a value the event has already reached does not
	 * hold anything up.
	 */
	class Event {
	public:
		virtual ~Event() = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};

	/**
	 * Names a shared event so that another object can be made for it with `Device::newSharedEvent()`. A handle
	 * only works inside the process that made it.
	 */
	class SharedEventHandle {
	public:
		virtual ~SharedEventHandle() = 0;
	};

	/**
	 * An event whose value the host can read and set as well, and wait on or be notified about.
	 */
	class SharedEvent: public Event {
	public:
		virtual ~SharedEvent() = 0;

		/**
		 * Called when the event has reached the value it was registered for, on the thread that polls the device
		 * (`Device::pollEvents()`).
		 */
		using NotificationBlock = std::function<void()>;

		virtual uint64_t signaledValue() = 0;

		/**
		 * Raises the value to `value`. A value that does not exceed the current one leaves the event unchanged.
		 */
		virtual void setSignaledValue(uint64_t value) = 0;

		/**
		 * Calls `block` once the value is at least `value` (from the polling thread, so also if it already is). The
		 * registration holds the event until it has been called; blocks registered for values reached together are
		 * called in the order they were registered.
		 */
		virtual void notify(uint64_t value, NotificationBlock block) = 0;

		/**
		 * Waits until the value is at least `value` or `timeoutMilliseconds` have passed.
		 *
		 * @returns whether the value was reached.
		 */
		virtual bool waitUntilSignaledValue(uint64_t value, uint64_t timeoutMilliseconds) = 0;

		virtual std::shared_ptr<SharedEventHandle> newSharedEventHandle() = 0;
	};

	/**
	 * Orders work between encoders: the work an encoder updates the fence after is done before the work another
	 * encoder waits for the fence before. Created by `Device::newFence()`, used with the encoders'
	 * `updateFence()` and `waitForFence()`.
	 */
	class Fence {
	public:
		virtual ~Fence() = 0;

		virtual std::shared_ptr<Device> device() = 0;
	};
};
