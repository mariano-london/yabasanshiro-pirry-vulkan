/*  VK_KHR_display window backend — direct-to-display, no window system.

    Written for RK3566 handhelds (Powkiddy RGB30 / dArkOS), which render
    straight to KMS/DRM: there is no X server and no Wayland compositor, and
    the Mali Vulkan driver (/opt/vulkan/libmali.so) exports only
    VK_KHR_display and VK_KHR_surface — no xcb_surface, no wayland_surface.
    The existing Window_xcb.cpp therefore cannot be used on these devices.

    Three of the four entry points are no-ops here: without a window system
    there is no window to create, destroy, or pump events for. Input is
    handled by SDL elsewhere in the port. All the work is in _InitOSSurface(),
    which picks a display, a mode and a compatible plane, then creates a
    display-plane surface.
*/

#include "BUILD_OPTIONS.h"
#include "Platform.h"

#include "Window.h"
#include "Shared.h"
#include "Renderer.h"

#include <assert.h>
#include <iostream>
#include <vector>

#if VK_USE_PLATFORM_DISPLAY_KHR

void Window::_InitOSWindow()
{
  /* No window system: nothing to create. The surface size is resolved in
     _InitOSSurface() from the display's own preferred mode, which is the
     panel's native resolution. */
}

void Window::_DeInitOSWindow()
{
}

void Window::_UpdateOSWindow()
{
  /* No window-system events exist on a direct-to-display surface. Input and
     quit handling come from SDL in the retro_arena front end. */
}

void Window::_InitOSSurface()
{
  const VkPhysicalDevice gpu = _renderer->GetVulkanPhysicalDevice();

  /* ---- 1. displays ---- */
  uint32_t display_count = 0;
  if (vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, nullptr) != VK_SUCCESS
      || display_count == 0) {
    std::cerr << "VK_KHR_display: no displays reported by the driver.\n";
    assert(0 && "VK_KHR_display: no displays");
    return;
  }
  std::vector<VkDisplayPropertiesKHR> displays(display_count);
  vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &display_count, displays.data());

  const VkDisplayKHR display = displays[0].display;

  /* ---- 2. modes: take the highest-resolution one ---- */
  uint32_t mode_count = 0;
  vkGetDisplayModePropertiesKHR(gpu, display, &mode_count, nullptr);
  if (mode_count == 0) {
    std::cerr << "VK_KHR_display: display 0 reports no modes.\n";
    assert(0 && "VK_KHR_display: no modes");
    return;
  }
  std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
  vkGetDisplayModePropertiesKHR(gpu, display, &mode_count, modes.data());

  uint32_t best = 0;
  for (uint32_t i = 1; i < mode_count; i++) {
    const VkExtent2D &a = modes[i].parameters.visibleRegion;
    const VkExtent2D &b = modes[best].parameters.visibleRegion;
    if ((uint64_t)a.width * a.height > (uint64_t)b.width * b.height) best = i;
  }
  const VkDisplayModePropertiesKHR &mode = modes[best];

  /* The panel decides the surface size here, unlike the windowed backends
     where the caller picks it. Keep Window's own fields in step so the
     swapchain in Window.cpp is built at the right extent. */
  _surface_size_x = mode.parameters.visibleRegion.width;
  _surface_size_y = mode.parameters.visibleRegion.height;

  /* ---- 3. a plane that can drive this display ---- */
  uint32_t plane_count = 0;
  vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, nullptr);
  if (plane_count == 0) {
    std::cerr << "VK_KHR_display: no display planes.\n";
    assert(0 && "VK_KHR_display: no planes");
    return;
  }
  std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
  vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &plane_count, planes.data());

  uint32_t plane_index = UINT32_MAX;
  uint32_t plane_stack = 0;
  for (uint32_t i = 0; i < plane_count; i++) {
    /* Skip planes already bound to a different display. */
    if (planes[i].currentDisplay != VK_NULL_HANDLE
        && planes[i].currentDisplay != display) continue;

    uint32_t supported = 0;
    vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &supported, nullptr);
    if (supported == 0) continue;
    std::vector<VkDisplayKHR> supported_displays(supported);
    vkGetDisplayPlaneSupportedDisplaysKHR(gpu, i, &supported, supported_displays.data());

    for (uint32_t j = 0; j < supported; j++) {
      if (supported_displays[j] == display) {
        plane_index = i;
        plane_stack = planes[i].currentStackIndex;
        break;
      }
    }
    if (plane_index != UINT32_MAX) break;
  }
  if (plane_index == UINT32_MAX) {
    std::cerr << "VK_KHR_display: no plane supports display 0.\n";
    assert(0 && "VK_KHR_display: no compatible plane");
    return;
  }

  /* ---- 4. the surface ---- */
  VkDisplaySurfaceCreateInfoKHR create_info{};
  create_info.sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
  create_info.displayMode     = mode.displayMode;
  create_info.planeIndex      = plane_index;
  create_info.planeStackIndex = plane_stack;
  create_info.transform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  create_info.globalAlpha     = 1.0f;
  create_info.alphaMode       = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
  create_info.imageExtent     = mode.parameters.visibleRegion;

  ErrorCheck(vkCreateDisplayPlaneSurfaceKHR(_renderer->GetVulkanInstance(),
                                            &create_info, nullptr, &_surface));

  std::cerr << "VK_KHR_display: surface " << _surface_size_x << "x"
            << _surface_size_y << " on plane " << plane_index << "\n";
}

#endif
