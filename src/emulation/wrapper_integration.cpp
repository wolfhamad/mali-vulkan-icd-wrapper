#include "wrapper_integration.h"
#include "integration.h"
#include "ring_allocator.h"
#include <unordered_map>
#include <iostream>

namespace emu {

static std::unordered_map<VkPipeline, EmulationInfo> g_emulationMap;
static VkDevice g_device = VK_NULL_HANDLE;

VkResult WrapperOnDeviceCreated(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t computeQueueFamilyIndex, const char* shaderSpvPath) {
    g_device = device;
    return init(device, physicalDevice, computeQueueFamilyIndex, shaderSpvPath);
}

void WrapperOnDeviceDestroyed() {
    shutdown();
    g_emulationMap.clear();
    g_device = VK_NULL_HANDLE;
}

void WrapperAugmentPhysicalDeviceFeatures(VkPhysicalDeviceFeatures* features) {
    augmentPhysicalDeviceFeatures(features);
}

VkResult WrapperRegisterEmulatedPipeline(VkDevice device, VkPipelineCache pipelineCache, const VkGraphicsPipelineCreateInfo* origCreateInfo, VkPipeline appPipeline, VkPolygonMode requestedMode, EmulationInfo* outInfo) {
    if (!origCreateInfo || appPipeline == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;

    // Create an emulation pipeline variant
    VkPipeline emuPipeline = VK_NULL_HANDLE;
    VkResult res = createEmulationPipelineVariant(device, pipelineCache, origCreateInfo, &emuPipeline, requestedMode);
    if (res != VK_SUCCESS) return res;

    EmulationInfo info;
    info.appPipeline = appPipeline;
    info.emuPipeline = emuPipeline;
    info.requestedMode = requestedMode;

    g_emulationMap[appPipeline] = info;
    if (outInfo) *outInfo = info;
    return VK_SUCCESS;
}

void WrapperUnregisterEmulatedPipeline(VkDevice device, VkPipeline pipeline) {
    auto it = g_emulationMap.find(pipeline);
    if (it != g_emulationMap.end()) {
        if (it->second.emuPipeline != VK_NULL_HANDLE) {
            destroyEmulationPipelineVariant(device, it->second.emuPipeline);
        }
        g_emulationMap.erase(it);
    }
}

bool WrapperEmulateDrawIndexedIfNeeded(VkCommandBuffer cmd,
                                       VkPipeline boundPipeline,
                                       VkBuffer boundIndexBuffer,
                                       VkDeviceSize boundIndexBufferOffset,
                                       VkIndexType boundIndexType,
                                       uint32_t indexCount,
                                       uint32_t instanceCount,
                                       uint32_t firstIndex,
                                       int32_t vertexOffset,
                                       uint32_t firstInstance) {
    auto it = g_emulationMap.find(boundPipeline);
    if (it == g_emulationMap.end()) return false; // no emulation for this pipeline

    EmulationInfo info = it->second;

    // Basic validation
    if (indexCount == 0) return false;
    if (indexCount % 3u != 0u) {
        // not a triangle list; we don't handle other topologies here
        return false;
    }

    uint32_t triCount = indexCount / 3u;
    uint32_t outIndexCount = (info.requestedMode == VK_POLYGON_MODE_LINE) ? (triCount * 6u) : (triCount * 3u);
    VkDeviceSize dstSize = static_cast<VkDeviceSize>(outIndexCount) * sizeof(uint32_t);

    // Allocate dst buffer with STORAGE and INDEX usage
    SimpleBuffer dst;
    SimpleBufferManager* mgr = getBufferManager();
    if (!mgr) {
        std::cerr << "emu: no buffer manager available\n";
        return false;
    }
    VkResult r = mgr->createBuffer(dstSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &dst);
    if (r != VK_SUCCESS) {
        std::cerr << "emu: failed to allocate dst buffer (" << r << ")\n";
        return false;
    }

    // Create a temporary src buffer with STORAGE usage and copy src into it if needed
    // For simplicity, always do a GPU copy into a STORAGE buffer to avoid needing the original buffer to have STORAGE usage.
    // Create srcTemp buffer sized to indexCount * sizeof(indexType)
    VkDeviceSize srcBytes = (boundIndexType == VK_INDEX_TYPE_UINT16) ? (indexCount * sizeof(uint16_t)) : (indexCount * sizeof(uint32_t));
    SimpleBuffer srcTemp;
    r = mgr->createBuffer(srcBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &srcTemp);
    if (r != VK_SUCCESS) {
        std::cerr << "emu: failed to allocate src temp buffer (" << r << ")\n";
        return false;
    }

    // Copy from boundIndexBuffer into srcTemp
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = boundIndexBufferOffset + static_cast<VkDeviceSize>(firstIndex) * ((boundIndexType == VK_INDEX_TYPE_UINT16) ? sizeof(uint16_t) : sizeof(uint32_t));
    copyRegion.dstOffset = 0;
    copyRegion.size = srcBytes;
    vkCmdCopyBuffer(cmd, boundIndexBuffer, srcTemp.buffer, 1, &copyRegion);

    // Now run compute to expand indices from srcTemp -> dst
    uint32_t srcIndexOffsetElements = 0; // we already copied starting at firstIndex
    recordExpandTrianglesToLines(cmd, srcTemp.buffer, srcIndexOffsetElements, indexCount, boundIndexType, dst.buffer, 0);

    // Bind emulation pipeline and expanded index buffer and draw
    // Note: we bind the emulation pipeline directly (this changes pipeline state for the command buffer)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, info.emuPipeline);
    vkCmdBindIndexBuffer(cmd, dst.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, outIndexCount, instanceCount, 0, vertexOffset, firstInstance);

    // Restore: bind the original pipeline and index buffer so subsequent commands behave as the app expects
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipeline);
    vkCmdBindIndexBuffer(cmd, boundIndexBuffer, boundIndexBufferOffset, boundIndexType);

    // Note: dst and srcTemp buffers will be freed when the SimpleBufferManager is destroyed (on device shutdown);
    // for a production ring allocator, we'd reuse buffers and avoid continuous allocation.

    return true;
}

} // namespace emu
