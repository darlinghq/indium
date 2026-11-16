// Copyright (C) 2026 Lubos Dolezel
#include <indium/event.private.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

Indium::Event::~Event() {};
Indium::SharedEvent::~SharedEvent() {};
Indium::SharedEventHandle::~SharedEventHandle() {};
Indium::Fence::~Fence() {};

Indium::EventCore::EventCore(std::shared_ptr<PrivateDevice> device):
	_device(std::move(device))
{
	VkSemaphoreTypeCreateInfo typeInfo {};
	typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	typeInfo.initialValue = 0;

	VkSemaphoreCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &typeInfo;

	if (DynamicVK::vkCreateSemaphore(_device->device(), &createInfo, nullptr, &_semaphore) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create an event");
	}
};

Indium::EventCore::~EventCore() {
	DynamicVK::vkDestroySemaphore(_device->device(), _semaphore, nullptr);
};

uint64_t Indium::EventCore::value() const {
	uint64_t value = 0;
	if (DynamicVK::vkGetSemaphoreCounterValue(_device->device(), _semaphore, &value) != VK_SUCCESS) {
		return 0;
	}
	return value;
};

bool Indium::EventCore::signalFromHost(uint64_t value) {
	std::scoped_lock lock(_mutex);

	if (value <= this->value()) {
		return false;
	}

	VkSemaphoreSignalInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
	info.semaphore = _semaphore;
	info.value = value;
	if (DynamicVK::vkSignalSemaphore(_device->device(), &info) != VK_SUCCESS) {
		return false;
	}
	_promised = std::max(_promised, value);
	return true;
};

bool Indium::EventCore::reserveDeviceSignal(uint64_t value) {
	std::scoped_lock lock(_mutex);

	if (value <= _promised || value <= this->value()) {
		return false;
	}
	_promised = value;
	return true;
};

bool Indium::EventCore::wait(uint64_t value, uint64_t timeoutNanoseconds) const {
	VkSemaphoreWaitInfo info {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	info.semaphoreCount = 1;
	info.pSemaphores = &_semaphore;
	info.pValues = &value;
	return DynamicVK::vkWaitSemaphores(_device->device(), &info, timeoutNanoseconds) == VK_SUCCESS;
};

void Indium::EventCore::notify(uint64_t value, SharedEvent::NotificationBlock block) {
	// the callback holds the core, so the semaphore stays valid until the device's event loop has called it
	auto self = shared_from_this();
	_device->waitForSemaphore(_semaphore, value, [self, block = std::move(block)]() {
		block();
	});
};

Indium::PrivateEvent::PrivateEvent(std::shared_ptr<EventCore> core): _core(std::move(core)) {};

std::shared_ptr<Indium::Device> Indium::PrivateEvent::device() {
	return _core->device();
};

Indium::PrivateSharedEventHandle::PrivateSharedEventHandle(std::shared_ptr<EventCore> core): _core(std::move(core)) {};

Indium::PrivateSharedEvent::PrivateSharedEvent(std::shared_ptr<EventCore> core): _core(std::move(core)) {};

std::shared_ptr<Indium::Device> Indium::PrivateSharedEvent::device() {
	return _core->device();
};

uint64_t Indium::PrivateSharedEvent::signaledValue() {
	return _core->value();
};

void Indium::PrivateSharedEvent::setSignaledValue(uint64_t value) {
	_core->signalFromHost(value);
};

void Indium::PrivateSharedEvent::notify(uint64_t value, NotificationBlock block) {
	_core->notify(value, std::move(block));
};

bool Indium::PrivateSharedEvent::waitUntilSignaledValue(uint64_t value, uint64_t timeoutMilliseconds) {
	constexpr uint64_t nanosecondsPerMillisecond = 1000000;
	uint64_t timeout = timeoutMilliseconds > std::numeric_limits<uint64_t>::max() / nanosecondsPerMillisecond ?
		std::numeric_limits<uint64_t>::max() : timeoutMilliseconds * nanosecondsPerMillisecond;
	return _core->wait(value, timeout);
};

std::shared_ptr<Indium::SharedEventHandle> Indium::PrivateSharedEvent::newSharedEventHandle() {
	return std::make_shared<PrivateSharedEventHandle>(_core);
};

Indium::PrivateFence::PrivateFence(std::shared_ptr<PrivateDevice> device): _device(std::move(device)) {};

std::shared_ptr<Indium::Device> Indium::PrivateFence::device() {
	return _device;
};
