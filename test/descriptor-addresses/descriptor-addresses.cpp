#include <indium/indium.hpp>
#include <indium/command-encoder.private.hpp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
static void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan descriptor setup failed");
}
int main(int argc, char **argv) try {
    if (argc != 2) return 2;
    bool empty = std::strcmp(argv[1], "empty") == 0;
    bool dense = std::strcmp(argv[1], "dense") == 0;
    bool multi = std::strcmp(argv[1], "multi") == 0;
    if (!empty && !dense && !multi && std::strcmp(argv[1], "sparse") != 0) return 2;
    Indium::init(nullptr, 0, false);
    auto device = std::static_pointer_cast<Indium::PrivateDevice>(Indium::createSystemDefaultDevice());
    if (!device) return 2;
    VkPhysicalDeviceProperties properties{};
    Indium::DynamicVK::vkGetPhysicalDeviceProperties(device->physicalDevice(), &properties);
    printf("GPU %s\n", properties.deviceName);
    Indium::FunctionResources resources;
    uint64_t values[] = {123, 456};
    auto buffer = device->newBuffer(values, sizeof(values), Indium::ResourceOptions::StorageModeShared);
    resources.setBuffer(buffer, dense ? 0 : sizeof(uint64_t), dense ? 0 : 2);
    if (multi) resources.setBuffer(buffer, 0, 0);
    Indium::FunctionInfo function{};
    if (!empty) {
        Indium::BindingDescriptor binding{};
        binding.type = Indium::BindingType::Buffer;
        binding.index = dense ? 0 : 2;
        function.bindings.push_back(binding);
        if (multi) {
            binding.index = 0;
            function.bindings.push_back(binding);
        }
    }
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = empty ? 0 : 1;
    layoutInfo.pBindings = empty ? nullptr : &binding;
    std::array<VkDescriptorSetLayout, 1> layouts{};
    check(Indium::DynamicVK::vkCreateDescriptorSetLayout(device->device(), &layoutInfo, nullptr, &layouts[0]));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool{};
    check(Indium::DynamicVK::vkCreateDescriptorPool(device->device(), &poolInfo, nullptr, &pool));
    std::vector<std::shared_ptr<Indium::Buffer>> retained;
    Indium::createDescriptorSets<1>(layouts, pool, device, {std::cref(resources)}, {std::cref(function)}, retained);
    bool pass = empty ? retained.empty() : retained.size() == 1 && retained[0]->length() == (multi ? 2 : 1) * sizeof(uint64_t);
    if (!empty && pass) {
        uint64_t address;
        std::memcpy(&address, retained[0]->contents(), sizeof(address));
        pass = address == buffer->gpuAddress() + (dense ? 0 : sizeof(uint64_t));
        if (multi) {
            std::memcpy(&address, static_cast<char*>(retained[0]->contents()) + sizeof(uint64_t), sizeof(address));
            pass = pass && address == buffer->gpuAddress();
        }
    }
    printf("%s %s: retained=%zu table_bytes=%zu\n", pass ? "PASS" : "FAIL", argv[1], retained.size(), retained.empty() ? 0 : retained[0]->length());
    Indium::DynamicVK::vkDestroyDescriptorPool(device->device(), pool, nullptr);
    Indium::DynamicVK::vkDestroyDescriptorSetLayout(device->device(), layouts[0], nullptr);
    retained.clear(); resources.buffers.clear(); buffer.reset(); device.reset();
    Indium::finit();
    return pass ? 0 : 1;
} catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
