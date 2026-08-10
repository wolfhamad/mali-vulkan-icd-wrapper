#pragma once

#include <vulkan/vulkan.h>

// High-level helper API for integrating fillModeNonSolid emulation into your wrapper.
// These functions are intended to be called from the wrapper's existing Vulkan interception points.

namespace emu {

struct EmulationInfo {
    VkPipeline appPipeline;     // pipeline handle returned to the app (driver pipeline)
    VkPipeline emuPipeline;     // internal pipeline to use for emulated draws
    VkPolygonMode requestedMode; // VK_POLYGON_MODE_LINE or VK_POLYGON_MODE_POINT
};

// Initialize emulation for a device. Pass UINT32_MAX for computeQueueFamilyIndex to auto-select a compute-capable queue.
VkResult WrapperOnDeviceCreated(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t computeQueueFamilyIndex, const char* shaderSpvPath);

// Shutdown emulation for a device
void WrapperOnDeviceDestroyed();

// Called during vkGetPhysicalDeviceFeatures(2) interception to advertise fillModeNonSolid
void WrapperAugmentPhysicalDeviceFeatures(VkPhysicalDeviceFeatures* features);

// Create an emulation pipeline variant and register it for the app pipeline returned to the app.
// Call this after you created the driver's pipeline that you returned to the app.
VkResult WrapperRegisterEmulatedPipeline(VkDevice device, VkPipelineCache pipelineCache, const VkGraphicsPipelineCreateInfo* origCreateInfo, VkPipeline appPipeline, VkPolygonMode requestedMode, EmulationInfo* outInfo);

// Unregister/destroy emulation data for a pipeline (call from vkDestroyPipeline)
void WrapperUnregisterEmulatedPipeline(VkDevice device, VkPipeline pipeline);

// Attempt to emulate an indexed draw. If emulation was performed, returns true and the original draw should NOT be called.
// Parameters describe the current command-buffer state (bound pipeline, bound index buffer/type/offset). If emulation is not needed
// or fails, returns false and the caller should perform the original vkCmdDrawIndexed.
// Note: this helper issues additional vkCmd* calls (compute dispatch, pipeline barrier, vkCmdBindPipeline, vkCmdBindIndexBuffer, vkCmdDrawIndexed).
bool WrapperEmulateDrawIndexedIfNeeded(VkCommandBuffer cmd,
                                       VkPipeline boundPipeline,
                                       VkBuffer boundIndexBuffer,
                                       VkDeviceSize boundIndexBufferOffset, // in bytes
                                       VkIndexType boundIndexType,
                                       uint32_t indexCount,
                                       uint32_t instanceCount,
                                       uint32_t firstIndex,
                                       int32_t vertexOffset,
                                       uint32_t firstInstance);

// Simple helper to convert a bound index offset in bytes into an index offset in elements for UINT32.
static inline uint32_t BytesToIndexCount(VkDeviceSize bytes, VkIndexType t) {
    if (t == VK_INDEX_TYPE_UINT16) return static_cast<uint32_t>(bytes / sizeof(uint16_t));
    return static_cast<uint32_t>(bytes / sizeof(uint32_t));
}

} // namespace emu
