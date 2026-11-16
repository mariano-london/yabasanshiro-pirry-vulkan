/*  Wall-clock frame instrumentation for RK3566/RGB30 profiling.

    Opt-in via the YABA_PERF_LOG environment variable (set it to the directory
    the CSVs should be written to). When unset, every entry point below is a
    load of one int and a branch, so a normal build is unaffected.

    This is deliberately NOT the existing frameprofile.cpp mechanism:
      - that one is compiled out unless _VDP_PROFILE_ is defined,
      - it takes a mutex on every one of its 34 call sites, which perturbs the
        very thread scheduling we are trying to observe, and
      - it times with clock(), i.e. process CPU time. A thread blocked on a
        page fault, a vsync or a futex accrues no CPU time, so stalls --
        exactly what we are hunting -- are invisible to it.
    Everything here uses CLOCK_MONOTONIC wall time and lock-free atomics.
*/

#ifndef PERFLOG_H
#define PERFLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero once logging is active. Read directly by the hot paths. */
extern int g_perf_on;

/* rom_path may be NULL; it is only used to name the output files. */
void PerfLogInit(const char *rom_path);
void PerfLogShutdown(void);

/* CLOCK_MONOTONIC, nanoseconds. Signed: callers subtract these. */
long long PerfNowNs(void);

/* Bracket one emulated frame (the YabauseExec() call). */
void PerfFrameBegin(void);
void PerfFrameEnd(void);

/* Accumulated by whichever thread does the work; drained by PerfFrameEnd. */
void PerfAddSwapNs(long long ns);
/* Vulkan renderer phase timers (drained into the frame CSV): slot 0 updateTextureImage (copy + submit + fence), 1 fence waits inside
   YabaVkSubmit, 2 Vdp2DrawStart..Vdp2DrawEnd entry (CPU scene generation), 3 Vdp2DrawEnd total (record + submit + wait), 4 queue-idle waits,
   5 number of queue submits, 6 vkQueueSubmit call time, 7 time from Vdp2DrawEnd entry to the texture update,
   8..13 Vdp2DrawScreens parts: pre (windows, line/back colour), NBG3, NBG2, NBG1, NBG0, RBG0,
   14 render thread busy (between events), 15 render thread idle (blocked waiting for an event), 16 wait for the VDP1 set's previous frame at drawStart,
   17 wait for the VDP2 set's previous scene at Vdp2DrawStart. NOTE: vk_gen_us (slot 2) spans the
   render thread's idle wait for the VBlank-in event, so it overstates CPU work; use 8..13 and 14/15. */
void PerfAddVk(int slot, long long v);
void PerfAddVdpWaitNs(long long ns);

/* Frame limiter. exec_us on its own is useless for load: YabauseExec()
   contains frameSkipAndLimit(), which both nanosleeps AND busy-spins to pace
   to 60Hz, so exec_us sits at the frame period whether the machine is busy or
   idle. Reporting these separately lets work be recovered as
   exec_us - limit_us. */
void PerfAddLimitNs(long long ns);   /* whole wait block: nanosleep + spin */
void PerfAddSpinNs(long long ns);    /* the busy-wait loop alone */

/* How late this frame was at the skip decision (line 1060 of vdp2.cpp).
   Signed: negative means ahead of schedule. This is the direct cause of a
   skip, so it is the number that matters. */
void PerfSetBehindUs(long long us);
void PerfSetSkipDecided(int decided);

/* VDP1 draw commands issued for this frame, to test whether skip-heavy
   seconds coincide with VDP1 work spikes. */
void PerfSetV1Drawn(unsigned int cumulative);

/* ---- subsystem split of work_us ----
   work_us told us the emulation thread uses ~71% of the frame budget but not
   what spends it: SH2, SCSP, SCU, M68K, SMPC, CS2 and the VDP1 command walk
   are all inside YabauseExec(). These are accumulated directly (no function
   call, no atomics) because every one of these call sites runs on the
   emulation thread only, and SH2Exec alone is called thousands of times per
   frame. Drained and zeroed by PerfFrameEnd(). */
extern long long g_perf_sh2m_ns;   /* master SH2 */
extern long long g_perf_sh2s_ns;   /* slave SH2  */
extern long long g_perf_scsp_ns;
extern long long g_perf_scu_ns;
extern long long g_perf_m68k_ns;
extern long long g_perf_smpc_ns;
extern long long g_perf_cs2_ns;
extern long long g_perf_vdp2_ns;   /* NB: contains the frame limiter; the CSV
                                      reports vdp2_us with limit_us removed */
/* Called once per SCANLINE (263x per frame) and previously unmeasured --
   the likeliest home of the 25-30% of work_us that the first subsystem
   split left unaccounted. */
extern long long g_perf_hbin_ns;
extern long long g_perf_hbout_ns;
extern long long g_perf_m68kexec_ns;

/* VDP2 sub-split. vdp2_us turned out not to be pure rendering cost: with
   g_vin_decoupled forced on, Vdp2VBlankIN's body is two joins that BLOCK on
   the render thread (Vdp2JoinPendingVIN, Vdp1JoinPendingRenders) plus a
   little work. Separating them says how much of VDP2's 26.6% is work and how
   much is the emulation thread waiting. */
extern long long g_perf_joinvin_ns;      /* Vdp2JoinPendingVIN     - blocking */
extern long long g_perf_joinrender_ns;   /* Vdp1JoinPendingRenders - blocking */
extern long long g_perf_vdp2in_ns;       /* Vdp2VBlankIN  total */
extern long long g_perf_vdp2out_ns;      /* Vdp2VBlankOUT total */

/* Sinks for PERF_TIME at the two join sites (which are in vdp2.cpp, where the
   accumulators are not directly in scope as lvalues for the macro). */
void PerfAddJoinVinNs(long long ns);
void PerfAddJoinRenderNs(long long ns);

/* VDP1 texture cache. The sprite draw path is:
     if (YglIsCached(...)) { fast path; return; }
     ... Vdp1ReadTexture(...)   <- decodes the Saturn texture, the slow path
   So a call to Vdp1ReadTexture IS a cache miss. Counting calls gives the miss
   count; v1_drawn_d gives the total commands, so the miss RATE falls out
   without touching the three YglIsCached sites.
   Hypothesis under test: the 3.73x spike in the VDP1 walk on heavy frames is
   a burst of cache misses forcing texture decodes. */
void PerfAddTexDecode(long long ns);

/* Render thread, so these need atomics. Vdp1Draw() (the VDP1 command-chain
   walk) runs on the render thread's event loop, NOT inside YabauseExec, so
   it is concurrent with work_us rather than part of it. Reported separately. */
void PerfAddVdp1DrawNs(long long ns);

/* Called from the existing once-per-second latch in vdp2.cpp. Also flushes. */
void PerfLogSecond(int fps, unsigned int skipped, unsigned int vdp1,
                   unsigned int idle_skips, unsigned int d_fields);

#ifdef __cplusplus
}
#endif

/* Timing helper. Evaluates the clock only when logging is on, so the
   disabled case costs one branch.

   PERF_TIME(PerfAddSwapNs, SDL_GL_SwapWindow(wnd));
*/
#define PERF_TIME(sink, stmt)                        \
  do {                                               \
    if (g_perf_on) {                                 \
      long long _perf_t0 = PerfNowNs();              \
      stmt;                                          \
      sink(PerfNowNs() - _perf_t0);                  \
    } else {                                         \
      stmt;                                          \
    }                                                \
  } while (0)

#endif /* PERFLOG_H */
