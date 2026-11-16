#pragma once
/* Zero-initialising allocation for the renderer classes.
   Most of them hold dozens of raw Vk handles and counters with no initialiser, and are created with
   `new`, so every such member started as garbage (found by UBSAN: an uninitialised enum, and by the
   rig: an uninitialised VkRenderPass tested against VK_NULL_HANDLE). Zeroed memory makes all of
   them VK_NULL_HANDLE / 0 / false, which is what the code assumes. Applied to the classes that are
   heap-allocated; derived classes inherit it. */
#include <stdlib.h>   /* not <new>/<cstdlib>: some includers wrap headers in extern "C" */
#include <stddef.h>
#define YABA_ZERO_NEW \
  static void * operator new(size_t n) { void * p = calloc(1, n); if (!p) abort(); return p; } \
  static void operator delete(void * p) { free(p); }
