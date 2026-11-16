/* -----------------------------------------------------
This source code is public domain ( CC0 )
The code is provided as-is without limitations, requirements and responsibilities.
Creators and contributors to this source code are provided as a token of appreciation
and no one associated with this source code can be held responsible for any possible
damages or losses of any kind.

Original file creator:  Niko Kauppi (Code maintenance)
Contributors:
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

#include "perflog.h"
#define YABA_QIDLE(call) do { const long long _qi0 = PerfNowNs(); call; PerfAddVk(4, PerfNowNs() - _qi0); } while (0)
#include "Platform.h"

#include <iostream>
#include <assert.h>

void ErrorCheck(VkResult result);

/* Object naming (instrumentation). Off unless YABA_VK_NAMES=1: then the
   instance enables VK_EXT_debug_utils (if the loader offers it) and every
   named object shows up in validation messages as "[creation expr @ file:line]".
   A no-op otherwise, so it is safe to leave in the device build. */
/* Flight recorder hook: called (once per event) when a traced submit does not complete, so the renderer can print
   what the last few frames contained. Instrumentation only. */
typedef void (*YabaVkFailHook)(const char * tag);
void YabaVkSetFailHook(YabaVkFailHook hook);
void YabaVkRunFailHook(const char * tag);          /* run the hook by hand (uncaught exception, device lost seen by a C++ wrapper) */
void YabaFlightNote(const char * fmt, ...);        /* append a note to the flight recorder's current frame (defined in VIDVulkan.cpp) */

/* Hands freed heap memory back to the system and logs resident size before/after ("MEMTRIM: ..."). Shader
   compilation (shaderc/glslang) fills a per-thread glibc heap that is never returned on its own: on the handheld that
   was a 64 MB, 100% resident mapping, 40% of the emulator's footprint, on a 1 GB device that is thrashing. */
/* YABA_VK_MERGE_SUBMITS=1 (VDP1 renderer) or 2 (VDP1 and the VDP2 scene): texture/vertex uploads are recorded into the command buffer that uses them (one submit and one fence wait
   per renderer instead of two). */
int YabaMergeSubmits(void);
/* Submit without waiting; the returned fence is waited for (and destroyed) by YabaVkWaitFence. */
void YabaVkSubmitAsync(VkDevice device, VkQueue queue, const VkSubmitInfo * submit_info, const char * tag, VkFence * out);
void YabaVkWaitFence(VkDevice device, VkFence fence, const char * tag);

#include "GpuTimer.h"   /* YabaGpuTimer */
void YabaTrimMemory(const char * why);

void YabaVkName(VkDevice device, VkObjectType type, uint64_t handle, const char * name);
bool YabaVkNamesWanted();

/* Puts a freshly created image into the layout the code will later assume (e.g. SHADER_READ_ONLY_OPTIMAL for
   a render target that is sampled), with its contents cleared to zero. Images are created UNDEFINED; sampling
   one before its first render (the first frame after a resolution change) was a validation error and, on a
   tile-based GPU, reads garbage. One-shot, waits for completion; call at creation time only. The image needs
   TRANSFER_DST usage. */
void YabaVkInitColorImage(VkDevice device, VkQueue queue, uint32_t queueFamily, VkImage image,
                          VkImageLayout finalLayout, VkAccessFlags dstAccess, VkPipelineStageFlags dstStage);
#define YABA_VK_NAME(dev, type, obj, label) \
  YabaVkName((dev), (type), (uint64_t)(obj), label " @ " __FILE__ ":" YABA_VK_STR(__LINE__))
#define YABA_VK_STR2(x) #x
#define YABA_VK_STR(x) YABA_VK_STR2(x)

/* ---------------------------------------------------------------------------
   Submit tracing, for finding a job that the GPU kills.

   Several submits in this renderer are fire-and-forget (no fence, no wait).
   When one of their jobs faults -- on the RK3566's Mali-G52 that shows up in
   dmesg as "job status 0x00000058 (DATA_INVALID_FAULT) from job slot 0" --
   nothing notices: the queue is left wedged, and the next vkQueueWaitIdle,
   which is inside Window::BeginRender(), blocks forever. The renderer then
   hangs on its first frame with a black screen and no error anywhere.

   YabaVkSubmit() names each submit in the log and, unless YABA_VK_SYNC=0,
   waits on a fence after every one with a finite timeout, so the job that
   never completes is reported by name instead of having to be guessed at.
   There are no Vulkan validation layers on the device, so this is the only
   way to get that information.
--------------------------------------------------------------------------- */
void YabaVkSubmit(VkDevice device, VkQueue queue,
                  const VkSubmitInfo * submit_info, const char * tag);

uint32_t FindMemoryTypeIndex(const VkPhysicalDeviceMemoryProperties * gpu_memory_properties, const VkMemoryRequirements * memory_requirements, const VkMemoryPropertyFlags memory_properties);
