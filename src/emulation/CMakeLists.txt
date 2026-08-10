# Minimal CMake helper for building the compute shader

# This small CMakeLists demonstrates building the SPIR-V for expand_triangles_to_lines.comp at build time.
# Integration: include or call this CMake from your top-level CMakeLists.txt or copy the command into your build steps.

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
    add_custom_target(compile_fillmode_shaders ALL DEPENDS ${SHADER_OUT})
else()
    message(WARNING "glslangValidator not found; shader SPIR-V won't be built automatically. Build shaders manually and place them in the expected location.")
endif()
