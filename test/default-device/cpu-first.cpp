#include <indium/indium.hpp>
#include <indium/device.private.hpp>
#include <indium/dynamic-vk.hpp>
#include <algorithm>
#include <cstdio>
static PFN_vkEnumeratePhysicalDevices original;
static VkResult enumerate(VkInstance instance, uint32_t *count, VkPhysicalDevice *devices) {
    VkResult result = original(instance, count, devices);
    if (devices && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {
        std::stable_partition(devices, devices + *count, [](VkPhysicalDevice device) {
            VkPhysicalDeviceProperties properties{};
            Indium::DynamicVK::vkGetPhysicalDeviceProperties(device, &properties);
            return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        });
    }
    return result;
}
int main() {
    Indium::init(nullptr, 0, false);
    bool cpu = false, gpu = false;
    for (auto device : Indium::globalDeviceList) {
        VkPhysicalDeviceProperties properties{};
        Indium::DynamicVK::vkGetPhysicalDeviceProperties(device->physicalDevice(), &properties);
        printf("available: %s type=%u\n", properties.deviceName, properties.deviceType);
        cpu |= properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        gpu |= properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }
    if (!cpu || !gpu) { puts("SKIP requires CPU and hardware GPU drivers"); Indium::finit(); return 2; }
    Indium::finitGlobalDeviceList();
    original = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(Indium::DynamicVK::vkEnumeratePhysicalDevices.pointer);
    Indium::DynamicVK::vkEnumeratePhysicalDevices.pointer = reinterpret_cast<void*>(enumerate);
    Indium::initGlobalDeviceList();
    Indium::DynamicVK::vkEnumeratePhysicalDevices.pointer = reinterpret_cast<void*>(original);
    auto device = Indium::createSystemDefaultDevice();
    VkPhysicalDeviceProperties properties{};
    if (device) Indium::DynamicVK::vkGetPhysicalDeviceProperties(std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice(), &properties);
    bool pass = device && (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
    printf("%s CPU-first enumeration default: %s\n", pass ? "PASS" : "FAIL", properties.deviceName);
    device.reset();
    Indium::finit();
    return pass ? 0 : 1;
}
