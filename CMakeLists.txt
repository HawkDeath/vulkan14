cmake_minimum_required(VERSION 3.20)
project(vulkan14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/output")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/output/bin")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/output/lib")


set(SLANG_ENABLE_EXAMPLES OFF)
set(SLANG_ENABLE_TESTS OFF)

add_subdirectory(deps/slang)

find_package(Vulkan REQUIRED)
add_subdirectory(deps/glfw)
add_subdirectory(deps/glm)
set(VMA_BUILD_SAMPLES OFF)
add_subdirectory(deps/VMA)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/output/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/output/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/output/lib")

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE glm glfw Vulkan::Vulkan GL slang slang-rhi VulkanMemoryAllocator)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)
