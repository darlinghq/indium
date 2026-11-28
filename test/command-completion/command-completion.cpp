// SPDX-License-Identifier: MPL-2.0
// Contract: waitUntilCompleted waits for GPU execution and all completed handlers.
// https://developer.apple.com/documentation/metal/mtlcommandbuffer/waituntilcompleted()
#include <indium/indium.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {
unsigned failures=0;
void expect(bool condition,const char* message) {
    std::printf("%s %s\n",condition?"PASS":"FAIL",message);
    if(!condition) ++failures;
}
struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered=false,released=false;
    void hold() {
        std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();
        condition.wait(lock,[&] {return released;});
    }
    bool awaitEntry() {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock,std::chrono::seconds(5),[&] {return entered;});
    }
    void release() {std::lock_guard<std::mutex> lock(mutex);released=true;condition.notify_all();}
};
void waitsForEveryHandler(const std::shared_ptr<Indium::Device>& device,unsigned blockedIndex) {
    auto queue=device->newCommandQueue();auto command=queue->commandBuffer();Gate gate;
    std::mutex valuesMutex;std::vector<unsigned> callbackOrder;
    for(unsigned i=0;i<3;++i) command->addCompletedHandler([&,i](auto) {
        if(i==blockedIndex) gate.hold();
        std::lock_guard<std::mutex> lock(valuesMutex);callbackOrder.push_back(i);
    });
    command->commit();
    bool entered=gate.awaitEntry();expect(entered,"a real GPU submission enters the held completion handler");
    std::mutex observerMutex;std::condition_variable observer;
    unsigned announced=0,returned=0;
    std::vector<std::thread> waiters;
    for(unsigned i=0;i<2;++i) waiters.emplace_back([&] {
        {std::lock_guard<std::mutex> lock(observerMutex);++announced;observer.notify_all();}
        command->waitUntilCompleted();
        {std::lock_guard<std::mutex> lock(observerMutex);++returned;observer.notify_all();}
    });
    {
        std::unique_lock<std::mutex> lock(observerMutex);
        expect(observer.wait_for(lock,std::chrono::seconds(5),[&] {return announced==2;}),"both waiting threads announce their wait");
        // Bounded observation, not an assumption that both callers reached a
        // kernel wait. The broken implementation returns during this window.
        bool early=observer.wait_for(lock,std::chrono::milliseconds(150),[&] {return returned!=0;});
        expect(!early,"waitUntilCompleted stays blocked while a completion handler is held");
    }
    gate.release();
    for(auto& waiter:waiters) waiter.join();
    // The joins alone do not order the handler's writes: the command-buffer
    // completion wait must establish that ordering. Wait for a terminal handler
    // below before inspecting the fixture on a broken implementation.
    std::mutex tailMutex;std::condition_variable tail;bool tailDone=false;
    auto tailCommand=queue->commandBuffer();
    tailCommand->addCompletedHandler([&](auto) {std::lock_guard<std::mutex> lock(tailMutex);tailDone=true;tail.notify_all();});
    tailCommand->commit();
    {std::unique_lock<std::mutex> lock(tailMutex);expect(tail.wait_for(lock,std::chrono::seconds(5),[&] {return tailDone;}),"the callback queue drains after releasing the handler");}
    expect(returned==2,"both waiters return after releasing the handler");
    expect(callbackOrder==std::vector<unsigned>({0,1,2}),"all registered callbacks run exactly once and in order");
    command->waitUntilCompleted();
}
void emptyCompletionListRemainsReentrant(const std::shared_ptr<Indium::Device>& device) {
    auto queue=device->newCommandQueue();auto outer=queue->commandBuffer();Gate finished;
    outer->addCompletedHandler([&](auto) {
        auto inner=queue->commandBuffer();inner->commit();inner->waitUntilCompleted();
        std::lock_guard<std::mutex> lock(finished.mutex);finished.entered=true;finished.condition.notify_all();
    });
    outer->commit();expect(finished.awaitEntry(),"a completion handler can wait for a nested buffer without callbacks");outer->waitUntilCompleted();
    auto scheduled=queue->commandBuffer();std::atomic<bool> waited{false};
    scheduled->addScheduledHandler([&](auto buffer) {buffer->waitUntilCompleted();waited=true;});
    scheduled->commit();scheduled->waitUntilScheduled();expect(waited,"a scheduled handler can wait for GPU completion without callbacks");
    scheduled->waitUntilCompleted();
}
}
int main() {
    std::setbuf(stdout,nullptr);
    Indium::init();
    int result=0;
    {
        auto device=Indium::createSystemDefaultDevice();
        if(!device) {std::puts("SKIP no Vulkan device");Indium::finit();return 77;}
        std::atomic<bool> polling{true};
        std::thread poller([&] {while(polling) device->pollEvents(UINT64_MAX);});
        waitsForEveryHandler(device,0);
        waitsForEveryHandler(device,2);
        emptyCompletionListRemainsReentrant(device);
        polling=false;device->wakeupEventLoop();poller.join();result=failures?1:0;
    }
    Indium::finit();return result;
}
