#pragma once
#include <pthread.h>
#include <vulkan/vulkan.h>

/* The VkQueue is not externally synchronised by the driver: two threads calling vkQueueSubmit / vkQueueWaitIdle / vkQueuePresentKHR on it at the same
   time is undefined behaviour (the validation layer reports it as UNASSIGNED-Threading-MultipleThreads). This emulator uses one queue from the render
   thread (scene, VDP1, present) and, for VDP1 framebuffer reads and writes, from the emulation thread. Every queue call goes through these wrappers,
   which serialise them with one mutex. The mutex is never held across a fence wait. */
extern pthread_mutex_t g_yabaQueueMutex;

static inline VkResult YabaQueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo * info, VkFence fence) {
  pthread_mutex_lock(&g_yabaQueueMutex);
  const VkResult r = vkQueueSubmit(q, n, info, fence);
  pthread_mutex_unlock(&g_yabaQueueMutex);
  return r;
}
static inline VkResult YabaQueueWaitIdle(VkQueue q) {
  pthread_mutex_lock(&g_yabaQueueMutex);
  const VkResult r = vkQueueWaitIdle(q);
  pthread_mutex_unlock(&g_yabaQueueMutex);
  return r;
}
static inline VkResult YabaQueuePresent(VkQueue q, const VkPresentInfoKHR * info) {
  pthread_mutex_lock(&g_yabaQueueMutex);
  const VkResult r = vkQueuePresentKHR(q, info);
  pthread_mutex_unlock(&g_yabaQueueMutex);
  return r;
}
