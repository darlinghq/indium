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
 std::atomic<bool> stop{false};std::mutex mutex;std::condition_variable condition;bool scheduled=false,completed=false;
 auto buffer=device->newCommandQueue()->commandBuffer();
 buffer->addScheduledHandler([&](auto){std::lock_guard lock(mutex);scheduled=true;condition.notify_all();});
 buffer->addCompletedHandler([&](auto){std::lock_guard lock(mutex);completed=true;condition.notify_all();});
 std::thread poller([&]{while(!stop)device->pollEvents(1000000);});buffer->commit();
 bool before,completedBefore;{std::unique_lock lock(mutex);before=condition.wait_for(lock,std::chrono::seconds(1),[&]{return scheduled;});completedBefore=completed;if(completed)before=false;}
 VkSemaphoreSignalInfo signal{};signal.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;signal.semaphore=gate;signal.value=1;
 check(Indium::DynamicVK::vkSignalSemaphore(device->device(),&signal));buffer->waitUntilCompleted();
 bool drained;{std::unique_lock lock(mutex);drained=condition.wait_for(lock,std::chrono::seconds(5),[&]{return scheduled&&completed;});}
 stop=true;poller.join();check(Indium::DynamicVK::vkWaitForFences(device->device(),1,&fence,VK_TRUE,UINT64_MAX));Indium::DynamicVK::vkDestroyFence(device->device(),fence,nullptr);Indium::DynamicVK::vkDestroySemaphore(device->device(),gate,nullptr);
 printf("%s scheduled before GPU gate release; completed before release %s; handlers drained %s\n",before?"PASS":"FAIL",completedBefore?"yes":"no",drained?"yes":"no");return completedBefore?7:before&&drained?0:1;
}catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}
