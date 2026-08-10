#include "integration.h"
#include "fillmode_emulation.h"
#include "ring_allocator.h"
#include <cstring>
#include <vector>
#include <iostream>
#include <cstdint>

namespace emu {

static FillModeEmulation* g_emulator = nullptr;
static VkDevice g_device = VK_NULL_HANDLE;
static SimpleBufferManager* g_bufferManager = nullptr;

VkResult init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t computeQueueFamilyIndex, const char* shaderSpvPath) {
    if (g_emulator) return VK_SUCCESS; // already initialized
    g_device = device;

    // If caller passed UINT32_MAX, auto-detect a compute-capable queue family on the physical device.
    uint32_t chosenQueueFamily = computeQueueFamilyIndex;
    if (computeQueueFamilyIndex == UINT32_MAX) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qcount, nullptr);
        if (qcount > 0) {
            std::vector<VkQueueFamilyProperties> qprops(qcount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qcount, qprops.data());

            bool found = false;
            // prefer compute-only queue (compute but not graphics)
            for (uint32_t i = 0; i < qcount; ++i) {
                if ((qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    chosenQueueFamily = i;
                    found = true;
                    break;
                }
            }
            // otherwise pick any compute-capable queue
            if (!found) {
                for (uint32_t i = 0; i < qcount; ++i) {
                    if (qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                        chosenQueueFamily = i;
                        found = true;
                        break;
                    }
                }
            }
            // fallback: pick graphics-capable queue
            if (!found) {
                for (uint32_t i = 0; i < qcount; ++i) {
                    if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                        chosenQueueFamily = i;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) chosenQueueFamily = 0;
        } else {
            chosenQueueFamily = 0;
        }

        std::cout << "emu: selected queue family index " << chosenQueueFamily << " for compute-based fillModeNonSolid emulation\n";
    }

    g_emulator = new FillModeEmulation(device, physicalDevice, chosenQueueFamily);
    VkResult res = g_emulator->init(shaderSpvPath);
    if (res != VK_SUCCESS) {
        delete g_emulator;
        g_emulator = nullptr;
        return res;
    }

    // create a simple buffer manager for temporary expanded index buffers
    g_bufferManager = new SimpleBufferManager(device, physicalDevice);

    return VK_SUCCESS;
}

void shutdown() {
    if (g_emulator) {
        delete g_emulator;
        g_emulator = nullptr;
    }
    if (g_bufferManager) {
        delete g_bufferManager;
        g_bufferManager = nullptr;
    }
    g_device = VK_NULL_HANDLE;
}

void augmentPhysicalDeviceFeatures(VkPhysicalDeviceFeatures* features) {
    if (!features) return;
    // only set if we have an emulation backend
    if (g_emulator) {
        features->fillModeNonSolid = VK_TRUE;
    }
}

bool shouldEmulateRasterizationState(const VkPipelineRasterizationStateCreateInfo* rasterizationState) {
    if (!rasterizationState) return false;
    return rasterizationState->polygonMode != VK_POLYGON_MODE_FILL;
}

VkResult createEmulationPipelineVariant(VkDevice device,
                                        VkPipelineCache pipelineCache,
                                        const VkGraphicsPipelineCreateInfo* origCreateInfo,
                                        VkPipeline* outEmuPipeline,
                                        VkPolygonMode requestedMode) {
    if (!origCreateInfo || !outEmuPipeline) return VK_ERROR_INITIALIZATION_FAILED;

    // Make a shallow copy of the create info and modify rasterization & input assembly as needed.
    VkGraphicsPipelineCreateInfo copy = *origCreateInfo;

    // copy all pointed-to state structures shallowly (we will deep-copy rasterization and inputAssembly to modify)
    VkPipelineRasterizationStateCreateInfo rasterCopy{};
    if (origCreateInfo->pRasterizationState) {
        rasterCopy = *origCreateInfo->pRasterizationState;
        rasterCopy.pNext = nullptr;
        rasterCopy.polygonMode = VK_POLYGON_MODE_FILL; // driver must accept FILL
    }

    VkPipelineInputAssemblyStateCreateInfo iaCopy{};
    if (origCreateInfo->pInputAssemblyState) {
        iaCopy = *origCreateInfo->pInputAssemblyState;
        iaCopy.topology = (requestedMode == VK_POLYGON_MODE_LINE) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        iaCopy.pNext = nullptr;
    }

    // Assign our copied pointers into the pipeline createinfo copy
    copy.pRasterizationState = (origCreateInfo->pRasterizationState) ? &rasterCopy : nullptr;
    copy.pInputAssemblyState = (origCreateInfo->pInputAssemblyState) ? &iaCopy : nullptr;

    // Note: many pointers inside origCreateInfo point to memory that must outlive this call. We copied small structs locally
    // but more robust code should deep-copy all referenced state (viewport, multisample, depth-stencil, etc.). For now we
    // rely on the original pointers for unchanged structures.

    VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &copy, nullptr, outEmuPipeline);
    if (res != VK_SUCCESS) {
        std::cerr << "emu: createEmulationPipelineVariant: vkCreateGraphicsPipelines failed with " << res << "\n";
    }
    return res;
}

void destroyEmulationPipelineVariant(VkDevice device, VkPipeline emuPipeline) {
    if (device != VK_NULL_HANDLE && emuPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, emuPipeline, nullptr);
    }
}

void recordExpandTrianglesToLines(VkCommandBuffer cmd,
                                  VkBuffer srcIndexBuffer,
                                  VkDeviceSize srcIndexOffsetInIndices,
                                  uint32_t indexCount,
                                  VkIndexType indexType,
                                  VkBuffer dstBuffer,
                                  VkDeviceSize dstOffsetBytes) {
    if (!g_emulator) {
        std::cerr << "emu: recordExpandTrianglesToLines called but emulator not initialized\n";
        return;
    }
    g_emulator->recordExpandTrianglesToLines(cmd, srcIndexBuffer, srcIndexOffsetInIndices, indexCount, indexType, dstBuffer, dstOffsetBytes);
}

SimpleBufferManager* getBufferManager() {
    return g_bufferManager;
}

} // namespace emu
