#include "fillmode_emulation.h"
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <iostream>

VkResult FillModeEmulation::createShaderModuleFromFile(const char* path, VkShaderModule* outModule) {
    auto code = readFile(path);
    if (code.empty()) return VK_ERROR_INITIALIZATION_FAILED;

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = code.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    return vkCreateShaderModule(device, &smci, nullptr, outModule);
}

std::vector<char> FillModeEmulation::readFile(const char* filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

FillModeEmulation::FillModeEmulation(VkDevice device_, VkPhysicalDevice phys, uint32_t computeQueueFamilyIndex)
    : device(device_), physicalDevice(phys), queueFamilyIndex(computeQueueFamilyIndex) {}

FillModeEmulation::~FillModeEmulation() {
    if (computePipeline) vkDestroyPipeline(device, computePipeline, nullptr);
    if (computePipelineLayout) vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (computeShaderModule) vkDestroyShaderModule(device, computeShaderModule, nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
}

VkResult FillModeEmulation::init(const char* shaderSpvPath) {
    VkResult res = createShaderModuleFromFile(shaderSpvPath, &computeShaderModule);
    if (res != VK_SUCCESS) return res;

    // descriptor set layout: binding 0 = src indices (readonly SSBO), binding 1 = dst indices (writeonly SSBO)
    VkDescriptorSetLayoutBinding srcBinding{};
    srcBinding.binding = 0;
    srcBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    srcBinding.descriptorCount = 1;
    srcBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    srcBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding dstBinding{};
    dstBinding.binding = 1;
    dstBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dstBinding.descriptorCount = 1;
    dstBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    dstBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding bindings[2] = { srcBinding, dstBinding };
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = bindings;

    res = vkCreateDescriptorSetLayout(device, &dslci, nullptr, &descriptorSetLayout);
    if (res != VK_SUCCESS) return res;

    // pipeline layout with push constants (3 uints)
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(uint32_t) * 3; // srcIndexOffset, srcIndexCount, triangleCount

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcRange;

    res = vkCreatePipelineLayout(device, &plci, nullptr, &computePipelineLayout);
    if (res != VK_SUCCESS) return res;

    // compute pipeline
    VkPipelineShaderStageCreateInfo pssci{};
    pssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pssci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pssci.module = computeShaderModule;
    pssci.pName = "main";

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = pssci;
    cpci.layout = computePipelineLayout;

    res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &computePipeline);
    if (res != VK_SUCCESS) return res;

    // descriptor pool
    VkDescriptorPoolSize poolSizes[1];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 16; // allow several allocations

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 8;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = poolSizes;

    res = vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool);
    if (res != VK_SUCCESS) return res;

    return VK_SUCCESS;
}

VkDescriptorSet FillModeEmulation::allocateAndWriteDescriptorSet(VkBuffer srcBuffer, VkDeviceSize srcOffsetBytes, VkBuffer dstBuffer, VkDeviceSize dstOffsetBytes) {
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet ds;
    VkResult res = vkAllocateDescriptorSets(device, &dsai, &ds);
    if (res != VK_SUCCESS) {
        // try to reset pool and allocate again (simple recovery)
        vkResetDescriptorPool(device, descriptorPool, 0);
        res = vkAllocateDescriptorSets(device, &dsai, &ds);
        if (res != VK_SUCCESS) return VK_NULL_HANDLE;
    }

    VkDescriptorBufferInfo srcBufInfo{};
    srcBufInfo.buffer = srcBuffer;
    srcBufInfo.offset = srcOffsetBytes;
    srcBufInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo dstBufInfo{};
    dstBufInfo.buffer = dstBuffer;
    dstBufInfo.offset = dstOffsetBytes;
    dstBufInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &srcBufInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &dstBufInfo;

    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    return ds;
}

void FillModeEmulation::recordExpandTrianglesToLines(VkCommandBuffer cmd,
                                                      VkBuffer srcIndexBuffer,
                                                      VkDeviceSize srcIndexOffsetInIndices,
                                                      uint32_t indexCount,
                                                      VkIndexType indexType,
                                                      VkBuffer dstBuffer,
                                                      VkDeviceSize dstOffsetBytes) {
    if (indexType != VK_INDEX_TYPE_UINT32) {
        // initially only support 32-bit indices
        // A production implementation should either handle 16-bit by reading into uint32 or copy/convert beforehand.
        std::cerr << "FillModeEmulation: only VK_INDEX_TYPE_UINT32 is supported in this build.\n";
        return;
    }

    if (indexCount % 3u != 0u) {
        std::cerr << "FillModeEmulation: indexCount is not a multiple of 3 (triangles).\n";
        return;
    }

    uint32_t triangleCount = indexCount / 3u;
    if (triangleCount == 0) return;

    // Allocate and write descriptor set that points to src/dst
    VkDescriptorSet ds = allocateAndWriteDescriptorSet(srcIndexBuffer, srcIndexOffsetInIndices * sizeof(uint32_t), dstBuffer, dstOffsetBytes);
    if (ds == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &ds, 0, nullptr);

    struct Push { uint32_t srcIndexOffset; uint32_t srcIndexCount; uint32_t triangleCount; } pc;
    pc.srcIndexOffset = static_cast<uint32_t>(srcIndexOffsetInIndices);
    pc.srcIndexCount = indexCount;
    pc.triangleCount = triangleCount;

    vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    uint32_t groups = (triangleCount + 255u) / 256u;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Barrier to make compute shader writes available to index reads in the vertex input stage
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0,
                         1, &memBarrier,
                         0, nullptr,
                         0, nullptr);

    // Note: caller should bind the dstBuffer as INDEX_BUFFER and issue vkCmdDrawIndexed with indexCount = triangleCount * 6
}
