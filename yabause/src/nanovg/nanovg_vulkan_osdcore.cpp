/*  Copyright 2012 Guillaume Duhamel

This file is part of Yabause.

Yabause is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Yabause is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Yabause; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "osdcore.h"
#include "nanovg_osdcore.h"
#include "yabause.h"
#include "scsp.h"
#include "vidsoft.h"
#include "vidogl.h"
#include "peripheral.h"
#include "m68kcore.h"
#include "sh2core.h"
#include "sh2int.h"
#include "cdbase.h"
#include "cs2.h"
#include "debug.h"

#include <stdlib.h>
#include <stdarg.h>

#include "vulkan/vulkan.h"

#define NANOVG_VULKAN_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_vk.h"

//#include "Roboto-Bold.h"
//#include "Roboto-Regular.h"

extern "C" {

  extern unsigned char Roboto_Bold_ttf[];
  extern unsigned char Roboto_Regular_ttf[];
  extern unsigned int Roboto_Bold_ttf_len;
  extern unsigned int Roboto_Regular_ttf_len;

  static int OSDNanovgVInit(void);
  static void OSDNanovgVDeInit(void);
  static void OSDNanovgVReset(void);
  static void OSDNanovgVDisplayMessage(OSDMessage_struct * message, pixel_t * buffer, int w, int h);
  static void OSDNanovgVAddFrameProfileData(char * label, u32 data);
  static int OSDNanovgVUseBuffer(void);
  static void OSDNanovgVAddLogString(char * log);

  OSD_struct OSDNnovgVulkan = {
    OSDCORE_NANOVG_VULKAN,
    "nanovg Vunlan OSD Interface",
    OSDNanovgVInit,
    OSDNanovgVDeInit,
    OSDNanovgVReset,
    OSDNanovgVDisplayMessage,
    OSDNanovgVUseBuffer,
    OSDNanovgVAddFrameProfileData,
    OSDNanovgVAddLogString
  };
}



static NVGcontext* vg = NULL;
static int fontNormal, fontBold;

#define MAX_HISTORY 4
#define MAX_PROFILE_CNT 32

typedef struct FrameProfileInfo {
  char label[MAX_PROFILE_CNT][64];
  u32 time[MAX_PROFILE_CNT];
  int cnt;
} FrameProfileInfo;

static FrameProfileInfo frameinfo_histroy[MAX_HISTORY];
static int current_history_index = 0;
static int profile_index = 0;

int NanovgVulkanSetDevices(VkDevice device, VkPhysicalDevice gpu, VkRenderPass renderPass, VkCommandBuffer cmdBuffer) {
  
  VKNVGCreateInfo createInfo={0};
  createInfo.device = device;
  createInfo.gpu = gpu;
  createInfo.renderpass = renderPass;
  createInfo.cmdBuffer = cmdBuffer;

  if (vg != NULL) {
    nvgUpdateVk(vg,createInfo);
    return 0;
  }

  vg = nvgCreateVk(createInfo, NVG_ANTIALIAS);
  if (vg == NULL) {
    printf("Could not init nanovg.\n");
    return -1;
  }
  OSDNanovgVInit();
  return 0;

}

extern "C" {

static int OSDNanovgVInit(void)
{
  VKNVGCreateInfo create_info = { 0 };
  //create_info.device = device->device;
  //create_info.gpu = device->gpu;
  //create_info.renderpass = fb.render_pass;
  //create_info.cmdBuffer = cmd_buffer;

  if (vg == NULL) {
    printf("Could not init nanovg.\n");
    return 0;
  }

  fontNormal = nvgCreateFontMem(vg, "sans", Roboto_Regular_ttf, Roboto_Regular_ttf_len, 0);
  if (fontNormal == -1) {
    printf("Could not add font italic.\n");
    return -1;
  }
  fontBold = nvgCreateFontMem(vg, "sans", Roboto_Bold_ttf, Roboto_Bold_ttf_len, 0);
  if (fontBold == -1) {
    printf("Could not add font bold.\n");
    return -1;
  }

  memset(frameinfo_histroy, 0, sizeof(FrameProfileInfo)*MAX_HISTORY);
  current_history_index = 0;
  profile_index = 0;

  return 0;
}

static void OSDNanovgVDeInit(void)
{
  nvgDeleteVk(vg);
  vg = NULL;

}

static void OSDNanovgVReset(void)
{
}

static int OSDNanovgVUseBuffer() {
  return 0;
}

}

#if 0
#define MAX_COL 80
#define MAX_LOW 256
char g_message[MAX_LOW][MAX_COL];
int msgcnt = 0;

void OSDPushMessageDirect(char * msg) {

  if (msgcnt >= MAX_LOW) return;
  strncpy(g_message[msgcnt], msg, MAX_COL);
  msgcnt++;

}
#endif

#define MAX_LOG_HISTORY (256)
static char log_histroy[MAX_LOG_HISTORY][128];
static int current_log_history_index = 0;

extern "C" {
static void OSDNanovgVAddLogString(char * log) {
  int index;
  index = current_log_history_index;
  strncpy(log_histroy[index], log, 128);
  current_log_history_index++;
  if (current_log_history_index >= MAX_LOG_HISTORY) {
    current_log_history_index = 0;
  }
}


static void OSDNanovgVAddFrameProfileData(char * label, u32 data) {
  int index;
  if (MAX_PROFILE_CNT <= profile_index) return;
  index = current_history_index % MAX_HISTORY;
  strncpy(frameinfo_histroy[index].label[profile_index], label, 64);
  frameinfo_histroy[index].time[profile_index] = data;
  frameinfo_histroy[index].cnt = profile_index;
  profile_index++;
}
}

#define TOF(a) (((float)a)/255.0f)
#define BASE_ALPHA (0.8f)

NVGcolor graph_colorsvg[MAX_PROFILE_CNT] = {
  { TOF(255),TOF(0),TOF(0),BASE_ALPHA },
  { TOF(255),TOF(255),TOF(0),BASE_ALPHA },
  { TOF(128),TOF(255),TOF(0),BASE_ALPHA },
  { TOF(255),TOF(64),TOF(0),BASE_ALPHA },
  { TOF(0),TOF(255),TOF(255),BASE_ALPHA },
  { TOF(0),TOF(128),TOF(192),BASE_ALPHA },
  { TOF(128),TOF(128),TOF(192),BASE_ALPHA },
  { TOF(255),TOF(0),TOF(255),BASE_ALPHA },
  { TOF(128),TOF(64),TOF(64),BASE_ALPHA },
  { TOF(255),TOF(128),TOF(64),BASE_ALPHA },
  { TOF(0),TOF(255),TOF(0),BASE_ALPHA },
  { TOF(0),TOF(128),TOF(128),BASE_ALPHA },
  { TOF(0),TOF(64),TOF(128),BASE_ALPHA },
  { TOF(128),TOF(128),TOF(255),BASE_ALPHA },
  { TOF(128),TOF(0),TOF(64),BASE_ALPHA },
  { TOF(255),TOF(0),TOF(128),BASE_ALPHA },
  { TOF(128),TOF(0),TOF(0),BASE_ALPHA },
  { TOF(255),TOF(128),TOF(0),BASE_ALPHA },
  { TOF(0),TOF(128),TOF(0),BASE_ALPHA },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 },
  { 0.5, 0.5, 0.5, 0.5 }
};


void ProfileDrawGraph() {
  int i, j;
  int historyindx;
  float targetY = 500.0f;
  
  if (vg == NULL) {
    return;
  }
  
  if (current_history_index < MAX_HISTORY) {
    current_history_index++;
    profile_index = 0;
    return;
  }

  historyindx = current_history_index % MAX_HISTORY;

  if (frameinfo_histroy[historyindx].cnt == 0) {
    current_history_index++;
    profile_index = 0;
    return;
  }

  float startX = 0.0f;
  float width = 32.0f;

  for (i = 0; i < MAX_HISTORY; i++) {

    float startY = 32;
    for (j = 0; j < frameinfo_histroy[historyindx].cnt; j++) {

      float height = frameinfo_histroy[historyindx].time[j] * targetY / 16666.0f;
      nvgBeginPath(vg);
      nvgRect(vg, startX, startY, width, height);
      nvgFillColor(vg, graph_colorsvg[j]);
      nvgFill(vg);
      startY += height;
    }

    if (i == (MAX_HISTORY - 1)) {
      float textY = 32;
      for (j = 0; j < frameinfo_histroy[historyindx].cnt; j++) {
        char buf[128];
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        sprintf(buf, "%s:%d", frameinfo_histroy[historyindx].label[j], (int)(frameinfo_histroy[historyindx].time[j]));
        nvgText(vg, startX + width, textY, buf, NULL);
        textY += 24;
      }
    }

    startX += width;
    historyindx = (historyindx + 1) % MAX_HISTORY;
  }


  // Draw target line
  nvgBeginPath(vg);
  nvgMoveTo(vg, 0, targetY + 32);
  nvgLineTo(vg, 32 * 32, targetY + 32);
  nvgStrokeColor(vg, nvgRGBA(255, 0, 0, 255));
  nvgStroke(vg);


  // Reset indexs;
  current_history_index++;
  profile_index = 0;

}


/* Text menu: black screen (the pass clears it), title, rows with the label left and the value right, the selected row highlighted, a footer line. */
extern "C" void NanovgVulkanDrawTextMenu(int w, int h, const char * title, const char * const * labels, const char * const * values,
                                         const int * selectable, int count, int selected, const char * footer) {
  if (vg == NULL) return;
  const float pad = w * 0.06f;
  /* Everything scales with the panel height (720 = 1.0) so the small panels (e.g. 480x272) keep readable, fitting rows. */
  float s = h / 720.0f; if (s < 0.5f) s = 0.5f; if (s > 1.5f) s = 1.5f;
  const float rowH = 46.0f * s;
  nvgBeginFrame(vg, w, h, 1.0f);
  nvgFontFace(vg, "sans");

  nvgFontSize(vg, 44.0f * s);
  nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  nvgFillColor(vg, nvgRGBA(255, 200, 80, 255));
  nvgText(vg, pad, h * 0.08f, title, NULL);

  const float top = h * 0.08f + 70.0f * s;
  const float footerY = h - 40.0f * s;
  /* Scrolling window: only the rows that fit between the title and the footer are drawn, the selected row is always inside it, and
     "more" markers show when rows are hidden above or below. */
  int visible = (int)(((footerY - 50.0f * s) - (top - rowH * 0.5f)) / rowH);
  if (visible < 3) visible = 3;
  static int first = 0;
  static char lastTitle[64] = "";
  if (strncmp(lastTitle, title, sizeof(lastTitle) - 1) != 0) { first = 0; strncpy(lastTitle, title, sizeof(lastTitle) - 1); lastTitle[sizeof(lastTitle) - 1] = 0; }
  if (selected <= first && first > 0) first = selected - 1;   /* scrolling up: keep one row of context above the selection */
  if (selected >= first + visible) first = selected - visible + 1;
  if (first > count - visible) first = count - visible;
  if (first < 0) first = 0;
  const int last = (first + visible < count) ? first + visible : count;

  nvgFontSize(vg, 32.0f * s);
  for (int i = first; i < last; i++) {
    const float y = top + (i - first) * rowH;
    if (i == selected) {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, pad - 12.0f, y - rowH * 0.5f + 3.0f, w - 2 * pad + 24.0f, rowH - 6.0f, 6.0f);
      nvgFillColor(vg, nvgRGBA(40, 90, 190, 230));
      nvgFill(vg);
    }
    nvgFillColor(vg, i == selected ? nvgRGBA(255, 255, 255, 255) : (selectable[i] ? nvgRGBA(215, 215, 215, 255) : nvgRGBA(120, 120, 120, 255)));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, pad, y, labels[i], NULL);
    if (values[i] != NULL && values[i][0] != 0) {
      nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
      nvgFillColor(vg, i == selected ? nvgRGBA(255, 240, 160, 255) : nvgRGBA(150, 200, 255, 255));
      nvgText(vg, w - pad, y, values[i], NULL);
    }
  }

  nvgFontSize(vg, 22.0f * s);
  nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  nvgFillColor(vg, nvgRGBA(170, 170, 170, 255));
  nvgText(vg, pad, footerY, footer, NULL);

  /* hidden rows: markers on the right of the title line (above) and just over the footer (below) */
  nvgFontSize(vg, 22.0f * s);
  nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
  nvgFillColor(vg, nvgRGBA(255, 200, 80, 255));
  if (first > 0) nvgText(vg, w - pad, h * 0.08f, "^ more above", NULL);
  if (last < count) nvgText(vg, w - pad, footerY - 26.0f * s, "v more below", NULL);
  nvgEndFrame(vg);
}

extern "C" {

static void OSDNanovgVDisplayMessage(OSDMessage_struct * message, pixel_t * buffer, int w, int h)
{
  int LeftX = 8;
  int Width = 500;
  int TxtY = 12;
  int Height = 13;
  int msglength;
  int vidwidth, vidheight;
  float fontsize = 32.0f;
  int maxlen = 0;
  int i = 0;

  if (vg == NULL) {
    return;
  }

  VIDCore->GetGlSize(&vidwidth, &vidheight);
  

  if (message->type == OSDMSG_FPS) {
    LeftX = 8;
  }
  else if (message->type == OSDMSG_RECORD) {
    LeftX = vidwidth - 8;
  }
  else {
    return;
  }

  Width = vidwidth - 2 * LeftX;


  switch (message->type) {
  case OSDMSG_STATUS:
    TxtY = vidheight - (Height + TxtY);
    break;
  }

  msglength = strlen(message->message);



  nvgBeginFrame(vg, vidwidth, vidheight, 1.0f);


  nvgFontSize(vg, fontsize);
  nvgFontFace(vg, "sans");
  
  if (message->type == OSDMSG_FPS) {

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, 320 + 8, 32);
    nvgFillColor(vg, nvgRGBA(8, 8, 8, 128));
    nvgFill(vg);
    
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, LeftX, TxtY, message->message, NULL);
    TxtY += fontsize;
    fontsize = 16;
    nvgFontSize(vg, fontsize);
    int linecnt = (vidheight - TxtY) / fontsize;
    int start_point = current_log_history_index - linecnt;
    if (start_point < 0) {
      start_point = MAX_LOG_HISTORY + start_point;
    }
    for (i = 0; i < linecnt; i++) {
      nvgText(vg, LeftX, TxtY, log_histroy[start_point], NULL);
      start_point++;
      start_point %= MAX_LOG_HISTORY;
      TxtY += fontsize;
    }
    ProfileDrawGraph();
  }

  if (message->type == OSDMSG_RECORD) {

    nvgBeginPath(vg);
    nvgRect(vg, LeftX - (strlen(message->message)*(fontsize/2)), 0, 320 + 8, 32);
    nvgFillColor(vg, nvgRGBA(8, 8, 8, 128));
    nvgFill(vg);

    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, LeftX, TxtY, message->message, NULL);
  }

  nvgEndFrame(vg);

}

}


#if 0
OSD_struct *OSDCoreList[] = {
  &OSDNnovg,
  &OSDSoft,
  &OSDDummy,
  NULL
};
#endif

