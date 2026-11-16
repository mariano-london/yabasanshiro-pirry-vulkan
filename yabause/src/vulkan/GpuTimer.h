#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "QueueLock.h"

/* GPU time per pass (instrumentation, YABA_VK_GPUTIMER=0 turns it off). Timestamps are written between the passes of one command buffer;
   the result of each interval is accumulated per label and printed every 10 s as
   "GPUSTAT <name>: <label> ... ms/frame". On a tile-based GPU only whole passes are meaningful (timestamps inside a render pass are not),
   so mark() belongs between passes, not between draws. collect() must run after the submit's fence has completed. */
class YabaGpuTimer {
public:
  void init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily, const char * name);
  void begin(VkCommandBuffer cb);                       /* resets the pool and writes the first timestamp */
  void mark(VkCommandBuffer cb, const char * label);    /* the interval since the previous timestamp belongs to `label` */
  void collect();                                        /* after the fence: read, accumulate, print every 10 s */
private:
  static const uint32_t kMax = 24;
  VkDevice dev = VK_NULL_HANDLE;
  VkQueryPool pool = VK_NULL_HANDLE;
  double periodNs = 0.0;
  const char * name = "";
  const char * labels[kMax];
  uint32_t used = 0;
  bool ok = false;
  /* accumulation */
  double sumMs[kMax];
  const char * sumLabel[kMax];
  uint32_t sumCount = 0, frames = 0;
  long long lastPrint = 0;
};
