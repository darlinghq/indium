#pragma once

#include <indium/buffer.hpp>

#include <vulkan/vulkan.h>

namespace Indium {
	class PrivateDevice;

	class PrivateBuffer: public Buffer {
	private:
		std::shared_ptr<PrivateDevice> _privateDevice;
		size_t _length;
		StorageMode _storageMode;
		void* _mapped = nullptr;

	public:
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, size_t length, ResourceOptions options);
		PrivateBuffer(std::shared_ptr<PrivateDevice> device, const void* pointer, size_t length, ResourceOptions options);
		~PrivateBuffer();

		virtual std::shared_ptr<Device> device() override;

		virtual size_t length() const override;
		virtual void* contents() override;
		virtual void didModifyRange(Range<size_t> range) override;

		virtual uint64_t gpuAddress() override;

		INDIUM_PROPERTY(VkBuffer, b, B,uffer) = VK_NULL_HANDLE;
		INDIUM_PROPERTY(VkDeviceMemory, m, M,emory) = VK_NULL_HANDLE;
	};
};
