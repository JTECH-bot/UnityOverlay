# UnityOverlay — native ImGui overlay for Unity/IL2CPP

An `eglSwapBuffers` overlay that draws an ImGui menu inside a Unity process,
plus a JSON-driven offsets/config loader. Built as `libmodmenu.so` for
`arm64-v8a`.

## Layout

```
UnityOverlay/
├── CMakeLists.txt          NDK build, arm64-v8a only
├── build.sh                convenience configure + build
├── assets/
│   ├── resolved_offsets.json   offsets for the target build
│   └── mod_config.json         persisted toggle state
└── src/
    ├── main.cpp            EGL hook, ImGui lifecycle, JNI_OnLoad, touch input
    ├── menu.h / menu.cpp   theme + tabbed UI
    └── memory.h            JSON parser, config/offsets types, mem read/write
```

## Build

```bash
export ANDROID_NDK_HOME=$HOME/android-ndk-r27c
cd UnityOverlay
./build.sh
# -> build/lib/arm64-v8a/libmodmenu.so
```

`CMakeLists.txt` refuses to configure without the NDK toolchain and refuses any
ABI other than `arm64-v8a`. ImGui is pulled at `v1.91.4`; xHook at `v1.2.0`,
compiled directly into the target because the upstream repo only ships
`Android.mk` (its `xh_jni.c` is skipped — it defines a second `JNI_OnLoad`).
`src/xhook_compat.h` is force-included into xHook's C files to supply
`PAGE_SIZE`/`PAGE_MASK` at runtime, which keeps the hook correct on 16 KB-page
devices.

## CI

`.github/workflows/build.yml` runs on push, PR and manual dispatch:

1. checks out the repo and finds the project dir (`UnityOverlay/` at repo root,
   or `.` if the project is the repo root),
2. installs NDK r27c via `android-actions/setup-ndk@v3`,
3. configures with the NDK toolchain, `arm64-v8a`, `android-24`, `c++_static`,
4. builds with Ninja and caches `build/_deps`,
5. verifies the artifact with `llvm-readelf` (AArch64, shared object) and
   `llvm-nm` (`JNI_OnLoad` exported),
6. uploads `libmodmenu-arm64-v8a` for 30 days.

Tagging `v*` also attaches `libmodmenu.so` to a GitHub release.

## Wiring it up

1. Load the library before the Unity renderer starts (`System.loadLibrary`,
   or dlopen from your injector).
2. Put `resolved_offsets.json` and `mod_config.json` in the app's `assets/`
   directory and pass the absolute path once:

```java
package com.unityoverlay;

public final class Overlay {
    static { System.loadLibrary("modmenu"); }

    public static native void nativeTouch(int action, float x, float y);
    public static native void nativeToggleMenu();
    public static native void nativeSetAssetsPath(String dir);
}
```

Call `nativeSetAssetsPath(ctx.getFilesDir() + "/assets")` early, then forward
`MotionEvent` samples from your input tap:

```java
switch (e.getActionMasked()) {
    case MotionEvent.ACTION_DOWN:   Overlay.nativeTouch(0, e.getX(), e.getY()); break;
    case MotionEvent.ACTION_MOVE:   Overlay.nativeTouch(1, e.getX(), e.getY()); break;
    case MotionEvent.ACTION_UP:     Overlay.nativeTouch(2, e.getX(), e.getY()); break;
}
```

The installer thread waits for `libEGL.so` to appear in `/proc/self/maps`, then
PLT-hooks `eglSwapBuffers` and refreshes for ~5s so late `dlopen`s get patched.

## JSON schema

`resolved_offsets.json` — flat map or wrapped in an `offsets` object. Values
may be numbers or `"0x..."` strings. Values below `0x10000000` are treated as
offsets from the module base; larger ones are absolute addresses.

| Key | Meaning |
| --- | --- |
| `GameAssembly` | module base, or base offset |
| `EntityList` | pointer array (or inline array) of objects |
| `EntityCount` | `int` holding the live count (optional) |
| `EntityStride` | bytes per slot, default `8` |
| `MaxEntities` | hard cap per frame, default `64` |
| `LocalPlayer` | pointer to the local object |
| `Position` / `Head` | field offsets inside an entity (X/Y/Z floats) |
| `Health` / `Team` / `Name` | field offsets (`Name` is a `char*`) |
| `ViewMatrix` | 16 floats, column-major view-projection |
| `CameraYaw` / `CameraPitch` | floats the aimbot writes (degrees) |
| `CameraYawPtr` | set non-zero to treat the two above as pointers-to-float |
| `HeadHeight` | metres (`1.65`) or centimetres (`165`) |
| `UnlockFlags` | `uint32` bitfield for the unlocker tab |
| `TriggerFlag` | `int` set to `1` while the crosshair is on target |

`mod_config.json`:

```json
{
  "visuals": { "lines": true, "boxes": true, "health": true, "names": true,
               "skeleton": false, "max_distance": 150 },
  "aimbot":  { "enabled": false, "aim_lock": false, "triggerbot": false,
               "fov": 35, "smoothness": 6, "target_head": true },
  "camera":  { "freecam": false, "fov": 60, "timescale": 1.0 },
  "player":  { "noclip": false, "speed_multiplier": 1.0, "infinite_jump": false },
  "menu_open": true
}
```

Anything missing falls back to the defaults in `memory.h`.

## How the ESP renders

`menu::draw_overlay` runs inside the ImGui frame using
`ImGui::GetBackgroundDrawList()`, so nothing touches the GL pipeline directly:

1. `EntityReader::read_all` walks `EntityList` with `EntityStride`, stopping at
   `EntityCount` (or `MaxEntities`), and snapshots position, head, health, team
   and name into a fixed `Entity[128]` buffer.
2. Every field read goes through `mem_read`, which uses `process_vm_readv`
   and falls back to `pread` on `/proc/self/mem`. Unmapped addresses return
   zero instead of crashing the title.
3. `world_to_screen` projects feet and head with `ViewMatrix`; entities behind
   the camera (`w < 0.001`) are dropped.
4. Box height drives width (`0.55x`), the health bar sits left of the box and
   lerps red→green, the name line shows distance, and the skeleton is derived
   proportionally between the projected head and feet points.

## How the aimbot ticks

`menu::update_aimbot` runs once per frame before the UI:

1. Reads current `CameraYaw` / `CameraPitch` (optionally dereferencing a
   pointer when `CameraYawPtr` is set).
2. For each live entity, computes yaw `atan2(dx, dz)` and pitch
   `-asin(dy / len)` from the local eye position (`HeadHeight * 0.85`).
3. Normalises both deltas to `(-180, 180]` and takes the angular distance;
   anything outside `fov` is rejected, and the closest candidate wins.
4. `aim_lock` clamps the per-frame step to at least `0.35`, otherwise the step
   is `1 / smoothness`. The result is written back and pitch is clamped to
   `±89°`.
5. `triggerbot` writes `1` to `TriggerFlag` when the target is within `1.5°`,
   `0` otherwise.

## Applying values to a game

The menu only flips config bits and the aimbot writes camera floats. Anything
engine-specific stays in your per-title glue, driven by `Resolved`:

```cpp
ov::Resolved r; r.resolve(offs);
if (cfg.player.noclip && r.local_player) {
    uintptr_t local = ov::read_mem<uintptr_t>(r.local_player);
    ov::write_mem<float>(local + r.off_pos + 4, 0.f);  // your field, your offset
}
```

Offsets are build-specific; re-resolve them whenever the game updates, then
hit **Reload Config** in the menu.

## Notes

- GL state is saved and restored around the draw, so the title's own rendering
  is untouched.
- `io.IniFilename` is nulled so ImGui never writes into the game directory.
- Font scale is derived from surface height, so the menu is legible on both
  phone and tablet displays.
- Reads are validated with `process_vm_readv` (with a `/proc/self/mem`
  fallback), so a stale offset returns zeros instead of a segfault.
- Verified on host: JSON loader, entity walk, projection and a full ImGui frame
  with ESP geometry (900 vertices from 4 entities). `menu.cpp` and `main.cpp`
  both compile clean.
