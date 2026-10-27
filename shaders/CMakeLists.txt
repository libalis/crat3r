cmake_minimum_required(VERSION 3.20)

file(GLOB_RECURSE SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.geom"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rgen"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rint"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rahit"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rchit"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rmiss"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.geom.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.comp.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rgen.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rint.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rahit.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rchit.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rmiss.glsl"
)

set(SHADER_SPVS "")

foreach(SHADER ${SHADER_SOURCES})
  set(OUTPUT_SPV "${SHADER}.spv")

  set(COMMAND_SPV
      ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
      -g
      -D__GLSL__
      --target-env "vulkan1.3"
      -o ${OUTPUT_SPV}
      ${SHADER}
  )

  add_custom_command(
    OUTPUT ${OUTPUT_SPV}
    COMMAND echo ${COMMAND_SPV}
    COMMAND ${COMMAND_SPV}
    MAIN_DEPENDENCY ${SHADER}
  )

  list(APPEND SHADER_SPVS ${OUTPUT_SPV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_SPVS})
