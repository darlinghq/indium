#include <indium/command-queue.private.hpp>
#include <indium/device.private.hpp>
#include <indium/command-buffer.private.hpp>
#include <indium/dynamic-vk.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <thread>

namespace {
	// How many queue workers have been woken and have not finished what they were woken for, plus the event loop
	// callbacks that are about to wake one. finit() waits for it to reach zero, so that no thread is still
	// releasing a command buffer's resources, and with them perhaps the last reference to a device, when
	// the process starts to shut the Vulkan driver down.
	std::atomic<size_t> activeWork { 0 };
}

Indium::QueueActivity::QueueActivity() {
	activeWork.fetch_add(1);
};

Indium::QueueActivity::~QueueActivity() {
	activeWork.fetch_sub(1);
};

void Indium::waitForQueueActivity(std::chrono::milliseconds timeout) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (activeWork.load() != 0 && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
};

// A thread that runs a function every time it is woken. It never exits on its own and is never joined: the state is
// shared with the thread, `shutdown()` ends it, and the function only touches the queue through a pointer it has to
// lock, so a queue that is freed on the thread itself (by the last reference a command buffer held) is not a problem.
struct Indium::QueueWorker {
	std::mutex mutex;
	std::condition_variable condition;
	bool wake = false;
	bool stop = false;

	void notify() {
		{
			std::scoped_lock lock(mutex);
			if (!wake) {
				// counted until the thread has done what it was woken for
				wake = true;
				activeWork.fetch_add(1);
			}
		}
		condition.notify_one();
	}

	void shutdown() {
		{
			std::scoped_lock lock(mutex);
			stop = true;
		}
		condition.notify_all();
	}

	static std::shared_ptr<QueueWorker> start(std::function<void()> body) {
		auto worker = std::make_shared<QueueWorker>();
		std::thread([worker, body = std::move(body)]() {
			for (;;) {
				{
					std::unique_lock lock(worker->mutex);
					worker->condition.wait(lock, [&]() { return worker->wake || worker->stop; });
					if (worker->stop) {
						if (worker->wake) {
							activeWork.fetch_sub(1);
						}
						return;
					}
					worker->wake = false;
				}
				body();
				activeWork.fetch_sub(1);
			}
		}).detach();
		return worker;
	}
};

Indium::CommandQueue::~CommandQueue() {};

std::shared_ptr<Indium::Device> Indium::PrivateCommandQueue::device() {
	return _privateDevice;
};

Indium::PrivateCommandQueue::PrivateCommandQueue(std::shared_ptr<PrivateDevice> device):
	_privateDevice(device)
{
	// TODO: support the case of having different graphics and compute queues
	if (_privateDevice->graphicsQueueFamilyIndex() || _privateDevice->computeQueueFamilyIndex()) {
		_queueFamilyIndex = _privateDevice->graphicsQueueFamilyIndex() ? *_privateDevice->graphicsQueueFamilyIndex() : *_privateDevice->computeQueueFamilyIndex();

		_supportsGraphics = !!_privateDevice->graphicsQueueFamilyIndex();
		_supportsCompute = _supportsGraphics ? (_privateDevice->computeQueueFamilyIndex() ? *_privateDevice->computeQueueFamilyIndex() == *_privateDevice->graphicsQueueFamilyIndex() : false) : !!_privateDevice->computeQueueFamilyIndex();
	}
};

Indium::PrivateCommandQueue::~PrivateCommandQueue() {
	if (_submissionWorker) {
		_submissionWorker->shutdown();
	}
	if (_handlerWorker) {
		_handlerWorker->shutdown();
	}
	// every command buffer holds a reference to its queue, so no pool is leased any more
	for (auto commandPool: _idleCommandPools) {
		DynamicVK::vkDestroyCommandPool(_privateDevice->device(), commandPool, nullptr);
	}
};

VkCommandPool Indium::PrivateCommandQueue::acquireCommandPool() {
	{
		std::scoped_lock lock(_commandPoolsMutex);
		if (!_idleCommandPools.empty()) {
			auto commandPool = _idleCommandPools.back();
			_idleCommandPools.pop_back();
			return commandPool;
		}
	}

	if (!_queueFamilyIndex) {
		throw std::runtime_error("Command queue has no queue family to record for");
	}

	VkCommandPoolCreateInfo createInfo {};
	createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	createInfo.queueFamilyIndex = *_queueFamilyIndex;

	VkCommandPool commandPool = VK_NULL_HANDLE;
	if (DynamicVK::vkCreateCommandPool(_privateDevice->device(), &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create a command pool");
	}
	return commandPool;
};

void Indium::PrivateCommandQueue::releaseCommandPool(VkCommandPool commandPool) {
	// the pool's command buffers have been freed, so resetting returns their memory to it
	DynamicVK::vkResetCommandPool(_privateDevice->device(), commandPool, 0);
	std::scoped_lock lock(_commandPoolsMutex);
	_idleCommandPools.push_back(commandPool);
};

void Indium::PrivateCommandQueue::startWorkers() {
	std::scoped_lock lock(_workersMutex);
	if (_submissionWorker) {
		return;
	}
	std::weak_ptr<PrivateCommandQueue> weak = shared_from_this();
	_submissionWorker = QueueWorker::start([weak]() {
		if (auto queue = weak.lock()) {
			queue->drain();
		}
	});
	_handlerWorker = QueueWorker::start([weak]() {
		if (auto queue = weak.lock()) {
			queue->runHandlers();
		}
	});
};

void Indium::PrivateCommandQueue::enqueue(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer) {
	std::scoped_lock lock(_orderMutex);
	for (const auto& entry: _order) {
		if (entry.key == commandBuffer.get()) {
			return;
		}
	}
	_order.push_back(Entry { commandBuffer.get(), commandBuffer, nullptr });
};

void Indium::PrivateCommandQueue::abandon(PrivateCommandBuffer* commandBuffer) {
	{
		std::scoped_lock lock(_orderMutex);
		auto it = std::find_if(_order.begin(), _order.end(), [&](const Entry& entry) { return entry.key == commandBuffer && !entry.committed; });
		if (it == _order.end()) {
			return;
		}
		_order.erase(it);
	}
	// the command buffers behind it may be ready now
	wakeSubmission();
};

void Indium::PrivateCommandQueue::commit(const std::shared_ptr<PrivateCommandBuffer>& commandBuffer) {
	startWorkers();
	{
		std::scoped_lock lock(_orderMutex);
		auto it = std::find_if(_order.begin(), _order.end(), [&](const Entry& entry) { return entry.key == commandBuffer.get(); });
		if (it == _order.end()) {
			_order.push_back(Entry { commandBuffer.get(), commandBuffer, commandBuffer });
		} else {
			it->committed = commandBuffer;
		}
	}
	wakeSubmission();
};

void Indium::PrivateCommandQueue::wakeSubmission() {
	std::shared_ptr<QueueWorker> worker;
	{
		std::scoped_lock lock(_workersMutex);
		worker = _submissionWorker;
	}
	if (worker) {
		worker->notify();
	}
};

void Indium::PrivateCommandQueue::wakeHandlers() {
	std::shared_ptr<QueueWorker> worker;
	{
		std::scoped_lock lock(_workersMutex);
		worker = _handlerWorker;
	}
	if (worker) {
		worker->notify();
	}
};

void Indium::PrivateCommandQueue::drain() {
	for (;;) {
		std::shared_ptr<PrivateCommandBuffer> commandBuffer;
		{
			std::scoped_lock lock(_orderMutex);
			// an enqueued command buffer that has not been committed holds back the ones after it
			if (_order.empty() || !_order.front().committed) {
				return;
			}
			commandBuffer = _order.front().committed;
		}

		// what is to be called once nothing is locked
		std::vector<std::function<void()>> afterSubmit;
		if (!commandBuffer->submitReady(afterSubmit)) {
			return;
		}
		{
			std::scoped_lock lock(_orderMutex);
			_order.pop_front();
		}
		{
			std::scoped_lock lock(_handlerMutex);
			_toSchedule.push_back(commandBuffer);
			_inFlight.push_back(commandBuffer);
			_unfinished.push_back(commandBuffer);
			watchCompletion();
		}
		for (const auto& call: afterSubmit) {
			call();
		}
		wakeHandlers();
	}
};

void Indium::PrivateCommandQueue::nextCompletion(VkSemaphore& semaphore, uint64_t& value) {
	if (!_completionTimeline) {
		_completionTimeline = _privateDevice->getWrappedTimelineSemaphore();
	}
	semaphore = _completionTimeline->semaphore;
	value = ++_completionTimeline->count;
};

void Indium::PrivateCommandQueue::watchCompletion() {
	if (_completionWatched || _unfinished.empty()) {
		return;
	}
	_completionWatched = true;
	std::weak_ptr<PrivateCommandQueue> weak = shared_from_this();
	_privateDevice->waitForSemaphore(_completionTimeline->semaphore, _unfinished.front()->completionValue(), [weak]() {
		if (auto queue = weak.lock()) {
			queue->onCompletion();
		}
	});
};

void Indium::PrivateCommandQueue::onCompletion() {
	// still counted when markCompleted() lets a waiting thread go, until the handler thread has been woken
	QueueActivity activity;
	uint64_t reached = 0;
	if (DynamicVK::vkGetSemaphoreCounterValue(_privateDevice->device(), _completionTimeline->semaphore, &reached) != VK_SUCCESS) {
		// an unrecoverable failure, like the other queue calls that abort
		abort();
	}

	std::vector<std::shared_ptr<PrivateCommandBuffer>> finished;
	{
		std::scoped_lock lock(_handlerMutex);
		_completionWatched = false;
		while (!_unfinished.empty() && _unfinished.front()->completionValue() <= reached) {
			finished.push_back(std::move(_unfinished.front()));
			_unfinished.pop_front();
		}
		watchCompletion();
	}
	for (const auto& commandBuffer: finished) {
		commandBuffer->markCompleted();
	}
	wakeHandlers();
};

void Indium::PrivateCommandQueue::runHandlers() {
	for (;;) {
		std::shared_ptr<PrivateCommandBuffer> commandBuffer;
		bool schedule = false;
		{
			std::scoped_lock lock(_handlerMutex);
			if (!_toSchedule.empty()) {
				commandBuffer = std::move(_toSchedule.front());
				_toSchedule.pop_front();
				schedule = true;
			} else if (!_inFlight.empty() && _inFlight.front()->retirable()) {
				commandBuffer = std::move(_inFlight.front());
				_inFlight.pop_front();
			} else {
				return;
			}
		}
		if (schedule) {
			commandBuffer->runScheduledHandlers();
		} else {
			commandBuffer->retire();
		}
	}
};

std::shared_ptr<Indium::CommandBuffer> Indium::PrivateCommandQueue::commandBuffer() {
	return std::make_shared<PrivateCommandBuffer>(shared_from_this());
};
