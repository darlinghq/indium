#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
static void check(VkResult r) {
    if (r != VK_SUCCESS)
        throw std::runtime_error("Vulkan operation failed");
}
struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceAddress address;
};
int main(int argc, char **argv) try {
    if (argc < 3)
        return 2;
    int expectedCount = argc - 2;
    float expected[4] = {};
    for (int i = 0; i < expectedCount && i < 4; ++i)
        expected[i] = std::strtof(argv[2 + i], nullptr);
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    if (!input)
        return 2;
    auto size = input.tellg();
    if (size <= 0 || size % 4)
        return 2;
    std::vector<uint32_t> code(size_t(size) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(code.data()), size);
    if (!input)
        return 2;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    VkInstance instance;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance));
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count)
        return 2;
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    auto physical = devices.front();
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::printf("GPU %s\n", properties.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
        ++family;
    if (family == count)
        return 2;
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.pNext = &features13;
    VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    available.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physical, &available);
    if (!available.features.shaderInt64 || !features12.bufferDeviceAddress || !features13.maintenance4)
        throw std::runtime_error("required physical addressing features unavailable");
    features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.maintenance4 = VK_TRUE;
    features12.pNext = &features13;
    VkPhysicalDeviceFeatures enabled{};
    enabled.shaderInt64 = VK_TRUE;
    float priority = 1;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &features12;
    deviceInfo.pEnabledFeatures = &enabled;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device;
    check(vkCreateDevice(physical, &deviceInfo, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    auto makeBuffer = [&](size_t bytes, VkBufferUsageFlags usage) {
        Buffer b{};
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = bytes;
        info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &b.buffer));
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, b.buffer, &requirements);
        uint32_t type = 0;
        while (type < memoryProperties.memoryTypeCount &&
               (!(requirements.memoryTypeBits & (1u << type)) ||
                (memoryProperties.memoryTypes[type].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
            ++type;
        if (type == memoryProperties.memoryTypeCount)
            throw std::runtime_error("coherent mapped memory unavailable");
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.pNext = &flags;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &allocation, nullptr, &b.memory));
        check(vkBindBufferMemory(device, b.buffer, b.memory, 0));
        check(vkMapMemory(device, b.memory, 0, bytes, 0, &b.mapped));
        VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        address.buffer = b.buffer;
        b.address = vkGetBufferDeviceAddress(device, &address);
        if (!b.address)
            throw std::runtime_error("buffer address unavailable");
        return b;
    };
    auto source = makeBuffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto output = makeBuffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto pointers = makeBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    float values[16];
    for (int i = 0; i < 16; ++i)
        values[i] = float(i);
    std::memcpy(source.mapped, values, sizeof(values));
    for (int i = 0; i < 4; ++i)
        static_cast<float *>(output.mapped)[i] = -999;
    uint64_t addresses[] = {source.address, output.address};
    std::memcpy(pointers.mapped, addresses, sizeof(addresses));
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VkDescriptorSetLayout layout;
    check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout));
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    VkPipelineLayout pipelineLayout;
    check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = pool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &layout;
    VkDescriptorSet set;
    check(vkAllocateDescriptorSets(device, &setInfo, &set));
    VkDescriptorBufferInfo bufferInfo{pointers.buffer, 0, 16};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderInfo.codeSize = code.size() * 4;
    shaderInfo.pCode = code.data();
    VkShaderModule shader;
    check(vkCreateShaderModule(device, &shaderInfo, nullptr, &shader));
    uint32_t sizes[] = {1, 1, 1};
    VkSpecializationMapEntry entries[] = {{0, 0, 4}, {1, 4, 4}, {2, 8, 4}};
    VkSpecializationInfo specialization{3, entries, sizeof(sizes), sizes};
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shader;
    pipelineInfo.stage.pName = "authoredKernel";
    pipelineInfo.stage.pSpecializationInfo = &specialization;
    VkPipeline pipeline;
    check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    VkCommandPoolCreateInfo commandPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPoolInfo.queueFamilyIndex = family;
    VkCommandPool commandPool;
    check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.commandBufferCount = 1;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkCommandBuffer command;
    check(vkAllocateCommandBuffers(device, &commandInfo, &command));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    check(vkBeginCommandBuffer(command, &begin));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(command, 1, 1, 1);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
    check(vkEndCommandBuffer(command));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    check(vkQueueWaitIdle(queue));
    bool pass = true;
    for (int i = 0; i < expectedCount; ++i) {
        float actual = static_cast<float *>(output.mapped)[i];
        pass = pass && actual == expected[i];
        std::printf("element %d actual %.0f expected %.0f\n", i, actual, expected[i]);
    }
    std::printf("%s readback\n", pass ? "PASS" : "FAIL");
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    for (auto b : {source, output, pointers}) {
        vkUnmapMemory(device, b.memory);
        vkDestroyBuffer(device, b.buffer, nullptr);
        vkFreeMemory(device, b.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return pass ? 0 : 1;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
