cmake_minimum_required(VERSION 3.20)

project(crat3r)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------

set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(deps/volk)

add_subdirectory(deps/vma)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glfw)

add_subdirectory(deps/glm)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(ASSIMP_NO_EXPORT ON CACHE BOOL "")
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "")
set(ASSIMP_INSTALL OFF CACHE BOOL "")
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "")
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "")
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "")
set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE BOOL "")
set(ASSIMP_BUILD_STL_IMPORTER ON CACHE BOOL "")
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "")
add_subdirectory(deps/assimp)

find_package(OpenMP REQUIRED)
include_directories(${OpenMP_CXX_INCLUDE_DIRS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")

set(MINIAUDIO_NO_NULL ON CACHE BOOL "")
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "")
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "")
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "")
add_subdirectory(deps/miniaudio)

# ----------------------------------------------------------

add_library(imgui STATIC
  "${CMAKE_SOURCE_DIR}/deps/imgui/imgui.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/imgui_demo.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/imgui_draw.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/imgui_tables.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/imgui_widgets.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/backends/imgui_impl_glfw.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/backends/imgui_impl_vulkan.cpp"
  "${CMAKE_SOURCE_DIR}/deps/imgui/backends/imgui_impl_null.cpp"
)

target_include_directories(imgui PUBLIC
  "${CMAKE_SOURCE_DIR}/deps/imgui/"
  "${CMAKE_SOURCE_DIR}/deps/imgui/backends"
)

target_link_libraries(imgui PUBLIC volk_headers glfw)

target_compile_definitions(imgui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)

# ----------------------------------------------------------

add_library(crat3r_core INTERFACE)

file(GLOB_RECURSE CORE_SOURCES "${CMAKE_SOURCE_DIR}/src/core/*.cpp")
target_sources(crat3r_core INTERFACE ${CORE_SOURCES})

target_include_directories(crat3r_core INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_include_directories(crat3r_core INTERFACE "${CMAKE_SOURCE_DIR}/deps/stb")

target_link_libraries(crat3r_core INTERFACE
  volk_headers
  VulkanMemoryAllocator
  glfw
  glm
  imgui
  assimp
  miniaudio
)

# ----------------------------------------------------------

function(add_assignment dir)
  if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/src/${dir}")
    add_executable(${dir})

    file(GLOB_RECURSE SOURCES "${CMAKE_SOURCE_DIR}/src/${dir}/*.cpp")
    target_sources(${dir} PRIVATE ${SOURCES})

    target_link_libraries(${dir} PRIVATE crat3r_core)

    target_compile_definitions(${dir} PRIVATE ROOT_DIR="${CMAKE_SOURCE_DIR}")
  endif()
endfunction()

add_assignment(a01)
add_assignment(a02)
add_assignment(a03)
add_assignment(a04)

# ----------------------------------------------------------

add_subdirectory(shaders)
