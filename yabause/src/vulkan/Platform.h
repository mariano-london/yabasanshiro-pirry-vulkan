/* -----------------------------------------------------
This source code is public domain ( CC0 )
The code is provided as-is without limitations, requirements and responsibilities.
Creators and contributors to this source code are provided as a token of appreciation
and no one associated with this source code can be held responsible for any possible
damages or losses of any kind.

Original file creator:  Niko Kauppi (Code maintenance)
Contributors:
Teagan Chouinard (GLFW)
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

#pragma once

// Contact me if you're interested about adding platform
// support in these tutorials, contact me on my youtube channel:
// https://www.youtube.com/user/Nigo40
// Biggest missing right now is support for Android.
// others like Xlib and so are also welcome additions.

#include "BUILD_OPTIONS.h"
#include <stdint.h>
#include <vector>

void InitPlatform();
void DeInitPlatform();
void AddRequiredPlatformInstanceExtensions(std::vector<const char *> *instance_extensions);

// GLFW
#if BUILD_USE_GLFW

// Define as a build option 
#define USE_FRAMEWORK_GLFW 1
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
// For Windows Message Box
#if defined( _WIN32 )
#undef APIENTRY
#include <windows.h>
#endif 

// WINDOWS
#elif defined( _WIN32 )
// this is always defined on windows platform

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <windows.h>

#elif defined(__ANDROID__)

#include <android/native_window.h> // requires ndk r5 or newer
#include <android/native_window_jni.h> // requires ndk r5 or newer

// LINUX
#elif defined( __linux )

// SDL2 surface. Preferred on handhelds such as the RK3566 ones running
// dArkOS: no X server and no Wayland compositor, and SDL already owns the KMS
// plane (it takes DRM master), so SDL's own KMSDRM Vulkan backend is the only
// way to get a surface without fighting it for the display.
#if defined( YAB_VK_SDL )

#define VK_USE_PLATFORM_SDL_KHR 1

// Direct-to-display (KMS/DRM), no window system at all. Only correct if the
// front end does NOT create an SDL video window, otherwise the two contend
// for the same display. Kept as a fallback.
#elif defined( YAB_VK_DISPLAY )

#define VK_USE_PLATFORM_DISPLAY_KHR 1

#else
// there are other ways to create windows on linux,
// requirements might change based on the available libraries and distro used
// for example: xlib, wayland and mir
// xcb seems like a popular and well supported option on X11, until wayland and mir take over

#define VK_USE_PLATFORM_XCB_KHR 1
#include <xcb/xcb.h>

#endif

#else
// platform not yet supported
#error Platform not yet supported
#endif

#include <vulkan/vulkan.h>
