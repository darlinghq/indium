#include <indium/indium.hpp>
#include <indium/dynamic-vk.hpp>
#include <cstdio>
#include <indium/device.private.hpp>
#include <stdexcept>
static PFN_vkCreateImage original;
static VkImageUsageFlags observed;
static unsigned creations;
static VkResult create(VkDevice device,const VkImageCreateInfo *info,const VkAllocationCallbacks *allocation,VkImage *image) {
 ++creations;observed=info->usage;return original(device,info,allocation,image);
}
int main() {
 Indium::init(nullptr,0,false);auto device=Indium::createSystemDefaultDevice();if(!device)return 2;
 std::printf("device %s\n",device->name().c_str());
 if(!Indium::DynamicVK::vkCreateImage.resolve())return 3;
 original=reinterpret_cast<PFN_vkCreateImage>(Indium::DynamicVK::vkCreateImage.pointer);
 Indium::DynamicVK::vkCreateImage.pointer=reinterpret_cast<void*>(create);
 unsigned failures=0;
 struct Case { Indium::PixelFormat format;Indium::TextureUsage usage;VkImageUsageFlags expected;const char *name; };
 const VkImageUsageFlags transfer=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 for(auto test:{Case{Indium::PixelFormat::Depth32Float,Indium::TextureUsage::RenderTarget,transfer|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,"depth render target"},Case{Indium::PixelFormat::RGBA8Unorm,Indium::TextureUsage::ShaderRead,transfer|VK_IMAGE_USAGE_SAMPLED_BIT,"color shader read"},Case{Indium::PixelFormat::RGBA8Unorm,Indium::TextureUsage::ShaderWrite,transfer|VK_IMAGE_USAGE_STORAGE_BIT,"color shader write"},Case{Indium::PixelFormat::RGBA8Unorm,Indium::TextureUsage::ShaderRead|Indium::TextureUsage::RenderTarget,transfer|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,"combined read render"}}) {
  auto descriptor=Indium::TextureDescriptor::texture2DDescriptor(test.format,16,12,false);descriptor.usage=test.usage;descriptor.resourceOptions=Indium::ResourceOptions::StorageModePrivate;
  auto texture=device->newTexture(descriptor);bool pass=texture && observed==test.expected;
  std::printf("%s %s usage=%x expected=%x\n",pass?"PASS":"FAIL",test.name,observed,test.expected);failures+=!pass;
 }
 auto unknown=Indium::TextureDescriptor::texture2DDescriptor(Indium::PixelFormat::Depth32Float,16,12,false);unknown.resourceOptions=Indium::ResourceOptions::StorageModePrivate;unknown.usage=Indium::TextureUsage::Unknown;
 auto texture=device->newTexture(unknown);VkImageFormatProperties properties{};
 auto status=Indium::DynamicVK::vkGetPhysicalDeviceImageFormatProperties(std::static_pointer_cast<Indium::PrivateDevice>(device)->physicalDevice(),VK_FORMAT_D32_SFLOAT,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,observed,0,&properties);
 bool pass=texture && status==VK_SUCCESS;std::printf("%s unknown depth uses supported usage=%x\n",pass?"PASS":"FAIL",observed);failures+=!pass;
 for(auto usage:{static_cast<Indium::TextureUsage>(1u<<30),Indium::TextureUsage::ShaderWrite}) {
  unknown.usage=usage;unsigned before=creations;bool rejected=false;
  try {device->newTexture(unknown);}catch(const std::runtime_error&){rejected=creations==before;}
  std::printf("%s invalid depth usage rejected before allocation\n",rejected?"PASS":"FAIL");failures+=!rejected;
 }
 Indium::DynamicVK::vkCreateImage.pointer=reinterpret_cast<void*>(original);return failures?1:0;
}
