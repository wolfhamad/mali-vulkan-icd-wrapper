# CMake additions to build the emulation helper library and compile the compute shader

cmake_minimum_required(VERSION 3.5)
project(mali_vulkan_icd_wrapper_emulation)

# Find Vulkan headers/libraries using the parent project's find_package(Vulkan) or expect Vulkan include paths
find_package(Vulkan REQUIRED)

set(EMU_SRCS
    fillmode_emulation.cpp
    integration.cpp
    ring_allocator.cpp
)

add_library(mv_icd_emulation STATIC ${EMU_SRCS})
target_include_directories(mv_icd_emulation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mv_icd_emulation PUBLIC Vulkan::Vulkan)

# Compile shader via glslangValidator if available
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/expand_triangles_to_lines.comp)
    set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/expand_triangles_to_lines.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_SRC} -o ${SHADER_OUT}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling expand_triangles_to_lines.comp to SPIR-V"
        VERBATIM
    )
    add_custom_target(emu_shaders ALL DEPENDS ${SHADER_OUT})
else()
    message(WARNING "glslangValidator not found; shader SPIR-V won't be built automatically. Build shaders manually and place them in the expected location.")
endif()
