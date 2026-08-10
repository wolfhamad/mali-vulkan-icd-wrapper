#pragma once

#include <vulkan/vulkan.h>

// Integration helpers for fillModeNonSolid emulation.
// These functions are intended to be called from your wrapper's interception points.
// Typical usage:
//  - At wrapper/device init: emu::init(device, physicalDevice, computeQueueFamilyIndex, "path/to/expand_triangles_to_lines.spv");
//  - In vkGetPhysicalDeviceFeatures / vkGetPhysicalDeviceFeatures2 interception: emu::augmentPhysicalDeviceFeatures(&features);
//  - In vkCreateGraphicsPipelines interception: if (emu::shouldEmulateRasterizationState(rasterizationState))
//       call emu::createEmulationPipelineVariant(device, pipelineCache, origCreateInfo, &emuPipeline);
//  - In vkCmdDrawIndexed interception: when bound pipeline requires emulation, call
//       emu::recordExpandTrianglesAndDraw(...)

namespace emu {

// Initialize emulation module. shaderSpvPath points to expand_triangles_to_lines.spv created from expand_triangles_to_lines.comp.
// computeQueueFamilyIndex is used for selecting queue family for compute operations (0 if unknown).
VkResult init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t computeQueueFamilyIndex, const char* shaderSpvPath);

// Tear down resources
void shutdown();

// Augment device features to report fillModeNonSolid support to applications (set the flag to VK_TRUE).
void augmentPhysicalDeviceFeatures(VkPhysicalDeviceFeatures* features);

// Inspect a rasterization state and return true if emulation is recommended (i.e., polygonMode != FILL)
bool shouldEmulateRasterizationState(const VkPipelineRasterizationStateCreateInfo* rasterizationState);

// Create an emulation pipeline variant derived from the original pipeline create info.
// This will attempt to create a pipeline that renders as line-list (or point-list) suitable for drawing expanded indices.
// The caller is responsible for supplying a valid pipeline cache (or VK_NULL_HANDLE).
VkResult createEmulationPipelineVariant(VkDevice device,
                                        VkPipelineCache pipelineCache,
                                        const VkGraphicsPipelineCreateInfo* origCreateInfo,
                                        VkPipeline* outEmuPipeline,
                                        VkPolygonMode requestedMode);

// Destroy an emulation pipeline created with createEmulationPipelineVariant
void destroyEmulationPipelineVariant(VkDevice device, VkPipeline emuPipeline);

// Helper to record compute dispatch that expands triangle indices from srcIndexBuffer into dstBuffer and performs the barrier.
// After calling this, bind dstBuffer as INDEX_BUFFER and issue vkCmdDrawIndexed with indexCount = (indexCount/3) * (requestedMode==VK_POLYGON_MODE_LINE ? 6 : 3)
void recordExpandTrianglesToLines(VkCommandBuffer cmd,
                                  VkBuffer srcIndexBuffer,
                                  VkDeviceSize srcIndexOffsetInIndices,
                                  uint32_t indexCount,
                                  VkIndexType indexType,
                                  VkBuffer dstBuffer,
                                  VkDeviceSize dstOffsetBytes);

} // namespace emu
