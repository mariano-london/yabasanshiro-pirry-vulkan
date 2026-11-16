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

#include "TextureManager.h"
#include "Shared.h"   /* YabaVkSubmit: traced queue submits */
#include "VIDVulkan.h"
#include <dlfcn.h>
static void * TexMapBase() { Dl_info d; if (dladdr((void*)&TexMapBase, &d) && d.dli_fbase) return d.dli_fbase; return nullptr; }
#include "vulkan/vulkan.hpp"

/* Texture statistics counters (instrumentation, see YabaTexStatFrame) */
thread_local int g_ts_unit = 0;
static unsigned int g_ts_pa = 0, g_ts_prows = 0, g_ts_hgt = 0;
static unsigned long g_ts_spill = 0, g_ts_u_inval[3] = {0,0,0};
static unsigned long g_ts_u_hit[3] = {0,0,0}, g_ts_u_miss[3] = {0,0,0}, g_ts_u_rep[3] = {0,0,0}, g_ts_u_alloc[3] = {0,0,0}, g_ts_u_pix[3] = {0,0,0};
static unsigned long g_ts_hit = 0, g_ts_miss = 0, g_ts_alloc = 0, g_ts_pixels = 0, g_ts_frames = 0;

TextureManager::~TextureManager() {
  const VkDevice device = vulkan->getDevice();

  YabaQueueWaitIdle(vulkan->getVulkanQueue());

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);
  vkDestroySampler(device, _textureSampler, nullptr);
  vkDestroyImageView(device, _textureImageView, nullptr);
  vkDestroyImage(device, _textureImage, nullptr);
  vkFreeMemory(device, _textureImageMemory, nullptr);


  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
  vkDestroyCommandPool(device, commandPool, nullptr);
}

int TextureManager::init(unsigned int w, unsigned int h) {

  const VkDevice device = vulkan->getDevice();
  vk::Device d(device);

  _texture = (uint32_t*)createTextureImage(w, h); //(uint32_t*)malloc( w*h*4);
  _width = w;
  _height = h;
  reset();

  VkCommandPoolCreateInfo pool_create_info{};
  pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_create_info.queueFamilyIndex = vulkan->getVulkanGraphicsQueueFamilyIndex();
  vkCreateCommandPool(device, &pool_create_info, nullptr, &commandPool);

  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkSemaphoreCreateInfo semaphore_create_info{};
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  vkCreateSemaphore(device, &semaphore_create_info, nullptr, &complete);

  return 0;
}

extern "C" int g_texcache_persist;
extern u32 v2_page_seq[128];
extern u32 v2_wseq;
extern u32 v2_flush_seq;

void TextureManager::reset() {
  _currentX = 0;
  _currentY = 0;
  _yMax = 0;
  if (!_envRead) { _envRead = true; const char * e = getenv("YABA_VK_TEXPERSIST"); _persistEnv = (e != nullptr && e[0] == '1'); _pY = kTransientRows; _pYMax = kTransientRows; _pX = 0; }
  const bool act = _persistAllowed && _persistEnv && g_texcache_persist != 0;
  { static int once = 0; if (once++ < 3) { printf("TEXPERSIST: reset() env=%d g_texcache_persist=%d active=%d\n", (int)_persistEnv, g_texcache_persist, (int)_persistActive); fflush(stdout); } }
  if (act != _persistActive || _flushReq) {
    /* switched on/off, or the persistent area is full / invalid: forget every persistent entry. */
    if (_flushReq) { printf("TEXPERSIST: flush (area rows used %u)\n", _pY); fflush(stdout); }
    _texCacheP.clear(); _pX = 0; _pY = kTransientRows; _pYMax = kTransientRows;
    _pDirtyMin = 0xFFFFFFFFu; _pDirtyMax = 0; _flushReq = false; _persistActive = act;
    if (_height > 0 && _height < kTransientRows + 512) { /* make room for the persistent area now, not in the middle of a frame */
      this->realloc(_width, kTransientRows + 1024);
    }
  }
  if (_persistActive) { g_ts_pa = 1; g_ts_prows = _pY; g_ts_hgt = _height; } /* only an instance that uses the persistent area reports (other TextureManager instances exist) */
  resetCache();
}

void TextureManager::realloc(unsigned int width, unsigned int height) {
  uint32_t * newtexture = (uint32_t*)reallocTextureImage(_texture, width, height);
  _width = width;
  _height = height;
  _texture = newtexture;
}

void TextureManager::allocate(CharTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  if (_persistActive) {
    const bool pers = _nextPersist; _nextPersist = false;
    g_ts_alloc++; g_ts_pixels += (unsigned long)w * h;
    { int u = g_ts_unit & 3; if (u > 2) u = 0; g_ts_u_alloc[u]++; g_ts_u_pix[u] += (unsigned long)w * h; }
    if (pers) {
      allocateIn(_pX, _pY, _pYMax, 0xFFFFFFFFu, output, w, h, x, y);
      { static int dbg = 0; if (dbg++ < 6 || (dbg % 2000) == 0) { printf("TEXPERSIST: palloc #%d w %u h %u -> x %u y %u (cursor %u,%u ymax %u, atlas %ux%u)\n", dbg, w, h, *x, *y, _pX, _pY, _pYMax, _width, _height); fflush(stdout); } }
      const unsigned int top = *y, bot = *y + h;
      if (top < _pDirtyMin) _pDirtyMin = top;
      if (bot > _pDirtyMax) _pDirtyMax = bot;
      if (_pY > 3000) _flushReq = true;
      g_ts_prows = _pY; g_ts_hgt = _height;
    } else {
      /* transient: the band above the persistent area; if one frame needs more than the band, spill into the persistent area
         (reclaimed at the next flush). */
      if (_yMax + h > kTransientRows || w > _width) {
        g_ts_spill++;
        allocateIn(_pX, _pY, _pYMax, 0xFFFFFFFFu, output, w, h, x, y);
        const unsigned int top = *y, bot = *y + h;
        if (top < _pDirtyMin) _pDirtyMin = top;
        if (bot > _pDirtyMax) _pDirtyMax = bot;
        if (_pY > 3000) _flushReq = true;
      } else {
        allocateIn(_currentX, _currentY, _yMax, kTransientRows, output, w, h, x, y);
      }
    }
    return;
  }
  g_ts_alloc++; g_ts_pixels += (unsigned long)w * h;
  { int u = g_ts_unit & 3; if (u > 2) u = 0; g_ts_u_alloc[u]++; g_ts_u_pix[u] += (unsigned long)w * h; }
  if (_width < w) {
    //YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    this->realloc(w, _height);
    this->allocate(output, w, h, x, y);
    return;
  }
  if ((_height - _currentY) < h) {
    //YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    this->realloc(_width, _height + 512);
    this->allocate(output, w, h, x, y);
    return;
  }

  if ((_width - _currentX) >= w) {
    *x = _currentX;
    *y = _currentY;
    output->w = _width - w;
    output->textdata = _texture + _currentY * _width + _currentX;
    //yprintf("allocate %lx", (uintptr_t)output->textdata );
    _currentX += w;

    if ((_currentY + h) > _yMax) {
      _yMax = _currentY + h;
    }
  }
  else {
    _currentX = 0;
    _currentY = _yMax;
    this->allocate(output, w, h, x, y);
  }
}

void TextureManager::allocateIn(unsigned int & cx, unsigned int & cy, unsigned int & ymax, unsigned int rowLimit,
                                CharTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  (void)rowLimit;
  for (;;) {
    if (_width < w) { this->realloc(w, _height); continue; }
    if ((_height - cy) < h) { this->realloc(_width, _height + 512); continue; }
    if ((_width - cx) >= w) {
      *x = cx; *y = cy;
      output->w = _width - w;
      output->textdata = _texture + cy * _width + cx;
      cx += w;
      if ((cy + h) > ymax) ymax = cy + h;
      return;
    }
    cx = 0; cy = ymax;
  }
}

/* Same validation as YglRangeClean (yglcache.c) for VDP2: the entry is stale if its source pages were written after it was
   created (page stamp newer than the entry's generation) or VDP2 VRAM was replaced wholesale (flush stamp). */
static bool Vdp2RangeClean(uint32_t srcaddr, uint32_t srclen, uint32_t gen) {
  if ((int32_t)(gen - __atomic_load_n(&v2_flush_seq, __ATOMIC_ACQUIRE)) < 0) return false;
  uint32_t p0 = (srcaddr & 0x7FFFF) >> 12;
  uint32_t p1 = ((srcaddr + (srclen ? srclen - 1 : 0)) & 0x7FFFF) >> 12;
  if (p1 < p0) { p0 = 0; p1 = 127; }
  for (uint32_t p = p0; p <= p1; p++) {
    uint32_t pv = __atomic_load_n(&v2_page_seq[p], __ATOMIC_ACQUIRE);
    if ((int32_t)(pv - gen) > 0) return false;
  }
  return true;
}

bool TextureManager::isBlankHit(uint64_t key) {
  const TextureCache * it = _texCache.find(key);
  if (it == nullptr || !it->blank) return false;
  g_ts_hit++; g_ts_u_hit[2]++;
  return true;
}

int TextureManager::isCachedP(uint64_t key, TextureCache * c, uint32_t srcaddr, uint32_t srclen, uint32_t extra, int cramdep) {
  /* Stamp for a following addCacheP: taken BEFORE the tile is decoded, so a write that lands during the decode makes the
     entry stale at its next lookup instead of being missed. */
  _pendGen = __atomic_load_n(&v2_wseq, __ATOMIC_ACQUIRE);
  _pendSrcAddr = srcaddr; _pendSrcLen = srclen; _pendExtra = extra; _pendCramDep = (uint8_t)cramdep; _pendCramGen = _cramSeq;
  g_ts_unit = 2;
  const TextureCache * it = _texCacheP.find(key);
  if (it != nullptr) {
    if (it->extra == extra && it->srclen == srclen && it->srcaddr == srcaddr &&
        Vdp2RangeClean(it->srcaddr, it->srclen, it->gen) && (!it->cramdep || it->cramgen == _cramSeq)) {
      g_ts_hit++; g_ts_u_hit[2]++;
      c->addr = it->addr; c->x = it->x; c->y = it->y; c->blank = it->blank;
      return 1;
    }
    g_ts_u_inval[2]++;
  }
  g_ts_miss++; g_ts_u_miss[2]++;
  return 0;
}

void TextureManager::addCacheP(uint64_t key, TextureCache * c) {
  TextureCache e = *c;
  e.srcaddr = _pendSrcAddr; e.srclen = _pendSrcLen; e.gen = _pendGen; e.extra = _pendExtra;
  e.cramdep = _pendCramDep; e.cramgen = _pendCramGen;
  _texCacheP.insert(key, e);
}

void TextureManager::push() {

}

void TextureManager::pull() {

}

/* Texture statistics (instrumentation): how much is decoded per frame. "TEXSTAT:" every 120 frames, in the log. */
void YabaTexStatFrame() {
  if (++g_ts_frames % 120 == 0) {
    printf("TEXSTAT: per frame: cache hits %.1f misses %.1f | textures allocated %.1f, %.0f pixels (%.2f Mpix/s at 60 fps)\n",
           (double)g_ts_hit / 120, (double)g_ts_miss / 120, (double)g_ts_alloc / 120, (double)g_ts_pixels / 120, (double)g_ts_pixels / 120 * 60 / 1e6);
    fflush(stdout);
    for (int u = 1; u <= 2; u++) {
      printf("TEXSTAT-UNIT: %s per frame: hits %.1f misses %.1f (key was cached LAST frame: %.1f) | allocs %.1f, %.0f pixels\n",
             u == 1 ? "VDP1" : "VDP2", (double)g_ts_u_hit[u] / 120, (double)g_ts_u_miss[u] / 120, (double)g_ts_u_rep[u] / 120,
             (double)g_ts_u_alloc[u] / 120, (double)g_ts_u_pix[u] / 120);
    }
    if (g_ts_pa) {
      printf("TEXPERSIST: per frame: VDP2 hits invalidated by stamps %.1f | transient spills %.2f | area rows used %u of %u\n",
             (double)g_ts_u_inval[2] / 120, (double)g_ts_spill / 120, g_ts_prows, g_ts_hgt);
    }
    g_ts_spill = 0; g_ts_u_inval[2] = 0;
    for (int u = 0; u < 3; u++) g_ts_u_hit[u] = g_ts_u_miss[u] = g_ts_u_rep[u] = g_ts_u_alloc[u] = g_ts_u_pix[u] = 0;
    g_ts_hit = g_ts_miss = g_ts_alloc = g_ts_pixels = 0;
  }
}

int TextureManager::isCached(uint64_t addr, TextureCache * c) {
  const TextureCache * it = _texCache.find(addr);
  const int su = (g_ts_unit & 3) > 2 ? 0 : (g_ts_unit & 3);
  if (it == nullptr) {
    g_ts_miss++; g_ts_u_miss[su]++;
    if (_texCachePrev.find(addr) != nullptr) g_ts_u_rep[su]++;
    return 0; // not found
  }
  g_ts_hit++; g_ts_u_hit[su]++;

  //*c = _texCache[addr];
  c->addr = it->addr;
  c->x = it->x;
  c->y = it->y;
  c->blank = it->blank;

  return 1; // found
}

void TextureManager::addCache(uint64_t addr, TextureCache * c) {
  _texCache.insert(addr, *c);
}

void TextureManager::resetCache() {
  if (!_texCache.empty()) { std::swap(_texCache, _texCachePrev); }
  _texCache.clear();
}


void * TextureManager::createTextureImage(int texWidth, int texHeight) {

  const VkDevice device = vulkan->getDevice();
  if (device == VK_NULL_HANDLE) return NULL;

  VkDeviceSize imageSize = texWidth * texHeight * 4;

  char * pixels = (char*)malloc(texWidth * texHeight * 4);
  memset(pixels, 0, texWidth * texHeight * 4);

  vulkan->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &imageBuffer);
  memcpy(imageBuffer, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);
  //stbi_image_free(pixels);

  vulkan->createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _textureImage, _textureImageMemory);
  vulkan->transitionImageLayout(_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vulkan->copyBufferToImage(stagingBuffer, _textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
  vulkan->transitionImageLayout(_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  free(pixels);

  vkMapMemory(device, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &imageBuffer);
  //yprintf("createTextureImage vkMapMemory %lx", (uintptr_t)imageBuffer);

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = _textureImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &_textureImageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.anisotropyEnable = VK_FALSE;
  //samplerInfo.maxAnisotropy = 16;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  if (vkCreateSampler(device, &samplerInfo, nullptr, &_textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }

  this->texwidth = texWidth;
  this->texheight = texHeight;

  return imageBuffer;

}

#include <iostream>


void * TextureManager::reallocTextureImage(void * pixels, int texWidth, int texHeight) {

  std::cout << "reallocTextureImage height:" << texHeight << std::endl;

  const VkDevice device = vulkan->getDevice();
  if (device == VK_NULL_HANDLE) return NULL;


  VkDeviceSize imageSize = texWidth * texHeight * 4;

  char * newpixels = (char*)malloc(texWidth * texHeight * 4);
  for (int i = 0; i < this->texheight; i++) {
    char * newrow = &newpixels[i*texWidth * 4];
    char * oldrow = &((char*)pixels)[i*this->texwidth * 4];
    for (int j = 0; j < this->texwidth * 4; j++) {
      newrow[j] = oldrow[j];
    }
  }

  /* The GPU may still be reading the resources destroyed below: they are
     bound by command buffers submitted for earlier frames, and nothing here
     waits for those to retire. Destroying them underneath an in-flight job is
     a use-after-free on the device, which the Mali driver reports as
     "job status 0x00000058 (DATA_INVALID_FAULT) from job slot 0" and then
     loses the device, aborting the process with vk::DeviceLostError. OpenGL
     needs no equivalent because glTexImage2D re-specifies a texture object
     and the driver synchronises internally.
     Every sibling path in this renderer already does this -- see
     TextureManager::~TextureManager, Vdp1Renderer::changeResolution,
     WindowRenderer::changeResolution and VIDVulkan::freeSubRenderTarget. */
  YabaQueueWaitIdle(vulkan->getVulkanQueue());

  if (_unmapped) { fprintf(stderr, "TEXMAP: realloc while the staging buffer is unmapped (tm %p, caller %p)\n", (void*)this, (void*)((char*)__builtin_return_address(0) - (char*)TexMapBase())); fflush(stderr); }
  else vkUnmapMemory(device, stagingBufferMemory);
  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);


  vulkan->createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &imageBuffer);
  memcpy(imageBuffer, newpixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);

  vkDestroyImage(device, _textureImage, nullptr);
  vkFreeMemory(device, _textureImageMemory, nullptr);
  vulkan->createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _textureImage, _textureImageMemory);
  vulkan->transitionImageLayout(_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  vulkan->copyBufferToImage(stagingBuffer, _textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
  vulkan->transitionImageLayout(_textureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  free(newpixels);

  vkMapMemory(device, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &imageBuffer);
  _unmapped = false;
  //yprintf("reallocTextureImage vkMapMemory %lx", (uintptr_t)imageBuffer);

  vkDestroyImageView(device, _textureImageView, nullptr);
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = _textureImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &_textureImageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  this->texwidth = texWidth;
  this->texheight = texHeight;


  return imageBuffer;

}


/* The texture update in three steps, so a caller can fold the upload into a command buffer it records anyway (one submit and one fence
   wait instead of two): beginUpdate() unmaps the staging buffer, recordUpdate() records the copy into the atlas image, endUpdate() maps the
   staging buffer again once the submit that carries the copy has COMPLETED (YabaVkSubmit waits on a fence, so the CPU may write again). */
void TextureManager::beginUpdate() {
  const VkDevice device = vulkan->getDevice();
  if (device == VK_NULL_HANDLE) return;
  if (_unmapped) { fprintf(stderr, "TEXMAP: beginUpdate while the staging buffer is already unmapped (tm %p, caller %p)\n", (void*)this, (void*)((char*)__builtin_return_address(0) - (char*)TexMapBase())); fflush(stderr); }
  vkUnmapMemory(device, stagingBufferMemory);
  _unmapped = true;
}

void TextureManager::recordUpdate(VkCommandBuffer commandBuffer) {
  /* Rows to upload: the transient band [0,_yMax) every frame, plus (persistent cache on) the rows of the persistent area written
     since the last upload. Without the persistent cache this is the single region it always was. */
  const bool havePersistRows = _persistActive && _pDirtyMax > _pDirtyMin;
  if (this->_yMax > 0 || havePersistRows) {

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    vkCmdPipelineBarrier(
      commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier
    );

    VkBufferImageCopy regions[2];
    uint32_t nreg = 0;
    auto addRegion = [&](uint32_t row0, uint32_t rows) {
      VkBufferImageCopy region = {};
      region.bufferOffset = (VkDeviceSize)row0 * _width * 4;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageOffset = { 0, (int32_t)row0, 0 };
      region.imageExtent = { _width, rows, 1 };
      regions[nreg++] = region;
    };
    if (this->_yMax > 0) addRegion(0, this->_yMax);
    if (havePersistRows) {
      uint32_t r0 = _pDirtyMin, r1 = _pDirtyMax;
      if (r1 > _height) r1 = _height;
      if (r1 > r0) addRegion(r0, r1 - r0);
    }
    _pDirtyMin = 0xFFFFFFFFu; _pDirtyMax = 0;
    if (nreg > 0) vkCmdCopyBufferToImage(
      commandBuffer,
      stagingBuffer,
      _textureImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      nreg,
      regions
    );

    VkImageMemoryBarrier destbarrier = {};
    destbarrier = barrier;
    destbarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    destbarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    destbarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    destbarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    destbarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    vkCmdPipelineBarrier(
      commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &destbarrier
    );
  }

}

void TextureManager::endUpdate() {
  const VkDevice device = vulkan->getDevice();
  if (device == VK_NULL_HANDLE) return;
  if (!_unmapped) { fprintf(stderr, "TEXMAP: endUpdate while the staging buffer is mapped (tm %p, caller %p)\n", (void*)this, (void*)((char*)__builtin_return_address(0) - (char*)TexMapBase())); fflush(stderr); }
  vkMapMemory(device, stagingBufferMemory, 0, VK_WHOLE_SIZE, 0, &imageBuffer);
  _unmapped = false;
  _texture = (unsigned int *)imageBuffer;
}

void TextureManager::updateTextureImage(const std::function<void(VkCommandBuffer commandBuffer)>& f) {
  struct TexUpTimer { long long t0; ~TexUpTimer() { PerfAddVk(0, PerfNowNs() - t0); } } texup_timer{ PerfNowNs() };

  const VkDevice device = vulkan->getDevice();
  if (device == VK_NULL_HANDLE) return;

  beginUpdate();

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  //beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  recordUpdate(commandBuffer);

  f(commandBuffer);


  vkEndCommandBuffer(commandBuffer);
  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  submitInfo.signalSemaphoreCount = 0; //1;
  submitInfo.pSignalSemaphores = nullptr; // (VkSemaphore*)&complete;
  YabaVkSubmit(vulkan->getDevice(), vulkan->getVulkanQueue(), &submitInfo,
               "TextureManager::updateTextureImage");

  endUpdate();
}

