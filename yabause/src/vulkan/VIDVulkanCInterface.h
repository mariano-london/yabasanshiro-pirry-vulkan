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

#ifndef VIDVULKAN_H
#define VIDVULKAN_H

#include "vdp1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDCORE_VULKAN   4

  /* Text menu (Vulkan build): one menu row; drawn by VIDVulkanDrawMenu() while the game is paused. */
  typedef struct { const char * label; const char * value; int selectable; } VkMenuRow;
  /* Draws one menu frame on a black screen and presents it (main thread, game paused). */
  void VIDVulkanDrawMenu(const char * title, const VkMenuRow * rows, int count, int selected, const char * footer);
  extern VideoInterface_struct CVIDVulkan;

  /* Creates the Renderer, opens the Vulkan Window (which creates the surface
     via Window_sdl.cpp) and hands it to VIDVulkan. Exposed as plain C so the
     front end never includes the Vulkan C++ headers: vulkan/Window.h defines
     a global class Window that collides with nanogui's, and glm's templates
     cannot live inside the front end's extern "C" block. */
  void VIDVulkanSetupRenderer(int width, int height);

  /* Presents the Vulkan swapchain. The reference front end does this inside
     its own YuiSwapBuffers (vulkan/yui.cpp:174); retro_arena's YuiSwapBuffers
     calls SDL_GL_SwapWindow instead, which is a no-op with no GL context, so
     nothing was ever presented and the screen stayed black while the GPU was
     visibly busy. */
  void VIDVulkanPresent(void);

  void VIDVulkanVdp2DrawStart(void);
  void VIDVulkanVdp2DrawEnd(void);
  void VIDVulkanVdp2DrawScreens(void);

#ifdef __cplusplus
}
#endif

#endif