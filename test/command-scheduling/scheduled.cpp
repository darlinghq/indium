#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <stdexcept>
static void check(VkResult result){if(result!=VK_SUCCESS)throw std::runtime_error("authored scheduling Vulkan setup failed");}
int main() try {
 Indium::init();auto device=std::static_pointer_cast<Indium::PrivateDevice>(Indium::createSystemDefaultDevice());if(!device)return 2;
 printf("GPU %s\n",device->name().c_str());
 VkSemaphoreTypeCreateInfo type{};type.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
 VkSemaphoreCreateInfo create{};create.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;create.pNext=&type;
 VkSemaphore gate;check(Indium::DynamicVK::vkCreateSemaphore(device->device(),&create,nullptr,&gate));
 VkSemaphoreSubmitInfo wait{};wait.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;wait.semaphore=gate;wait.value=1;wait.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
 VkSubmitInfo2 submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO_2;submit.waitSemaphoreInfoCount=1;submit.pWaitSemaphoreInfos=&wait;
 VkFenceCreateInfo fenceInfo{};fenceInfo.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;VkFence fence;check(Indium::DynamicVK::vkCreateFence(device->device(),&fenceInfo,nullptr,&fence));
 check(Indium::DynamicVK::vkQueueSubmit2(device->graphicsQueue(),1,&submit,fence));
 std::atomic<bool> stop{false};std::mutex mutex;std::condition_variable condition;bool scheduled=false,completed=false,releaseHandler=false;int scheduledCount=0,completedCount=0;std::atomic<int> waitersReturned{0},waitersEntered{0};
 auto buffer=device->newCommandQueue()->commandBuffer();
 buffer->addScheduledHandler([&](auto){std::unique_lock lock(mutex);++scheduledCount;scheduled=true;condition.notify_all();condition.wait(lock,[&]{return releaseHandler;});});
 buffer->addCompletedHandler([&](auto){std::lock_guard lock(mutex);++completedCount;completed=true;condition.notify_all();});
 std::thread poller([&]{while(!stop)device->pollEvents(1000000);});std::thread committer([&]{buffer->commit();});
 std::thread waiter1([&]{++waitersEntered;condition.notify_all();buffer->waitUntilScheduled();++waitersReturned;});std::thread waiter2([&]{++waitersEntered;condition.notify_all();buffer->waitUntilScheduled();++waitersReturned;});
 bool before,completedBefore;{std::unique_lock lock(mutex);before=condition.wait_for(lock,std::chrono::seconds(1),[&]{return scheduled;});completedBefore=completed;if(completed)before=false;}
 bool entered;{std::unique_lock lock(mutex);entered=condition.wait_for(lock,std::chrono::seconds(5),[&]{return waitersEntered==2;});}std::this_thread::sleep_for(std::chrono::milliseconds(100));bool blocked=entered&&waitersReturned==0;{std::lock_guard lock(mutex);releaseHandler=true;condition.notify_all();}committer.join();waiter1.join();waiter2.join();
 bool scheduledWaitOnly=waitersReturned==2;
 VkSemaphoreSignalInfo signal{};signal.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;signal.semaphore=gate;signal.value=1;
 check(Indium::DynamicVK::vkSignalSemaphore(device->device(),&signal));buffer->waitUntilCompleted();
 bool drained;{std::unique_lock lock(mutex);drained=condition.wait_for(lock,std::chrono::seconds(5),[&]{return scheduled&&completed;});}
 std::atomic<int> reentrantScheduled{0},reentrantCompleted{0};auto reentrant=device->newCommandQueue()->commandBuffer();
 reentrant->addScheduledHandler([&](auto cb){cb->waitUntilCompleted();++reentrantScheduled;});
 reentrant->addCompletedHandler([&](auto){++reentrantCompleted;});reentrant->commit();reentrant->waitUntilScheduled();
 bool reentrantPass=reentrantScheduled==1&&reentrantCompleted==1;
 stop=true;poller.join();check(Indium::DynamicVK::vkWaitForFences(device->device(),1,&fence,VK_TRUE,UINT64_MAX));Indium::DynamicVK::vkDestroyFence(device->device(),fence,nullptr);Indium::DynamicVK::vkDestroySemaphore(device->device(),gate,nullptr);
 printf("%s scheduled before GPU gate release; completed before release %s; handlers drained %s\n",before?"PASS":"FAIL",completedBefore?"yes":"no",drained?"yes":"no");printf("%s waiters held until scheduled handler returned; %s scheduling wait independent of GPU completion; callback counts %d/%d\n",blocked?"PASS":"FAIL",scheduledWaitOnly?"PASS":"FAIL",scheduledCount,completedCount);printf("%s scheduled handler may wait for GPU completion without blocking event poller\n",reentrantPass?"PASS":"FAIL");return completedBefore?7:before&&drained&&blocked&&scheduledWaitOnly&&scheduledCount==1&&completedCount==1&&reentrantPass?0:1;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}
