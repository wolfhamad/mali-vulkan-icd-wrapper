#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// Minimal encapsulation for GPU-based index expansion (triangles -> lines or points).
// This class provides:
//  - creation of the compute pipeline from a SPIR-V file
//  - allocation/management helpers for descriptor set layouts and pools
//  - a helper to record a compute dispatch + memory barrier that expands triangle indices
//
// Integration: the wrapper should call createComputePipeline() after device creation and provide
// a device-local destination buffer for expanded indices. The wrapper is responsible for allocating
// buffers sized appropriately and with the right usage flags.

class FillModeEmulation {
public:
    FillModeEmulation(VkDevice device, VkPhysicalDevice phys, uint32_t computeQueueFamilyIndex);
    ~FillModeEmulation();

    // Initialize resources and create compute pipeline. shaderSpvPath is path to expand_triangles_to_lines.spv
    VkResult init(const char* shaderSpvPath);

    // Record commands into `cmd` to expand `indexCount` indices (must be multiple of 3) from `srcIndexBuffer` (at srcIndexOffset indices)
    // into `dstBuffer` at dstOffset bytes. dstBuffer must have been created with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT.
    // Only supports VK_INDEX_TYPE_UINT32 in this initial implementation.
    void recordExpandTrianglesToLines(VkCommandBuffer cmd,
                                      VkBuffer srcIndexBuffer,
                                      VkDeviceSize srcIndexOffsetInIndices,
                                      uint32_t indexCount,
                                      VkIndexType indexType,
                                      VkBuffer dstBuffer,
                                      VkDeviceSize dstOffsetBytes);

    // Accessors
    VkPipelineLayout getPipelineLayout() const { return computePipelineLayout; }
    VkPipeline getComputePipeline() const { return computePipeline; }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;

    VkShaderModule computeShaderModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // helper to create shader module from SPIR-V on disk
    VkResult createShaderModuleFromFile(const char* path, VkShaderModule* outModule);

    // helper to allocate and write a descriptor set for src/dst buffers
    VkDescriptorSet allocateAndWriteDescriptorSet(VkBuffer srcBuffer, VkDeviceSize srcOffsetBytes, VkBuffer dstBuffer, VkDeviceSize dstOffsetBytes);

    // small utility
    static std::vector<char> readFile(const char* filename);
};
