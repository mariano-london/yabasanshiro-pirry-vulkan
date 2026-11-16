/* -----------------------------------------------------
This source code is public domain ( CC0 )
The code is provided as-is without limitations, requirements and responsibilities.
Creators and contributors to this source code are provided as a token of appreciation
and no one associated with this source code can be held responsible for any possible
damages or losses of any kind.

Original file creator:  Teagan Chouinard (GLFW)
Contributors:
Niko Kauppi (Code maintenance)
----------------------------------------------------- */
/*
        Copyright 2021 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "Platform.h"

#include <assert.h>

#if defined(__ANDROID__)

#include <vulkan/vulkan_android.h>
#include "VulkanTools.h"

void InitPlatform()
{
}

void DeInitPlatform()
{
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions)
{
  LOGI("%s", "VK_KHR_ANDROID_SURFACE_EXTENSION_NAME");
  instance_extensions->push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
}

#endif


#if USE_FRAMEWORK_GLFW

void InitPlatform()
{
  glfwInit();
  if (glfwVulkanSupported() == GLFW_FALSE) {
    assert(0 && " GLFW Failed to initialize with Vulkan.");
    return;
  }
}

void DeInitPlatform()
{
  glfwTerminate();
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions) {
  uint32_t instance_extension_count = 0;
  const char ** instance_extensions_buffer = glfwGetRequiredInstanceExtensions(&instance_extension_count);
  for (uint32_t i = 0; i < instance_extension_count; i++) {
    instance_extensions->push_back(instance_extensions_buffer[i]);
  }
}

#elif VK_USE_PLATFORM_WIN32_KHR

void InitPlatform()
{
}

void DeInitPlatform()
{
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions)
{
  instance_extensions->push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
}

#elif VK_USE_PLATFORM_XCB_KHR

void InitPlatform()
{
}

void DeInitPlatform()
{
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions)
{
  instance_extensions->push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
}

#elif VK_USE_PLATFORM_SDL_KHR

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <iostream>

extern "C" SDL_Window *YabaGetSDLWindow(void);

void InitPlatform()
{
  /* SDL_INIT_VIDEO is already done by the front end before the video core is
     created, so there is nothing to initialise. The loader does need to be
     pulled in before any vk* call, though. */
  if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
    std::cerr << "SDL_Vulkan_LoadLibrary failed: " << SDL_GetError() << "\n";
  }
}

void DeInitPlatform()
{
  SDL_Vulkan_UnloadLibrary();
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions)
{
  /* Let SDL name the extensions its own backend needs: on KMSDRM that is
     VK_KHR_surface + VK_KHR_display, which is exactly what the Mali driver
     exports, but hardcoding them would break on any other backend. */
  SDL_Window *w = YabaGetSDLWindow();
  unsigned int count = 0;
  if (w == nullptr || SDL_Vulkan_GetInstanceExtensions(w, &count, nullptr) != SDL_TRUE) {
    std::cerr << "SDL_Vulkan_GetInstanceExtensions failed: " << SDL_GetError() << "\n";
    return;
  }
  std::vector<const char *> names(count);
  if (SDL_Vulkan_GetInstanceExtensions(w, &count, names.data()) != SDL_TRUE) {
    std::cerr << "SDL_Vulkan_GetInstanceExtensions(2) failed: " << SDL_GetError() << "\n";
    return;
  }
  for (unsigned int i = 0; i < count; i++) {
    std::cerr << "SDL requires instance extension: " << names[i] << "\n";
    instance_extensions->push_back(names[i]);
  }
}

#elif VK_USE_PLATFORM_DISPLAY_KHR

void InitPlatform()
{
}

void DeInitPlatform()
{
}

void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions)
{
  /* VK_KHR_display needs the display-properties entry points, which live in
     VK_KHR_display itself, and that in turn requires
     VK_KHR_get_display_properties2 on some drivers. Only the base extension
     is mandatory, so request just that and let surface creation fail loudly
     if the driver lacks it. */
  instance_extensions->push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
}

#endif
