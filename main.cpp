#include <cstdint>
#include <format>
#include <iostream>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

int main() {
  glfwSetErrorCallback([](int code, const char *desc) -> void {
    std::cerr << "[GLFW] " << desc << "\n";
  });
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  GLFWwindow *window =
      glfwCreateWindow(1920, 1080, "vulkan14", nullptr, nullptr);

  if (window == nullptr) {
    glfwTerminate();
    return -1;
  }

  uint32_t vkApiVersion = 0u;
  vkEnumerateInstanceVersion(&vkApiVersion);

  std::cout << "VulkanAPI "
            << std::format("{}.{}.{}", VK_API_VERSION_MAJOR(vkApiVersion),
                           VK_API_VERSION_MINOR(vkApiVersion),
                           VK_API_VERSION_PATCH(vkApiVersion))
            << "\n";

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
  }

  if (window)
    glfwDestroyWindow(window);

  glfwTerminate();

  return 0;
}
