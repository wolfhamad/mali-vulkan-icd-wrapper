# Fill-mode non-solid emulation module

This directory contains a GPU compute-based implementation to expand triangle index lists into line (or point) index lists on-device. The goal is to provide a reusable module that a Vulkan wrapper/ICD may call from its pipeline/draw interception points to emulate VK_POLYGON_MODE_LINE and VK_POLYGON_MODE_POINT on hardware that lacks fillModeNonSolid.

Files
- expand_triangles_to_lines.comp - GLSL compute shader (source). Compile to SPIR-V with glslangValidator or shaderc (example below).
- fillmode_emulation.h/.cpp - C++ module that encapsulates creating the compute pipeline, descriptor layout, and recording a dispatch + barrier that expands indices.
- CMakeLists.txt - example helper to build SPIR-V at build time (optional).

Integration notes
1) Build the shader to SPIR-V (expand_triangles_to_lines.spv) and ensure the binary is installed next to the library or available via an expected path.
   Example:
     glslangValidator -V src/emulation/expand_triangles_to_lines.comp -o src/emulation/expand_triangles_to_lines.spv

2) In your wrapper interceptor:
   - Initialize FillModeEmulation with device, physical device, and a queue family index that supports compute.
   - At vkCreateGraphicsPipelines time, detect pipelines that requested non-FILL polygonModes and create an internal pipeline variant for LINE_LIST/POINT_LIST. Store mapping appPipeline -> EmulationInfo including a pointer to FillModeEmulation so draw-time code can call it.
   - At vkCmdDrawIndexed interception, when the bound pipeline requires emulation, call FillModeEmulation::recordExpandTrianglesToLines to expand indices into a device-local buffer, then bind the emulation pipeline and bind the expanded index buffer and issue vkCmdDrawIndexed with the expanded indexCount.

Limitations (initial implementation)
- This module's recording helper currently assumes VK_INDEX_TYPE_UINT32 for the source index buffer. 16-bit index support may be added later.
- This module does not allocate index buffers for you; it expects the caller to provide a destination VkBuffer with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT and sufficient size. The README includes guidance for allocation strategies.

Threading / command buffer notes
- The helper records commands into the provided VkCommandBuffer; it expects the command buffer to be in the recording state.
- The helper performs a pipeline barrier after compute dispatch to make writes visible to index reads.

