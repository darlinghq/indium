// Copyright (C) 2026 Lubos Dolezel
#pragma once

#include <vulkan/vulkan.h>

#include <indium/event.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace Indium {
	class PrivateDevice;

	/**
	 * The timeline semaphore behind an event, shared by the event object, every object made from its handle and
	 * the command buffers that signal or wait on it.
	 *
	 * Vulkan requires every signal of a timeline semaphore, from the host or from a queue, to raise its value, and
	 * that is checked against the value the semaphore will have by then, so signals that would not raise it are
	 * dropped here: a signal from a command buffer when an earlier one (or the host) already promised the value or
	 * more, and a host signal when the value already is that or more.
	 */
	class EventCore: public std::enable_shared_from_this<EventCore> {
	public:
		explicit EventCore(std::shared_ptr<PrivateDevice> device);
		~EventCore();

		EventCore(const EventCore&) = delete;
		EventCore& operator=(const EventCore&) = delete;

		const std::shared_ptr<PrivateDevice>& device() const { return _device; }
		VkSemaphore semaphore() const { return _semaphore; }

		uint64_t value() const;
		bool reached(uint64_t value) const { return this->value() >= value; }

		/** Raises the value from the host. @returns whether it did. */
		bool signalFromHost(uint64_t value);

		/**
		 * Records that a command buffer is about to signal `value`.
		 * @returns whether the signal is to be submitted: false when `value` does not exceed what has been signaled or
		 *          promised by earlier signals.
		 */
		bool reserveDeviceSignal(uint64_t value);

		bool wait(uint64_t value, uint64_t timeoutNanoseconds) const;
		void notify(uint64_t value, SharedEvent::NotificationBlock block);


	private:
		std::shared_ptr<PrivateDevice> _device;
		VkSemaphore _semaphore = VK_NULL_HANDLE;
		std::mutex _mutex;
		// the highest value signaled or promised so far
		uint64_t _promised = 0;
	};

	/** What the command buffer needs from an event of either kind. */
	class PrivateEventBase {
	public:
		virtual ~PrivateEventBase() = default;
		virtual const std::shared_ptr<EventCore>& core() const = 0;
	};

	class PrivateEvent: public Event, public PrivateEventBase {
	public:
		explicit PrivateEvent(std::shared_ptr<EventCore> core);

		virtual std::shared_ptr<Device> device() override;
		virtual const std::shared_ptr<EventCore>& core() const override { return _core; }

	private:
		std::shared_ptr<EventCore> _core;
	};

	class PrivateSharedEventHandle: public SharedEventHandle {
	public:
		explicit PrivateSharedEventHandle(std::shared_ptr<EventCore> core);

		const std::shared_ptr<EventCore>& core() const { return _core; }

	private:
		std::shared_ptr<EventCore> _core;
	};

	class PrivateSharedEvent: public SharedEvent, public PrivateEventBase {
	public:
		explicit PrivateSharedEvent(std::shared_ptr<EventCore> core);

		virtual std::shared_ptr<Device> device() override;
		virtual uint64_t signaledValue() override;
		virtual void setSignaledValue(uint64_t value) override;
		virtual void notify(uint64_t value, NotificationBlock block) override;
		virtual bool waitUntilSignaledValue(uint64_t value, uint64_t timeoutMilliseconds) override;
		virtual std::shared_ptr<SharedEventHandle> newSharedEventHandle() override;
		virtual const std::shared_ptr<EventCore>& core() const override { return _core; }

	private:
		std::shared_ptr<EventCore> _core;
	};

	/**
	 * Fences order work between encoders, and Indium submits all of a device's command queues on one Vulkan
	 * queue. Resources that are not tracked are ordered by nothing but fences, events and memory barriers, so
	 * a wait records a memory dependency on everything recorded before it (in submission order, so also across
	 * command buffers): the work an encoder updated a fence after is done and visible to what comes after
	 * the wait, whichever resources it touched. An update records nothing, and a wait that comes before the
	 * update (which Metal also accepts) holds nothing up. A render pass cannot take a barrier in the
	 * middle of it, so its encoder records the dependency in front of the whole pass.
	 */
	inline void checkFence(const std::shared_ptr<Fence>& fence, const std::shared_ptr<Device>& device) {
		if (!fence || fence->device() != device) {
			throw std::invalid_argument("The fence does not belong to the encoder's device");
		}
	}

	class PrivateFence: public Fence {
	public:
		explicit PrivateFence(std::shared_ptr<PrivateDevice> device);

		virtual std::shared_ptr<Device> device() override;

	private:
		std::shared_ptr<PrivateDevice> _device;
	};
};
