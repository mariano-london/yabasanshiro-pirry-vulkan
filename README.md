# yabasanshiro-pirry-vulkan

Vulkan renderer for [yabasanshiro-pirry](https://github.com/pirrypirrypirry/yabasanshiro-pirry) on RK3566 handhelds (Mali-G52). Test release, version 0.9.0.

dArkOS already ships pirry's OpenGL ES build, you don't need this to play. Grab it only if you want to try Vulkan and report back. Only tested on a PowKiddy RGB30.

## Credits

**pirry** ([pirrypirrypirry](https://github.com/pirrypirrypirry)) made yabasanshiro-pirry, the emulator this runs on: the speed work, the game fixes, multi-disc support, the menu, the docs. This fork only bolts a Vulkan renderer on top. If you don't need Vulkan, go grab [his release](https://github.com/pirrypirrypirry/yabasanshiro-pirry-release) instead.

Also: **devmiyax** (original Yaba Sanshiro + its Vulkan renderer), the Yabause developers, and **dArkOS**.

## Install

```bash
sudo cp yabasanshiro-pirry-vulkan-rk3566-0.9.0 /opt/yabasanshiro/pirri-rk3566-vk
sudo chmod 755 /opt/yabasanshiro/pirri-rk3566-vk
```

dArkOS already has an empty "standalone-pirry-rk3566-vk" slot for Saturn. Pick it per-game in EmulationStation's emulator choice. Nothing of pirry's gets touched.

If that slot is missing, add this under the saturn `<emulators>` list in `/etc/emulationstation/es_systems.cfg`:
```xml
<emulator name="standalone-pirry-rk3566-vk"></emulator>
```

Uninstall: delete `/opt/yabasanshiro/pirri-rk3566-vk`.

## Using it

Select opens the menu. Select+Start quits. Menu: Video, Sound, Controls (rebind buttons), Save/Load state, Reset, Next disc.

## Screen fit (Aspect rate "Original", other panel sizes from a rig, not real hardware)

| Panel | Picture, position |
|---|---|
| 640x480 | 640x480, full |
| 960x720 | 960x720, full |
| 1024x768 | 1024x768, full |
| 1280x720 | 960x720, 160px bars left/right |
| 480x272 | 362x272, 58px bars left/right |
| 720x720 (RGB30) | 720x540, 90px bars top/bottom |

"720p" resolution mode renders at 1280x720 and scales down on every panel: skip it on small screens.

## Known issues

- **NiGHTS into Dreams** crashes the GPU after a while. Works fine on pirry's build. Not fixed yet.
- A few visual glitches in Virtua Fighter 2 and on some loading screens.

## Building from source

```bash
git clone --recursive https://github.com/mariano-london/yabasanshiro-pirry-vulkan.git
cd yabasanshiro-pirry-vulkan && mkdir build && cd build
cmake ../yabause -DCMAKE_BUILD_TYPE=Release -DYAB_PORTS=retro_arena \
  -DYAB_WANT_DYNAREC_DEVMIYAX=ON -DYAB_WANT_VULKAN=ON -DUSE_EGL=ON \
  -DCMAKE_C_FLAGS="-O3 -mcpu=cortex-a55 -fno-strict-aliasing -D_GNU_SOURCE -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion" \
  -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a55 -fno-strict-aliasing -D_GNU_SOURCE" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition"
make -j$(nproc)
```
Needs `libvulkan-dev libshaderc-dev libglm-dev` on top of pirry's usual build deps. **`-mcpu=cortex-a55` is RK3566 only, it will crash on RK3326.**

## License

GPL-2.0-or-later, same as Yaba Sanshiro / Yabause. See `LICENSE`.
