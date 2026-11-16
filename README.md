# yabasanshiro-pirry-vulkan

A **Vulkan** version of [yabasanshiro-pirry](https://github.com/pirrypirrypirry/yabasanshiro-pirry) (Sega Saturn emulator, Yaba Sanshiro) **for RK3566 handhelds**
(Cortex-A55, Mali-G52). dArkOS already ships pirry's OpenGL ES build; this is the Vulkan alternative. Version **0.9.0**, a test release (pirry base 1.6.3).

It was developed and tested on **one device: the PowKiddy RGB30** (720x720 screen, 1 GB RAM) running dArkOS 08272026.
Other RK3566 handhelds have the same chip but have not been tried on real hardware. It does nothing on RK3326 devices
(R36S and similar), which have no Vulkan path.

## Credits

This project would not exist without the work of others.

- **pirry ([pirrypirrypirry](https://github.com/pirrypirrypirry))**: author of yabasanshiro-pirry, the source this fork is built on
  (SH-2 dynarec work, game fixes, multi-disc support, the in-game menu, and the documentation). Everything here is a layer on top of it.
- **devmiyax** ([devmiyax/yabause](https://github.com/devmiyax/yabause)): author of Yaba Sanshiro and of its Vulkan renderer,
  which this fork works on.
- **The Yabause developers**, on whose emulator Yaba Sanshiro is built.
- **dArkOS ([christianhaitian](https://github.com/christianhaitian/dArkOS))**: the OS this is developed and tested on, including its Vulkan support for RK3566.

## Download

The binary of this fork is on the **Releases** page of this repository: [Releases](../../releases). The release has the binary
`yabasanshiro-pirry-vulkan-rk3566-0.9.0`, `SHA256SUMS`, `INSTALL.txt` and `TESTING.txt`.
The install below keeps a backup of the emulator that comes with dArkOS, so you can go back at any time.

### Requirements
- An RK3566 handheld (Mali-G52) running **dArkOS with Vulkan enabled for RK3566** (dArkOS added it in April 2026; tested on 08272026, Debian 13, glibc 2.41).
- Your own Saturn games (`.chd`, `.cue` or `.m3u` for multi-disc).

### Install (replaces one file, reversible)
```bash
sudo cp /opt/yabasanshiro/yabasanshiro /opt/yabasanshiro/yabasanshiro.original
sudo cp yabasanshiro-pirry-vulkan-rk3566-0.9.0 /opt/yabasanshiro/yabasanshiro
sudo chmod 755 /opt/yabasanshiro/yabasanshiro
/opt/yabasanshiro/yabasanshiro --version     # must print: yabasanshiro-pirry-vulkan 0.9.0 (pirry base 1.6.3)
```
Then restart EmulationStation and launch a Saturn game with the standalone Yaba Sanshiro emulator. To go back:
`sudo cp /opt/yabasanshiro/yabasanshiro.original /opt/yabasanshiro/yabasanshiro`.
These steps come from reading dArkOS's own scripts; they have not been tried on a stock dArkOS install.

### Using it
- **Select** (press and release) opens the menu. **Select + Start** quits the emulator.
- The menu is text only: Video (resolution, aspect rate, rotating-background resolution, FPS overlay, frame skip), Sound, Controls
  (rebind the Saturn buttons for player 1), Save/Load state, Reset, Next disc, Exit. It scrolls and scales with the screen height.
- Compared with the OpenGL menu it has no box art or image views.

## What was measured (RGB30, Vulkan, Resolution "Original")

All numbers below were measured on builds based on pirry 1.6.2; this release is based on 1.6.3 (the GL and core changes of 1.6.3 have not been re-measured with the Vulkan renderer).

Method: the emulator writes a per-second CSV (frames presented in that second). "Mean" is the average of that column after the first 2 seconds;
"under 58" is the share of seconds below 58 fps. All runs on the same RGB30, EmulationStation restarted before the run unless noted.

**Virtua Fighter 2 (heavy 3D), the case this work was aimed at**

| Build | Run length | Mean fps | Seconds under 58 fps |
|---|---|---|---|
| Vulkan renderer before this work | 553 s | 40.7 | 84% |
| This fork's renderer (two frame sets, deferred fences), pirry 1.6.2 base | 586 s | 58.8 | 7.2% (lowest second: 54) |
| This release (0.9.0, pirry 1.6.3 base), Native resolution | 817 s | 58.7 | 6.5% (lowest second: 48) |

**Sega Rally Championship Plus** was already close to 60: mean 58.8 to 58.9 over 4 to 9 minutes, 1.6% to 3% of seconds under 58 (this release at Native: 501 s, 58.9 fps, 2.6% under 58).

**Burning Rangers** (heavier, this release, Native): 828 s, mean 56.3 fps, 34% of seconds under 58, 11% under 50, lowest second 39.

**Panzer Dragoon** (this release, Native): 506 s, mean 58.8 fps, 1.0% of seconds under 58, one 2.2 s freeze at 449 s. **Sonic R**: 1707 s (28 minutes), mean 59.0 fps, 0.7% under 58.

**Heat.** In the 817 s Virtua Fighter 2 run the SoC reached 85.6 C and the clocks dropped for 66 seconds (CPU to 1.8 or 1.6 GHz, GPU to 700 or 600 MHz): 57.8 fps while throttled against 58.8 fps unthrottled, so about one fps. The handheld was charging in some of these runs; charging adds heat and I did not record the charging state per run.

**Free play, back to back, several titles in one session (not a clean benchmark; includes start-up seconds)**

| Game | Run | Mean fps |
|---|---|---|
| Manx TT SuperBike | 316 s | 58.0 |
| Need for Speed (Road & Track) | 275 s | 56.4 |
| Virtua Fighter 2 | 305 s | 55.1 |
| Sega Rally Championship Plus | 371 s | 47.6 |
| Sega Touring Car Championship | 114 s | 35.3 |

VF2 and Sega Rally ran slower in that back-to-back session than in the clean runs above (55.1 and 47.6 against 58.8 and 58.9). The cause is not known:
memory pressure on a 1 GB device and heat are suspects, neither is proven. Short demo runs of Battle Garegga and Manx TT (about 3 minutes each) gave 58.8 to 59.0 fps.

"Fps" is how often the emulator presents a frame. Some games update the picture slower on their own: the Need for Speed demo draws about 15 VDP1 frames
a second, and it is slow on pirry's OpenGL build too.

These numbers contain no OpenGL-versus-Vulkan comparison on the same game. If you can compare with the OpenGL build that dArkOS ships, that is exactly the report I am looking for.

Temperature in the longest NiGHTS runs: SoC 60 to 75 C over 3 minutes at 59 fps, CPU 1.99 GHz and GPU 800 MHz the whole time (no throttling seen).

## Running at "Native" resolution

Native renders the picture at the fitted screen size (720x540 on the RGB30) instead of the Saturn's own 320x224 or 352x224, so the image is sharper and the
GPU does not have to stretch a tiny picture. In my one logged Vulkan run at Native, Need for Speed ran **517 s at a mean of 59.0 fps with 0.8% of seconds under 58**
(demo, menus, loading screens and racing), which is the same as the Original-resolution runs of that game. When I first switched to Native by hand it felt as smooth as Original.

That is one title. I have **not** measured Native on the heavy 3D titles (Virtua Fighter 2, Daytona USA) with this renderer, so do not assume it holds there.
On pirry's OpenGL build (a different renderer, measured separately on 10 November) Native stayed close to Original on Sega Rally, Daytona and Virtua Fighter 2,
while 2x was clearly worse on Virtua Fighter 2. To try it: Select, Video, Resolution. The change applies at once.

## The RGB30 screen quirk (square 720x720)

The RGB30's panel is square and the Saturn picture is 4:3. At the default Aspect rate the picture is **720x540, centred, with 90 px black bars at the top and bottom**.
Resolution mode "Native" renders at that fitted size; "Original" renders at the Saturn's own resolution (320x224 or 352x224) and the GPU scales it.
The Vulkan core inherited a bug on square and portrait panels (a picture 960 px wide, offset 154 px off the left edge, and a negative scissor
rectangle, which Vulkan rejects). pirry had already fixed this in the OpenGL core and the fix was ported here.

Other panel sizes were only checked in a software rig (geometry from the log, no screenshots, no real hardware):

| Panel | Picture size and position |
|---|---|
| 640x480 | 640x480, full screen |
| 960x720 | 960x720, full screen |
| 1024x768 | 1024x768, full screen |
| 1280x720 | 960x720, 160 px bars left and right |
| 480x272 | 362x272, 58 px bars left and right |
| 720x720 | 720x540, 90 px bars top and bottom |

The window uses the display's current mode. Resolution mode "720p" renders at 1280x720 and scales down on every panel, which wastes GPU time on small screens.

## Known problems

- **NiGHTS into Dreams crashes the GPU** on the RGB30 after 30 s to 4 minutes (the Mali reports a read fault, then the Vulkan device is lost and the emulator exits).
  It works on pirry's OpenGL build. Descriptor layouts, buffer bounds, heat and memory were checked and are not the cause. The log of a crash ends with the last 8 frames.
  The cause is not found.
- Some visual artifacts in Virtua Fighter 2 gameplay, and on a few loading and transition screens (the Need for Speed intro and loading screens showed them).
- Validation messages remain on a few titles (Darius Gaiden, Die Hard Arcade and others). None has been seen to cause a visible problem.
- After a crash EmulationStation sometimes needs a second restart to come back.

## What changed compared with pirry's tree (everything is in `yabause/src/`)

- **Vulkan renderer:** two alternating sets of per-frame resources for the VDP2 scene and the VDP1 renderer, fence waits deferred by two frames, all queue access
  behind one mutex; merged texture and vertex uploads; blank-tile skipping.
- **Correctness:** the descriptor layout of the VDP1 framebuffer-feedback pipelines (about 2000 validation errors per run on NiGHTS reduced to 0), a double
  buffer unmap on frames with the display off, a thread race on the colour RAM table, `robustBufferAccess` enabled when the GPU supports it (`YABA_VK_ROBUST=0` turns it off).
- **pirry 1.6.3 base:** one of pirry's 1.6.3 changes (VDP1 framebuffer reads without the VRAM lock) is only safe for his OpenGL core. The Vulkan core's framebuffer read does GPU work, and with the lock removed Burning Rangers crashed with a segmentation fault at the same scene (145 s) twice. This fork keeps the lock for the Vulkan core (`vdp1.cpp`); with that, the same game ran 828 s.
- **Menu:** the text menu above (the OpenGL build keeps pirry's menu).
- **Diagnostics:** a per-second and per-frame CSV (frame-time breakdown, SoC/GPU temperature, CPU/GPU clocks, free memory) and a flight recorder that prints the
  last 8 frames when the GPU fails or an uncaught exception ends the process. To keep the files for a bug report: `touch ~/.yabasanshiro/ENABLE_LOG`, play, then
  collect `~/.yabasanshiro/logs/` (`yabasanshiro-pirry-vulkan.log`, `seconds-*.csv`, `frames-*.csv`).

## Building

The emulator is built natively as an **aarch64 Linux** program, on Debian 13 (trixie) arm64 (the released binary was built that way, with GCC 14).

1. Dependencies (pirry's list plus the Vulkan packages):
```bash
sudo apt install build-essential cmake git pkg-config \
    libsdl2-dev libgles-dev libegl-dev libgbm-dev libdrm-dev libudev-dev \
    libasound2-dev libopenal-dev libpng-dev zlib1g-dev \
    libvulkan-dev libshaderc-dev libglm-dev
```
2. Source (`--recursive` is required, it fetches the Eigen submodule used by the menu code):
```bash
git clone --recursive https://github.com/mariano-london/yabasanshiro-pirry-vulkan.git
cd yabasanshiro-pirry-vulkan
```
CMake also downloads `libchdr`, `zlib` and `nlohmann/json` from GitHub and `libpng` from SourceForge, so the build machine needs internet access.

3. Configure (the RK3566 recipe with the Vulkan renderer switched on):
```bash
mkdir build && cd build
cmake ../yabause \
  -DCMAKE_BUILD_TYPE=Release \
  -DYAB_PORTS=retro_arena \
  -DYAB_WANT_DYNAREC_DEVMIYAX=ON \
  -DYAB_WANT_VULKAN=ON \
  -DUSE_EGL=ON \
  -DCMAKE_C_FLAGS="-O3 -mcpu=cortex-a55 -fno-strict-aliasing -D_GNU_SOURCE -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion" \
  -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a55 -fno-strict-aliasing -D_GNU_SOURCE" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition"
```

| Option | Why |
|---|---|
| `-DYAB_WANT_VULKAN=ON` | builds the Vulkan renderer (pirry's recipe has it OFF) |
| `-DUSE_EGL=ON`, `-DYAB_WANT_DYNAREC_DEVMIYAX=ON`, the `-Wno-` flags, `--allow-multiple-definition` | as in pirry's README below |
| `-mcpu=cortex-a55` | tunes for RK3566. **A Cortex-A55 build can crash on RK3326 (Cortex-A35): build it for RK3566 only.** |

4. Build and install:
```bash
make -j$(nproc)
strip -o yabasanshiro-pirry-vulkan src/retro_arena/yabasanshiro
```
The result is `build/yabasanshiro-pirry-vulkan`; install it as in "Install" above. No arm64 machine? Use the arm64 chroot method in pirry's README below, with the packages above.
The released binary was built from the tagged source of the release (the GPL source offer: tag `v0.9.0`).

## License

GNU General Public License, version 2 (or later, as in pirry's tree), the same as Yaba Sanshiro. See `LICENSE`. This fork keeps all upstream copyright and
third-party notices (`THIRD-PARTY-NOTICES.md`).

---

# Upstream README (pirry)

Everything below is the original README of pirry, minus its first paragraph, which points to his separate OpenGL release for RK3326. It describes his build; the Vulkan fork and its downloads are described above.


## What this is

This tree is based on the `pi4` branch of
[devmiyax/yabause](https://github.com/devmiyax/yabause) (Yaba Sanshiro 2,
`retro_arena` port, commit `f6f41dd6`). On top of it come the changes of
yabasanshiro-pirry: speed work in the SH-2 dynarec and the video code, fixes
for games that froze, crashed or didn't boot, graphics and sound fixes,
multi-disc support via `.m3u`, and the extended in-game menu.

Every fix applies to all games — there are no per-game hacks and no hidden
runtime switches. What you build here is exactly the released emulator.

Current version: **1.6.3**

## Building

The emulator is built as a native **aarch64 Linux** program. The release
binaries are built on **Debian 13 (trixie), arm64** with GCC 14 and CMake 3.31.
The resulting binary needs **glibc 2.38 or newer** on the handheld (dArkOS).

### 1. Install the build dependencies

```bash
sudo apt install build-essential cmake git pkg-config \
    libsdl2-dev libgles-dev libegl-dev libgbm-dev libdrm-dev libudev-dev \
    libasound2-dev libopenal-dev libpng-dev zlib1g-dev
```

### 2. Get the source

```bash
git clone --recursive https://github.com/pirrypirrypirry/yabasanshiro-pirry.git
cd yabasanshiro-pirry
```

`--recursive` is required — it fetches the Eigen submodule used by the menu.
If you already cloned without it: `git submodule update --init --recursive`.

During the build CMake also downloads `libchdr`, `zlib` and `nlohmann/json`
from GitHub and `libpng` from SourceForge, so the build machine needs internet
access.

### 3. Configure

```bash
mkdir build && cd build
cmake ../yabause \
  -DCMAKE_BUILD_TYPE=Release \
  -DYAB_PORTS=retro_arena \
  -DYAB_WANT_DYNAREC_DEVMIYAX=ON \
  -DYAB_WANT_VULKAN=OFF \
  -DUSE_EGL=ON \
  -DCMAKE_C_FLAGS="-O3 -mcpu=cortex-a35 -fno-strict-aliasing -D_GNU_SOURCE -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion" \
  -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a35 -fno-strict-aliasing -D_GNU_SOURCE" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition"
```

Every option matters:

| Option | Why |
|---|---|
| `-DUSE_EGL=ON` | Without it the shaders are built for desktop OpenGL and the emulator fails with *"Cannot initialize shader"* on the handheld |
| `-DYAB_WANT_DYNAREC_DEVMIYAX=ON` | The SH-2 dynarec — most of the speed comes from it |
| `-mcpu=cortex-a35` | Tunes the code for the RK3326 |
| `-Wno-…` flags | Parts of the old C code don't build with GCC 14 otherwise |
| `--allow-multiple-definition` | `nvgCreateGLES3` is defined twice in the bundled nanovg/nanogui code |

### 4. Build

```bash
make -j$(nproc)
strip -o yabasanshiro-pirry src/retro_arena/yabasanshiro
```

The finished emulator is `build/yabasanshiro-pirry`. Install it as
described on the [release page](https://github.com/pirrypirrypirry/yabasanshiro-pirry-release#installation).

### Optional: build exactly like the release (LTO)

The release binary additionally uses link-time optimization. Add `-flto=4 -fno-fat-lto-objects`
to both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`, and pass:

```bash
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition -flto=4 -O3 -mcpu=cortex-a35" \
  -DCMAKE_AR=/usr/bin/gcc-ar -DCMAKE_RANLIB=/usr/bin/gcc-ranlib
```

### Building on an x86 PC (arm64 chroot)

No arm64 machine at hand? An arm64 Debian chroot with QEMU user emulation
works fine — it's how the releases are built. Expect a full build to take
around 15 minutes.

```bash
sudo apt install debootstrap qemu-user-static binfmt-support
sudo debootstrap --arch=arm64 trixie /srv/trixie-arm64 http://deb.debian.org/debian
sudo cp /etc/resolv.conf /srv/trixie-arm64/etc/
sudo chroot /srv/trixie-arm64 /bin/bash
# inside the chroot: steps 1–4 above
```

 ### Building for RK3566 (Cortex-A55)

  This project is developed and tested on RK3326 (Cortex-A35) only. An RK3326 build
  also runs on RK3566 handhelds. To tune the build for that chip instead, replace
  `-mcpu=cortex-a35` with `-mcpu=cortex-a55` in `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`
  (step 3), and in `CMAKE_EXE_LINKER_FLAGS` if you use the LTO build above.

  **A Cortex-A55 build is for RK3566 only.** It can use CPU instructions that the
  Cortex-A35 does not have, so expect illegal-instruction crashes on RK3326 devices.

  We don't test or publish RK3566 builds. A contributor reports that it runs on a
  Powkiddy RGB30 with dArkOS and saw a small gain in one title, see
  [PR #3](https://github.com/pirrypirrypirry/yabasanshiro-pirry/pull/3).


## License

GPL-2.0-or-later, like Yaba Sanshiro / Yabause — see [LICENSE](LICENSE).
The included third-party components and their licenses are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Yaba Sanshiro is developed by devMiyax and builds on Yabause by the Yabause
team. All credit for the emulator itself goes to them; this repository only
adds the R36S work on top.
