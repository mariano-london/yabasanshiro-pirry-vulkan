/*  SDL2 Vulkan surface backend.

    Preferred over Window_display.cpp on devices like the RK3566 handhelds
    (Powkiddy RGB30 / dArkOS). Those have no X server and no Wayland
    compositor, so the upstream Window_xcb.cpp cannot work — but the front
    end already creates an SDL window, and on KMSDRM SDL takes DRM master and
    owns the KMS plane. A raw VK_KHR_display surface would then be fighting
    SDL for the same display.

    SDL2 solves this itself: its KMSDRM backend implements Vulkan surface
    creation (SDL_kmsdrmvulkan.c) on top of VK_KHR_display. Asking SDL for the
    surface keeps a single owner of the display and needs no changes to how
    the front end acquires it.

    Requires the window to have been created with SDL_WINDOW_VULKAN — see the
    YABA_VULKAN_ONLY block in retro_arena/main.cpp.
*/

#include "BUILD_OPTIONS.h"
#include "Platform.h"

#include "Window.h"
#include "Shared.h"
#include "Renderer.h"

#include <assert.h>
#include <iostream>

#if VK_USE_PLATFORM_SDL_KHR

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

/* Defined in retro_arena/main.cpp: the window the front end already owns. */
extern "C" SDL_Window *YabaGetSDLWindow(void);

void Window::_InitOSWindow()
{
  /* The front end created the window before the video core was initialised,
     so there is nothing to create here. Take its size as the surface size so
     Window.cpp builds the swapchain at the right extent. */
  SDL_Window *w = YabaGetSDLWindow();
  if (w == nullptr) {
    std::cerr << "Vulkan/SDL: no SDL window available.\n";
    assert(0 && "Vulkan/SDL: SDL window is null");
    return;
  }

  int dw = 0, dh = 0;
  SDL_Vulkan_GetDrawableSize(w, &dw, &dh);
  if (dw > 0 && dh > 0) {
    _surface_size_x = (uint32_t)dw;
    _surface_size_y = (uint32_t)dh;
  }
}

void Window::_DeInitOSWindow()
{
  /* The front end owns the window and destroys it itself. */
}

void Window::_UpdateOSWindow()
{
  /* The front end already pumps SDL events in its own loop; doing it here as
     well would steal input events from it. */
}

void Window::_InitOSSurface()
{
  SDL_Window *w = YabaGetSDLWindow();
  if (w == nullptr) {
    assert(0 && "Vulkan/SDL: SDL window is null");
    return;
  }

  if (SDL_Vulkan_CreateSurface(w, _renderer->GetVulkanInstance(), &_surface) != SDL_TRUE) {
    std::cerr << "Vulkan/SDL: SDL_Vulkan_CreateSurface failed: "
              << SDL_GetError() << "\n";
    assert(0 && "Vulkan/SDL: surface creation failed");
    return;
  }

  std::cerr << "Vulkan/SDL: surface " << _surface_size_x << "x"
            << _surface_size_y << "\n";
}

#endif
