#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Very small helper to create and track VkBuffer + VkDeviceMemory allocations used by the emulation module.
// This is not a high-performance ring allocator — it's a safe initial implementation that creates device-local
// buffers and records them for cleanup. Later it can be replaced with a real ring allocator.

struct SimpleBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

class SimpleBufferManager {
public:
    SimpleBufferManager(VkDevice device, VkPhysicalDevice physicalDevice);
    ~SimpleBufferManager();

    // Create a buffer with given size and usage and allocate device memory for it (device-local if possible)
    // The buffer will be tracked and destroyed on manager destruction.
    // Returns VK_SUCCESS on success, with outBuffer and outMemory set.
    VkResult createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, SimpleBuffer* out);

    // Destroy and free all tracked buffers/memory
    void cleanup();

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::vector<SimpleBuffer> allocations;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
