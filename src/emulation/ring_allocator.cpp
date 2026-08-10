#include "ring_allocator.h"
#include <stdexcept>
#include <cstring>

SimpleBufferManager::SimpleBufferManager(VkDevice device_, VkPhysicalDevice physicalDevice_)
    : device(device_), physicalDevice(physicalDevice_) {}

SimpleBufferManager::~SimpleBufferManager() {
    cleanup();
}

uint32_t SimpleBufferManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    // fallback: pick any matching the typeFilter
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if (typeFilter & (1u << i)) return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

VkResult SimpleBufferManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, SimpleBuffer* out) {
    if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || !out) return VK_ERROR_INITIALIZATION_FAILED;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    VkResult res = vkCreateBuffer(device, &bci, nullptr, &buffer);
    if (res != VK_SUCCESS) return res;

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, buffer, &memReq);

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memReq.size;
    uint32_t memTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);
    mai.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory memory;
    res = vkAllocateMemory(device, &mai, nullptr, &memory);
    if (res != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        return res;
    }

    res = vkBindBufferMemory(device, buffer, memory, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        return res;
    }

    SimpleBuffer sb;
    sb.buffer = buffer;
    sb.memory = memory;
    sb.size = size;
    allocations.push_back(sb);

    *out = sb;
    return VK_SUCCESS;
}

void SimpleBufferManager::cleanup() {
    for (auto &a : allocations) {
        if (a.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, a.buffer, nullptr);
        if (a.memory != VK_NULL_HANDLE) vkFreeMemory(device, a.memory, nullptr);
    }
    allocations.clear();
}
