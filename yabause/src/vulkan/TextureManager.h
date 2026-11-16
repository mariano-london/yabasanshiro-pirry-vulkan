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

#include "ZeroAlloc.h"
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <functional>

class CharTexture {
public:
  uint32_t * textdata = nullptr;
  uint32_t w = 0;
};

class TextureCache {
public:
  uint64_t addr = 0;
  float x = 0.0f;
  float y = 0.0f;
  /* Persistent-cache validation stamp (YABA_VK_TEXPERSIST, VDP2 tiles only); unused by the per-frame cache. */
  uint32_t srcaddr = 0, srclen = 0, gen = 0, extra = 0, cramgen = 0;
  uint8_t cramdep = 0;
  uint8_t blank = 0;   /* every texel of this tile is 0 (fully transparent): nothing to draw */
};

/* Texture cache table: open addressing with a generation stamp. clear() is O(1) (bump the generation) instead of freeing
   every node of a std::unordered_map each frame (TextureManager::resetCache was 4% of the render thread's CPU in the VF2
   profile, plus node allocation on every insert). A slot is "occupied" only if its stamp equals the current generation. */
class TexCacheTable {
  struct Slot { uint64_t key; uint32_t gen; TextureCache val; };
  std::vector<Slot> slots_;
  uint32_t mask_ = 0, cur_ = 1, count_ = 0;
  static inline uint32_t hash(uint64_t k) { return (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 32); }
  void grow() {
    std::vector<Slot> old; old.swap(slots_);
    slots_.assign(old.size() * 2, Slot{ 0, 0, TextureCache() });
    mask_ = (uint32_t)slots_.size() - 1; count_ = 0;
    for (const Slot & s : old) if (s.gen == cur_) insert(s.key, s.val);
  }
public:
  TexCacheTable() { slots_.assign(4096, Slot{ 0, 0, TextureCache() }); mask_ = 4095; }
  bool empty() const { return count_ == 0; }
  void clear() {
    count_ = 0;
    if (++cur_ == 0) { for (Slot & s : slots_) s.gen = 0; cur_ = 1; }
  }
  TextureCache * findMut(uint64_t k) { return const_cast<TextureCache *>(static_cast<const TexCacheTable *>(this)->find(k)); }
  const TextureCache * find(uint64_t k) const {
    uint32_t i = hash(k) & mask_;
    while (slots_[i].gen == cur_) { if (slots_[i].key == k) return &slots_[i].val; i = (i + 1) & mask_; }
    return nullptr;
  }
  void insert(uint64_t k, const TextureCache & v) {
    if ((count_ + 1) * 2 > slots_.size()) grow();
    uint32_t i = hash(k) & mask_;
    while (slots_[i].gen == cur_ && slots_[i].key != k) i = (i + 1) & mask_;
    if (slots_[i].gen != cur_) count_++;
    slots_[i].key = k; slots_[i].gen = cur_; slots_[i].val = v;
  }
};

class VIDVulkan;

/* Statistics only: which unit the next isCached/allocate belongs to (1 = VDP1 sprite/polygon, 2 = VDP2 tile, 0 = other). */
extern thread_local int g_ts_unit;

class TextureManager {
public:
  YABA_ZERO_NEW
private:
protected:
  unsigned int _currentX = 0;
  unsigned int _currentY = 0;
  unsigned int _yMax = 0;
  unsigned int * _texture = nullptr;
  unsigned int _width = 0;
  unsigned int _height = 0;
  TexCacheTable _texCache;
  /* Persistent texture cache (opt-in, YABA_VK_TEXPERSIST=1 and g_texcache_persist): VDP2 tiles whose VRAM source pages are
     unchanged keep their atlas slot across frames instead of being decoded again every frame. The atlas is split in a
     transient band (rows [0, kTransientRows): rewritten every frame, as before) and a persistent area above it. Entries
     are validated against the core's write stamps (v2_page_seq / v2_flush_seq, same scheme as yglcache.c). */
  TexCacheTable _texCacheP;
  bool _envRead = false, _persistAllowed = false;
  bool _unmapped = false;   /* instrumentation: the staging buffer is currently unmapped (between beginUpdate and endUpdate) */
  bool _persistEnv = false, _persistActive = false, _flushReq = false, _nextPersist = false;
  unsigned int _pX = 0, _pY = 0, _pYMax = 0;           /* persistent area cursors */
  unsigned int _pDirtyMin = 0xFFFFFFFFu, _pDirtyMax = 0; /* rows written since the last upload */
  uint32_t _cramSeq = 0;
  uint32_t _pendGen = 0, _pendSrcAddr = 0, _pendSrcLen = 0, _pendExtra = 0, _pendCramGen = 0; uint8_t _pendCramDep = 0;
  void allocateIn(unsigned int & cx, unsigned int & cy, unsigned int & ymax, unsigned int rowLimit,
                  CharTexture *, unsigned int, unsigned int, unsigned int *, unsigned int *);
  TexCacheTable _texCachePrev; /* statistics only: last frame's keys, to measure how many misses a persistent cache could save */
  VIDVulkan * vulkan;
public:
  TextureManager(VIDVulkan * vulkan) {
    this->vulkan = vulkan;
  }

  virtual ~TextureManager();
  int init(unsigned int w, unsigned int h);
  void reset();
  void realloc(unsigned int width, unsigned int height);
  void allocate(CharTexture *, unsigned int, unsigned int, unsigned int *, unsigned int *);
  void push();
  void pull();

  int isCached(uint64_t, TextureCache *);
  void addCache(uint64_t, TextureCache *);
  void resetCache();

  static const unsigned int kTransientRows = 640;
  /* Only the instance that decodes VDP2 tiles asks for persistence (the VDP1 renderer has its own TextureManager). */
  void allowPersist() { _persistAllowed = true; }
  bool persistActive() const { return _persistActive; }
  unsigned int atlasWidth() const { return _width; }
  void setNextAllocPersistent(bool b) { _nextPersist = b; }
  void markBlank(uint64_t key, bool persistent) { TextureCache * e = persistent ? _texCacheP.findMut(key) : _texCache.findMut(key); if (e) e->blank = 1; }
  /* Fast path for tiles known to be fully transparent: true (and counted as a cache hit) if the per-frame cache holds this key flagged blank. */
  bool isBlankHit(uint64_t key);
  void noteCramWrite() { _cramSeq++; }
  /* Persistent lookup. srcaddr/srclen: the VDP2 VRAM range the tile is decoded from; extra: everything else the decode
     depends on (colour mode, special-colour/priority settings); cramdep: the texels also depend on colour RAM contents. */
  int isCachedP(uint64_t key, TextureCache * c, uint32_t srcaddr, uint32_t srclen, uint32_t extra, int cramdep);
  void addCacheP(uint64_t key, TextureCache * c);

  void * data() { return (void*)_texture; }


  VkImageView geTextureImageView() {
    return _textureImageView;
  }

  VkSampler getTextureSampler() {
    return _textureSampler;
  }

  VkSemaphore getCompleteSemaphore() {
    return VK_NULL_HANDLE; // complete;
  }

  void * createTextureImage(int texWidth, int texHeight);
  void * reallocTextureImage(void * pixels, int texWidth, int texHeight);

  void updateTextureImage(const std::function<void(VkCommandBuffer commandBuffer)>& f);
  void beginUpdate();
  void recordUpdate(VkCommandBuffer commandBuffer);
  void endUpdate();

  VkImage _textureImage;
  VkDeviceMemory _textureImageMemory;
  VkImageView _textureImageView;
  VkSampler _textureSampler;

  int texwidth;
  int texheight;

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  void* imageBuffer;

  VkCommandPool commandPool;
  VkCommandBuffer commandBuffer;

  VkSemaphore complete;

};
