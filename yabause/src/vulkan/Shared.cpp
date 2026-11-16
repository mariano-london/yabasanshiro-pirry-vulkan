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

#include "BUILD_OPTIONS.h"
#include "Shared.h"
#include "perflog.h"

#include <stdio.h>
#include <vector>
#include <stdlib.h>

#include "VulkanTools.h"

#include <sstream>
using std::ostringstream;

#if defined(ANDROID)
#include <iostream>
#include <iomanip>

#include <unwind.h>
#include <dlfcn.h>

namespace {

  struct BacktraceState
  {
    void** current;
    void** end;
  };

  static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg)
  {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
      if (state->current == state->end) {
        return _URC_END_OF_STACK;
      }
      else {
        *state->current++ = reinterpret_cast<void*>(pc);
      }
    }
    return _URC_NO_REASON;
  }

}

size_t captureBacktrace(void** buffer, size_t max)
{
  BacktraceState state = { buffer, buffer + max };
  _Unwind_Backtrace(unwindCallback, &state);

  return state.current - buffer;
}

void dumpBacktrace(std::ostream& os, void** buffer, size_t count)
{
  for (size_t idx = 0; idx < count; ++idx) {
    const void* addr = buffer[idx];
    const char* symbol = "";

    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname) {
      symbol = info.dli_sname;
    }

    os << "  #" << std::setw(2) << idx << ": " << addr << "  " << symbol << "\n";
  }
}

#include <sstream>
#include <android/log.h>

void backtraceToLogcat()
{
  const size_t max = 30;
  void* buffer[max];
  std::ostringstream oss;

  dumpBacktrace(oss, buffer, captureBacktrace(buffer, max));

  __android_log_print(ANDROID_LOG_INFO, "app_name", "%s", oss.str().c_str());
}

#endif

#if BUILD_ENABLE_VULKAN_RUNTIME_DEBUG

void ErrorCheck(VkResult result)
{
  std::ostringstream stream;
  if (result < 0) {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      stream << "VK_ERROR_OUT_OF_HOST_MEMORY" << std::endl;
      break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      stream << "VK_ERROR_OUT_OF_DEVICE_MEMORY" << std::endl;
      break;
    case VK_ERROR_INITIALIZATION_FAILED:
      stream << "VK_ERROR_INITIALIZATION_FAILED" << std::endl;
      break;
    case VK_ERROR_DEVICE_LOST:
      stream << "VK_ERROR_DEVICE_LOST" << std::endl;
      break;
    case VK_ERROR_MEMORY_MAP_FAILED:
      stream << "VK_ERROR_MEMORY_MAP_FAILED" << std::endl;
      break;
    case VK_ERROR_LAYER_NOT_PRESENT:
      stream << "VK_ERROR_LAYER_NOT_PRESENT" << std::endl;
      break;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      stream << "VK_ERROR_EXTENSION_NOT_PRESENT" << std::endl;
      break;
    case VK_ERROR_FEATURE_NOT_PRESENT:
      stream << "VK_ERROR_FEATURE_NOT_PRESENT" << std::endl;
      break;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      stream << "VK_ERROR_INCOMPATIBLE_DRIVER" << std::endl;
      break;
    case VK_ERROR_TOO_MANY_OBJECTS:
      stream << "VK_ERROR_TOO_MANY_OBJECTS" << std::endl;
      break;
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      stream << "VK_ERROR_FORMAT_NOT_SUPPORTED" << std::endl;
      break;
    case VK_ERROR_SURFACE_LOST_KHR:
      stream << "VK_ERROR_SURFACE_LOST_KHR" << std::endl;
      break;
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      stream << "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR" << std::endl;
      break;
    case VK_SUBOPTIMAL_KHR:
      stream << "VK_SUBOPTIMAL_KHR" << std::endl;
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
      stream << "VK_ERROR_OUT_OF_DATE_KHR" << std::endl;
      break;
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
      stream << "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR" << std::endl;
      break;
    case VK_ERROR_VALIDATION_FAILED_EXT:
      stream << "VK_ERROR_VALIDATION_FAILED_EXT" << std::endl;
      break;
    default:
      break;
    }
#if (ANDROID)
    LOGE("%s", stream.str().c_str());
    backtraceToLogcat();
#endif
    std::cout << stream.str();
    //assert( 0 && "Vulkan runtime error." );
  }
}

#else

void ErrorCheck(VkResult result) {};

#endif // BUILD_ENABLE_VULKAN_RUNTIME_DEBUG

/* See the comment on YabaVkSubmit in Shared.h. */
static YabaVkFailHook g_yabaFailHook = nullptr;
void YabaVkSetFailHook(YabaVkFailHook hook) { g_yabaFailHook = hook; }
void YabaVkRunFailHook(const char * tag) { if (g_yabaFailHook) g_yabaFailHook(tag); }

/* Per-tag submit statistics (instrumentation): how long each named submit costs the calling thread, split into the vkQueueSubmit call
   and the fence wait. Printed every 10 s as "VKTAGSTAT: <tag> per second: n, subcall ms, fence-wait ms". This is the attribution for the
   fence-wait total in the frame CSV (vk_fence_us). */
#include <mutex>
#include <string.h>
namespace {
struct TagStat { const char * tag; long long n, sub_ns, wait_ns; };
TagStat g_tagStats[24];
int g_tagCount = 0;
long long g_tagLast = 0;
std::mutex g_tagMutex;
void TagStatAdd(const char * tag, long long sub_ns, long long wait_ns) {
  std::lock_guard<std::mutex> lock(g_tagMutex);
  TagStat * s = nullptr;
  for (int i = 0; i < g_tagCount; i++) if (strcmp(g_tagStats[i].tag, tag) == 0) { s = &g_tagStats[i]; break; }
  if (s == nullptr && g_tagCount < 24) { s = &g_tagStats[g_tagCount++]; s->tag = tag; s->n = s->sub_ns = s->wait_ns = 0; }
  if (s != nullptr) { s->n++; s->sub_ns += sub_ns; s->wait_ns += wait_ns; }
  const long long now = PerfNowNs();
  if (g_tagLast == 0) g_tagLast = now;
  if (now - g_tagLast >= 10000000000LL) {
    const double secs = (double)(now - g_tagLast) / 1e9;
    for (int i = 0; i < g_tagCount; i++) {
      TagStat & t = g_tagStats[i];
      if (t.n > 0) fprintf(stderr, "VKTAGSTAT: %-44s per second: n %.1f, subcall %.2f ms, fence-wait %.2f ms\n", t.tag,
                           (double)t.n / secs, (double)t.sub_ns / 1e6 / secs, (double)t.wait_ns / 1e6 / secs);
      t.n = t.sub_ns = t.wait_ns = 0;
    }
    fflush(stderr);
    g_tagLast = now;
  }
}
}

void YabaGpuTimer::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const char * n) {
  (void)queue;
  const char * e = getenv("YABA_VK_GPUTIMER");
  if (e != nullptr && e[0] == '0') return;
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical, &props);
  uint32_t qn = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, nullptr);
  std::vector<VkQueueFamilyProperties> qf(qn);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &qn, qf.data());
  if (queueFamily >= qn || qf[queueFamily].timestampValidBits == 0 || props.limits.timestampPeriod <= 0.0f) {
    fprintf(stderr, "GPUSTAT %s: timestamps not supported on this queue (validBits %u, period %f), timer off\n", n,
            queueFamily < qn ? qf[queueFamily].timestampValidBits : 0u, props.limits.timestampPeriod);
    fflush(stderr);
    return;
  }
  VkQueryPoolCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
  ci.queryCount = kMax;
  if (vkCreateQueryPool(device, &ci, nullptr, &pool) != VK_SUCCESS) return;
  dev = device; periodNs = props.limits.timestampPeriod; name = n; ok = true;
  for (uint32_t i = 0; i < kMax; i++) sumMs[i] = 0;
  sumCount = 0; frames = 0; lastPrint = 0; used = 0;
  fprintf(stderr, "GPUSTAT %s: timestamp period %.2f ns, valid bits %u\n", n, periodNs, qf[queueFamily].timestampValidBits);
  fflush(stderr);
}

void YabaGpuTimer::begin(VkCommandBuffer cb) {
  if (!ok) return;
  vkCmdResetQueryPool(cb, pool, 0, kMax);
  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
  used = 1;
}

void YabaGpuTimer::mark(VkCommandBuffer cb, const char * label) {
  if (!ok || used == 0 || used >= kMax) return;
  vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, used);
  labels[used] = label;
  used++;
}

void YabaGpuTimer::collect() {
  if (!ok || used < 2) { used = 0; return; }
  uint64_t ts[kMax];
  const VkResult r = vkGetQueryPoolResults(dev, pool, 0, used, sizeof(ts), ts, sizeof(uint64_t),
                                           VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
  if (r == VK_SUCCESS) {
    for (uint32_t i = 1; i < used; i++) {
      const double ms = (double)(ts[i] - ts[i - 1]) * periodNs / 1e6;
      uint32_t k = 0;
      while (k < sumCount && sumLabel[k] != labels[i]) k++;
      if (k == sumCount && sumCount < kMax) { sumLabel[sumCount] = labels[i]; sumMs[sumCount] = 0; sumCount++; }
      if (k < sumCount) sumMs[k] += ms;
    }
    const double total = (double)(ts[used - 1] - ts[0]) * periodNs / 1e6;
    frames++;
    sumMs[kMax - 1] += total;
  }
  used = 0;
  const long long now = PerfNowNs();
  if (lastPrint == 0) lastPrint = now;
  if (now - lastPrint >= 10000000000LL && frames > 0) {
    fprintf(stderr, "GPUSTAT %s: per frame over %u frames:", name, frames);
    for (uint32_t i = 0; i < sumCount; i++) fprintf(stderr, " %s %.2f ms,", sumLabel[i], sumMs[i] / frames);
    fprintf(stderr, " TOTAL %.2f ms\n", sumMs[kMax - 1] / frames);
    fflush(stderr);
    for (uint32_t i = 0; i < kMax; i++) sumMs[i] = 0;
    sumCount = 0; frames = 0; lastPrint = now;
  }
}

/* Submit without waiting for the job: the caller keeps the fence and calls YabaVkWaitFence() when the results (or the resources the job
   reads) are needed. Used for the VDP2 scene when two frame sets are in use. */
void YabaVkSubmitAsync(VkDevice device, VkQueue queue, const VkSubmitInfo * submit_info, const char * tag, VkFence * out) {
  *out = VK_NULL_HANDLE;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
    fprintf(stderr, "VKSUB %s: vkCreateFence failed (async)\n", tag);
    fflush(stderr);
    ErrorCheck(YabaQueueSubmit(queue, 1, submit_info, VK_NULL_HANDLE));
    return;
  }
  const long long t0 = PerfNowNs();
  const VkResult sub = YabaQueueSubmit(queue, 1, submit_info, fence);
  const long long t1 = PerfNowNs();
  PerfAddVk(6, t1 - t0); PerfAddVk(5, 1);
  if (sub != VK_SUCCESS) {
    fprintf(stderr, "VKSUB %s: vkQueueSubmit = %d (async)\n", tag, (int)sub);
    fflush(stderr);
    ErrorCheck(sub);
    vkDestroyFence(device, fence, nullptr);
    return;
  }
  TagStatAdd(tag, t1 - t0, 0);
  *out = fence;
}

void YabaVkWaitFence(VkDevice device, VkFence fence, const char * tag) {
  if (fence == VK_NULL_HANDLE) return;
  const long long t0 = PerfNowNs();
  const VkResult w = vkWaitForFences(device, 1, &fence, VK_TRUE, 2ull * 1000ull * 1000ull * 1000ull);
  const long long t1 = PerfNowNs();
  PerfAddVk(1, t1 - t0);
  TagStatAdd(tag, 0, t1 - t0);
  if (w != VK_SUCCESS) {
    fprintf(stderr, "VKSUB %s: JOB DID NOT COMPLETE (deferred vkWaitForFences = %d). Check dmesg for a Mali job fault.\n", tag, (int)w);
    fflush(stderr);
    if (g_yabaFailHook) g_yabaFailHook(tag);
  }
  vkDestroyFence(device, fence, nullptr);
}

pthread_mutex_t g_yabaQueueMutex = PTHREAD_MUTEX_INITIALIZER;

int YabaMergeSubmits(void) {
  static int v = -1;
  if (v < 0) { const char * e = getenv("YABA_VK_MERGE_SUBMITS"); v = (e != nullptr && e[0] >= '0' && e[0] <= '2') ? (e[0] - '0') : 2; } /* default 2; =0 restores the separate upload submits */
  return v;
}

void YabaVkSubmit(VkDevice device, VkQueue queue,
                  const VkSubmitInfo * submit_info, const char * tag)
{
  static int seq = 0;
  static int sync = -1;
  static int tracing = 1;
  if (sync < 0) {
    const char * e = getenv("YABA_VK_SYNC");
    sync = (e != nullptr && e[0] == '0') ? 0 : 1;
    /* YABA_VK_TRACE=0 silences the per-submit name lines as well, so a
       performance run writes nothing at all from this path -- the fprintf per
       submit per frame is itself measurable, and a fair comparison against
       the OpenGL build needs both switches off:
         YABA_VK_SYNC=0 YABA_VK_TRACE=0
       A job that fails to complete is still reported when sync is on,
       whatever YABA_VK_TRACE says. */
    const char * t = getenv("YABA_VK_TRACE");
    tracing = (t != nullptr && t[0] == '0') ? 0 : 1;
    if (tracing) {
      fprintf(stderr, "VKSUB: submit tracing ON, fence-per-submit %s\n",
              sync ? "ON" : "off");
      fflush(stderr);
    }
  }
  if (!tracing && !sync) {
    /* Nothing to observe and nothing to wait for: straight through. */
    ErrorCheck(YabaQueueSubmit(queue, 1, submit_info, VK_NULL_HANDLE));
    return;
  }

  const int n = ++seq;
  /* Printed BEFORE the submit: if the process wedges, the last line in the
     log is the submit that did it. Only the opening run is traced; after
     that the renderer is in steady state and the log would just grow. */
  const int trace = tracing && (n <= 80);
  if (trace) {
    fprintf(stderr, "VKSUB %03d %s submit (wait %u sem, signal %u sem)\n",
            n, tag, submit_info->waitSemaphoreCount,
            submit_info->signalSemaphoreCount);
    fflush(stderr);
  }

  if (!sync) {
    ErrorCheck(YabaQueueSubmit(queue, 1, submit_info, VK_NULL_HANDLE));
    return;
  }

  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
    fprintf(stderr, "VKSUB %03d %s: vkCreateFence failed\n", n, tag);
    fflush(stderr);
    ErrorCheck(YabaQueueSubmit(queue, 1, submit_info, VK_NULL_HANDLE));
    return;
  }

  const long long t_sub0 = PerfNowNs();
  const VkResult sub = YabaQueueSubmit(queue, 1, submit_info, fence);
  const long long t_sub1 = PerfNowNs();
  PerfAddVk(6, t_sub1 - t_sub0); PerfAddVk(5, 1);
  if (sub != VK_SUCCESS) {
    fprintf(stderr, "VKSUB %03d %s: vkQueueSubmit = %d\n", n, tag, (int)sub);
    fflush(stderr);
    ErrorCheck(sub);
    vkDestroyFence(device, fence, nullptr);
    return;
  }

  /* Two seconds is far longer than any legitimate job here and short enough
     that a dead job is reported rather than hanging the emulator. */
  const long long t_w0 = PerfNowNs();
  const VkResult w = vkWaitForFences(device, 1, &fence, VK_TRUE,
                                     2ull * 1000ull * 1000ull * 1000ull);
  const long long t_w1 = PerfNowNs();
  PerfAddVk(1, t_w1 - t_w0);
  TagStatAdd(tag, t_sub1 - t_sub0, t_w1 - t_w0);
  if (w != VK_SUCCESS) {
    fprintf(stderr,
            "VKSUB %03d %s: JOB DID NOT COMPLETE (vkWaitForFences = %d). "
            "This is the submit the GPU killed -- check dmesg for a Mali "
            "job fault at this point.\n", n, tag, (int)w);
    fflush(stderr);
    if (g_yabaFailHook) g_yabaFailHook(tag);
  } else if (trace) {
    fprintf(stderr, "VKSUB %03d %s done\n", n, tag);
    fflush(stderr);
  }
  vkDestroyFence(device, fence, nullptr);
}

uint32_t FindMemoryTypeIndex(const VkPhysicalDeviceMemoryProperties * gpu_memory_properties, const VkMemoryRequirements * memory_requirements, const VkMemoryPropertyFlags required_properties)
{
  for (uint32_t i = 0; i < gpu_memory_properties->memoryTypeCount; ++i) {
    if (memory_requirements->memoryTypeBits & (1 << i)) {
      if ((gpu_memory_properties->memoryTypes[i].propertyFlags & required_properties) == required_properties) {
        return i;
      }
    }
  }
  assert(0 && "Couldn't find proper memory type.");
  return UINT32_MAX;
}


/* ---- object naming (see Shared.h) ---- */
static PFN_vkSetDebugUtilsObjectNameEXT s_setName = nullptr;
static bool s_namesResolved = false;

bool YabaVkNamesWanted()
{
  const char * e = getenv("YABA_VK_NAMES");
  return e && atoi(e) != 0;
}

void YabaVkName(VkDevice device, VkObjectType type, uint64_t handle, const char * name)
{
  if (!YabaVkNamesWanted() || device == VK_NULL_HANDLE || handle == 0) return;
  if (!s_namesResolved) {
    s_namesResolved = true;
    s_setName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    printf("VKNAMES: %s\n", s_setName ? "enabled" : "requested but VK_EXT_debug_utils not available");
  }
  if (!s_setName) return;
  VkDebugUtilsObjectNameInfoEXT info = {};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectType = type;
  info.objectHandle = handle;
  info.pObjectName = name;
  s_setName(device, &info);
}


void YabaVkInitColorImage(VkDevice device, VkQueue queue, uint32_t queueFamily, VkImage image,
                          VkImageLayout finalLayout, VkAccessFlags dstAccess, VkPipelineStageFlags dstStage)
{
  VkCommandPoolCreateInfo pi = {};
  pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pi.queueFamilyIndex = queueFamily;
  VkCommandPool pool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(device, &pi, nullptr, &pool) != VK_SUCCESS) return;
  VkCommandBufferAllocateInfo ai = {};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = pool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &ai, &cb);
  VkCommandBufferBeginInfo bi = {};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  VkImageMemoryBarrier b = {};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = range;
  b.srcAccessMask = 0;
  b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  VkClearColorValue zero = {};
  vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b.dstAccessMask = dstAccess;
  b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b.newLayout = finalLayout;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
  vkEndCommandBuffer(cb);
  VkSubmitInfo si = {};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  YabaVkSubmit(device, queue, &si, "YabaVkInitColorImage");
  YabaQueueWaitIdle(queue);
  vkDestroyCommandPool(device, pool, nullptr);
}


#include <malloc.h>
#include <unistd.h>
static long YabaRssKb() {
  FILE * f = fopen("/proc/self/statm", "r");
  if (!f) return -1;
  long total = 0, rss = 0;
  if (fscanf(f, "%ld %ld", &total, &rss) != 2) rss = -1;
  fclose(f);
  return rss < 0 ? -1 : rss * (sysconf(_SC_PAGESIZE) / 1024);
}

void YabaTrimMemory(const char * why)
{
  const long before = YabaRssKb();
  malloc_trim(0);
  const long after = YabaRssKb();
  static unsigned calls = 0;
  calls++;
  /* every call that gave back >= 1 MB, plus every 50th, so a burst of creations cannot flood the log */
  if ((before - after) >= 1024 || (calls % 50) == 0) {
    printf("MEMTRIM: %s rss %ld kB -> %ld kB (released %ld kB) [call %u]\n", why, before, after, before - after, calls);
    fflush(stdout);
  }
}
