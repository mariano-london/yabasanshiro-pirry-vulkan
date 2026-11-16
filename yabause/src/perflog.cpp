/*  Wall-clock frame instrumentation for RK3566/RGB30 profiling.
    See perflog.h for why this does not reuse frameprofile.cpp.
*/

#include "perflog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>          /* getpid */
#include <sys/stat.h>
#include <sys/resource.h>
#include <atomic>

extern "C" {

int g_perf_on = 0;

/* Subsystem accumulators. Emulation thread only, so plain variables. */
long long g_perf_sh2m_ns = 0;
long long g_perf_sh2s_ns = 0;
long long g_perf_scsp_ns = 0;
long long g_perf_scu_ns = 0;
long long g_perf_m68k_ns = 0;
long long g_perf_smpc_ns = 0;
long long g_perf_cs2_ns = 0;
long long g_perf_vdp2_ns = 0;
long long g_perf_hbin_ns = 0;
long long g_perf_hbout_ns = 0;
long long g_perf_m68kexec_ns = 0;
long long g_perf_joinvin_ns = 0;
long long g_perf_joinrender_ns = 0;
long long g_perf_vdp2in_ns = 0;
long long g_perf_vdp2out_ns = 0;

/* skipped_frame is a non-static u32 in vdp2.cpp, reset to 0 each second.
   Logged raw: the per-second resets are obvious in the data, and reading it
   raw avoids adding a second place that has to agree about when it resets. */
extern unsigned int skipped_frame;

}

namespace {

FILE *g_frames = NULL;
FILE *g_seconds = NULL;
char *g_frame_buf = NULL;

/* Written by the render thread, drained by the emu thread. Relaxed ordering is
   right here: these are counters whose exact interleaving does not matter, and
   we must not introduce barriers into the paths being measured. */
std::atomic<long long> g_swap_ns(0);
std::atomic<long long> g_vdpwait_ns(0);
std::atomic<long long> g_limit_ns(0);
std::atomic<long long> g_spin_ns(0);
std::atomic<long long> g_vdp1draw_ns(0);
std::atomic<long long> g_texdecode_ns(0);
std::atomic<long long> g_texdecode_n(0);
std::atomic<long long> g_vk_acc[20];

/* Set once per frame on the emu thread, so plain variables are fine. */
long long g_behind_us = 0;
int g_skip_decided = 0;
unsigned int g_v1_cum = 0;
unsigned int g_v1_prev = 0;
int g_v1_primed = 0;

long long g_t_origin = 0;   /* first PerfFrameBegin, for a readable timeline */
long long g_frame_t0 = 0;
unsigned long long g_frame_no = 0;

long  g_minflt_prev = 0;
long  g_majflt_prev = 0;
int   g_rusage_primed = 0;

const size_t FRAME_BUF_BYTES = 256 * 1024;

void sanitize(const char *src, char *dst, size_t n)
{
  size_t j = 0;
  if (!src) src = "unknown";

  /* basename */
  const char *base = strrchr(src, '/');
  base = base ? base + 1 : src;

  for (size_t i = 0; base[i] && j + 1 < n; i++) {
    char c = base[i];
    if (c == '.') break;                       /* drop the extension */
    int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '-' || c == '_';
    dst[j++] = ok ? c : '_';
  }
  if (j == 0) { strncpy(dst, "unknown", n - 1); dst[n - 1] = 0; return; }
  dst[j] = 0;
}

} /* namespace */

extern "C" {

long long PerfNowNs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  /* Signed throughout. An earlier version of this instrumentation computed
     (a.tv_nsec - b.tv_nsec) in unsigned arithmetic, which promoted a
     legitimate negative value across a second boundary into ~UINT64_MAX. */
  return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

void PerfLogInit(const char *rom_path)
{
  const char *dir = getenv("YABA_PERF_LOG");
  if (!dir || !*dir) return;                   /* logging off: the default */

  mkdir(dir, 0775);                            /* ignore EEXIST */

  char name[160];
  sanitize(rom_path, name, sizeof(name));

  /* The pid is in the filename so a second run of the same title cannot
     append into the first run's file and overlay two timelines. */
  char path[512];
  long pid = (long)getpid();

  snprintf(path, sizeof(path), "%s/frames-%s-%ld.csv", dir, name, pid);
  g_frames = fopen(path, "w");
  snprintf(path, sizeof(path), "%s/seconds-%s-%ld.csv", dir, name, pid);
  g_seconds = fopen(path, "w");

  if (!g_frames || !g_seconds) {
    if (g_frames) { fclose(g_frames); g_frames = NULL; }
    if (g_seconds) { fclose(g_seconds); g_seconds = NULL; }
    return;                                    /* stay off rather than crash */
  }

  /* Batch the per-frame writes. Line buffering would mean a write() per frame
     (~60/s); under the memory pressure this build exists to investigate, that
     write can itself block and make the stall worse. Flushed once a second
     from PerfLogSecond, so a hang loses at most the last second. */
  g_frame_buf = (char *)malloc(FRAME_BUF_BYTES);
  if (g_frame_buf) setvbuf(g_frames, g_frame_buf, _IOFBF, FRAME_BUF_BYTES);

  fprintf(g_frames, "# rom=%s\n", rom_path ? rom_path : "unknown");
  fprintf(g_frames,
          "frame,t_ms,exec_us,limit_us,spin_us,work_us,behind_us,skip_decided,"
          "v1_drawn_d,swap_us,vdpwait_us,minflt_d,majflt_d,core,skipped_raw,"
          "sh2m_us,sh2s_us,scsp_us,scu_us,m68k_us,smpc_us,cs2_us,vdp2_us,"
          "hbin_us,hbout_us,m68kexec_us,vdp1draw_us,"
          "joinvin_us,joinrender_us,vdp2in_us,vdp2out_us,"
          "texdecode_us,texdecode_n,"
          "vk_texup_us,vk_fence_us,vk_gen_us,vk_end_us,vk_qidle_us,vk_submits,vk_subcall_us,vk_prep_us,"
          "vk_pre_us,vk_nbg3_us,vk_nbg2_us,vk_nbg1_us,vk_nbg0_us,vk_rbg0_us,"
          "rt_busy_us,rt_idle_us,vk_wv1_us,vk_wscene_us\n");
  fprintf(g_seconds, "# rom=%s\n", rom_path ? rom_path : "unknown");
  fprintf(g_seconds, "t_ms,fps,skipped,vdp1,idle_skips,d_fields,saturation,soc_mC,gpu_mC,cpu_khz,gpu_hz,mem_avail_mb\n");
  fflush(g_seconds);

  g_perf_on = 1;
}

void PerfLogShutdown(void)
{
  if (!g_perf_on) return;
  g_perf_on = 0;
  if (g_frames) { fflush(g_frames); fclose(g_frames); g_frames = NULL; }
  if (g_seconds) { fflush(g_seconds); fclose(g_seconds); g_seconds = NULL; }
  if (g_frame_buf) { free(g_frame_buf); g_frame_buf = NULL; }
}

void PerfAddSwapNs(long long ns)
{
  g_swap_ns.fetch_add(ns, std::memory_order_relaxed);
}

void PerfAddVdpWaitNs(long long ns)
{
  g_vdpwait_ns.fetch_add(ns, std::memory_order_relaxed);
}

void PerfAddLimitNs(long long ns)
{
  g_limit_ns.fetch_add(ns, std::memory_order_relaxed);
}

void PerfAddSpinNs(long long ns)
{
  g_spin_ns.fetch_add(ns, std::memory_order_relaxed);
}

void PerfAddVdp1DrawNs(long long ns)
{
  g_vdp1draw_ns.fetch_add(ns, std::memory_order_relaxed);
}

void PerfAddJoinVinNs(long long ns)
{
  g_perf_joinvin_ns += ns;
}

void PerfAddJoinRenderNs(long long ns)
{
  g_perf_joinrender_ns += ns;
}

/* Called from the render thread (vidogl.c runs under VdpProc), so atomic. */
void PerfAddTexDecode(long long ns)
{
  g_texdecode_ns.fetch_add(ns, std::memory_order_relaxed);
  g_texdecode_n.fetch_add(1, std::memory_order_relaxed);
}

void PerfAddVk(int slot, long long v)
{
  if (slot >= 0 && slot < 20) g_vk_acc[slot].fetch_add(v, std::memory_order_relaxed);
}

void PerfSetBehindUs(long long us)
{
  g_behind_us = us;
}

void PerfSetSkipDecided(int decided)
{
  if (decided) g_skip_decided = 1;
}

void PerfSetV1Drawn(unsigned int cumulative)
{
  g_v1_cum = cumulative;
}

void PerfFrameBegin(void)
{
  if (!g_perf_on) return;
  g_frame_t0 = PerfNowNs();
  if (g_t_origin == 0) g_t_origin = g_frame_t0;
}

void PerfFrameEnd(void)
{
  if (!g_perf_on || !g_frames) return;

  const long long now = PerfNowNs();
  const long long exec_ns = now - g_frame_t0;

  /* Drain the cross-thread accumulators for this frame. */
  const long long swap_ns = g_swap_ns.exchange(0, std::memory_order_relaxed);
  const long long wait_ns = g_vdpwait_ns.exchange(0, std::memory_order_relaxed);
  const long long limit_ns = g_limit_ns.exchange(0, std::memory_order_relaxed);
  const long long spin_ns = g_spin_ns.exchange(0, std::memory_order_relaxed);

  /* Real emulation cost: the frame period minus the deliberate pacing wait.
     Clamped at 0 because the limiter runs inside the measured region and the
     two clocks are read at slightly different points. */
  long long work_ns = exec_ns - limit_ns;
  if (work_ns < 0) work_ns = 0;

  /* VDP1 draw commands this frame. g_v1_drawn is cumulative and is zeroed
     once a second by FPSDisplay(), so a reset shows up as a negative delta;
     report 0 for that frame rather than a huge wrong number. */
  long long v1_d = 0;
  if (g_v1_primed) {
    long long d = (long long)g_v1_cum - (long long)g_v1_prev;
    v1_d = (d < 0) ? 0 : d;
  } else {
    g_v1_primed = 1;
  }
  g_v1_prev = g_v1_cum;

  /* getrusage is one syscall; reading /proc/self/stat would be an
     open+read+close per frame. ru_minflt/ru_majflt are process-cumulative. */
  long d_min = 0, d_maj = 0;
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) == 0) {
    if (g_rusage_primed) {
      d_min = ru.ru_minflt - g_minflt_prev;
      d_maj = ru.ru_majflt - g_majflt_prev;
    } else {
      g_rusage_primed = 1;
    }
    g_minflt_prev = ru.ru_minflt;
    g_majflt_prev = ru.ru_majflt;
  }

  /* The frame limiter runs inside Vdp2VBlankIN(), so g_perf_vdp2_ns contains
     limit_ns. Report VDP2's own cost with the pacing wait removed. */
  long long vdp2_ns = g_perf_vdp2_ns - limit_ns;
  if (vdp2_ns < 0) vdp2_ns = 0;

  fprintf(g_frames,
          "%llu,%lld,%lld,%lld,%lld,%lld,%lld,%d,%lld,%lld,%lld,%ld,%ld,%d,%u,"
          "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,"
          "%lld,%lld,%lld,%lld,"
          "%lld,%lld,%lld,%lld,"
          "%lld,%lld,"
          "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,"
          "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld\n",
          g_frame_no++,
          (now - g_t_origin) / 1000000LL,
          exec_ns / 1000LL,
          limit_ns / 1000LL,
          spin_ns / 1000LL,
          work_ns / 1000LL,
          g_behind_us,
          g_skip_decided,
          v1_d,
          swap_ns / 1000LL,
          wait_ns / 1000LL,
          d_min, d_maj,
          sched_getcpu(),
          skipped_frame,
          g_perf_sh2m_ns / 1000LL,
          g_perf_sh2s_ns / 1000LL,
          g_perf_scsp_ns / 1000LL,
          g_perf_scu_ns / 1000LL,
          g_perf_m68k_ns / 1000LL,
          g_perf_smpc_ns / 1000LL,
          g_perf_cs2_ns / 1000LL,
          vdp2_ns / 1000LL,
          g_perf_hbin_ns / 1000LL,
          g_perf_hbout_ns / 1000LL,
          g_perf_m68kexec_ns / 1000LL,
          g_vdp1draw_ns.exchange(0, std::memory_order_relaxed) / 1000LL,
          g_perf_joinvin_ns / 1000LL,
          g_perf_joinrender_ns / 1000LL,
          g_perf_vdp2in_ns / 1000LL,
          g_perf_vdp2out_ns / 1000LL,
          g_texdecode_ns.exchange(0, std::memory_order_relaxed) / 1000LL,
          g_texdecode_n.exchange(0, std::memory_order_relaxed),
          g_vk_acc[0].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[1].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[2].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[3].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[4].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[5].exchange(0, std::memory_order_relaxed),
          g_vk_acc[6].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[7].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[8].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[9].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[10].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[11].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[12].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[13].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[14].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[15].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[16].exchange(0, std::memory_order_relaxed) / 1000LL,
          g_vk_acc[17].exchange(0, std::memory_order_relaxed) / 1000LL);

  g_behind_us = 0;
  g_skip_decided = 0;
  g_perf_sh2m_ns = g_perf_sh2s_ns = g_perf_scsp_ns = g_perf_scu_ns = 0;
  g_perf_m68k_ns = g_perf_smpc_ns = g_perf_cs2_ns = g_perf_vdp2_ns = 0;
  g_perf_hbin_ns = g_perf_hbout_ns = g_perf_m68kexec_ns = 0;
  g_perf_joinvin_ns = g_perf_joinrender_ns = 0;
  g_perf_vdp2in_ns = g_perf_vdp2out_ns = 0;
}

/* One number from a sysfs file (temperature in milli-degrees, frequency in kHz or Hz); -1 if the file is missing or unreadable. */
static long PerfReadSysLong(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  long v = -1;
  if (fscanf(f, "%ld", &v) != 1) v = -1;
  fclose(f);
  return v;
}

/* MemAvailable from /proc/meminfo in MB; -1 if unreadable. */
static long PerfReadMemAvailMb(void)
{
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f) return -1;
  char line[128];
  long kb = -1;
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
    kb = -1;
  }
  fclose(f);
  return kb < 0 ? -1 : kb / 1024;
}

void PerfLogSecond(int fps, unsigned int skipped, unsigned int vdp1,
                   unsigned int idle_skips, unsigned int d_fields)
{
  if (!g_perf_on || !g_seconds) return;

  /* Same definition the adaptive first-slice rule uses in vdp2.cpp:
     526 = 2 time slices per line x 263 lines. */
  const double saturation =
      d_fields ? (double)idle_skips / (526.0 * (double)d_fields) : 1.0;

  /* Thermal / clock / memory columns (appended at the end so older readers keep working). -1 = not readable on this machine. */
  const long soc_mc  = PerfReadSysLong("/sys/class/thermal/thermal_zone0/temp");
  const long gpu_mc  = PerfReadSysLong("/sys/class/thermal/thermal_zone1/temp");
  const long cpu_khz = PerfReadSysLong("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
  long gpu_hz = PerfReadSysLong("/sys/class/devfreq/fde60000.gpu/cur_freq");
  const long mem_av_mb = PerfReadMemAvailMb();

  fprintf(g_seconds, "%lld,%d,%u,%u,%u,%u,%.4f,%ld,%ld,%ld,%ld,%ld\n",
          (PerfNowNs() - g_t_origin) / 1000000LL,
          fps, skipped, vdp1, idle_skips, d_fields, saturation,
          soc_mc, gpu_mc, cpu_khz, gpu_hz, mem_av_mb);
  fflush(g_seconds);

  if (g_frames) fflush(g_frames);   /* batched frame rows land once a second */
}

} /* extern "C" */
