# CLAUDE.md

Notes for working on this repository. Read `README.md` and the per-platform
READMEs (`mac/README.md`, `ios/README.md`, …) for the full build instructions.

## What this is

A native port of the PC game **OutRun 2006 Coast 2 Coast** (Steam version).
The game code is a reconstruction of the PC `OR2006C2C.EXE` in C++, partly
written by hand and partly machine-translated from x86 (`*_tr.cpp`,
`src/platform/bulk_tr.cpp`). Platform layers exist for Switch, macOS, PS5,
Xbox and (added in this fork) **iOS / iPadOS**.

The repository contains **no game data**. The player supplies their own Steam
install. The EXE is read at first launch, checked against SHA-256 ranges, and
cached as `OR2006C2C.cache` next to the game files.

Upstream is `r4dius/outrun2-decomp` (remote `upstream`); this fork is
`kristenadd/outrun2-decomp` (remote `origin`). The iOS work lives on the
`ios-port` branch.

The reconstruction is **unfinished**: some menus, effects and game paths are
not ported yet and are deliberately "fail-closed" (latched or counted rather
than guessed). Not every bug is a porting bug.

## Layout

| Path | What |
| --- | --- |
| `src/driving`, `src/platform` | reconstructed game code (`*_tr.cpp` / `bulk_tr.cpp`: x86-translated) |
| `src/runtime/game_host.cpp` | shared game runner (frame loop, audio mixer thread, autoplay / replay) |
| `src/system` | files, EXE image loading / cache, first-launch setup screen |
| `src/input` | DualSense-layout pad adapter shared by Mac, iOS and PS5 |
| `src/enhancements` | port-only Options ▸ Settings rows (aspect, resolution, AA, frame rate) |
| `mac/source` | macOS platform: Metal D3D9 device (`metal_d3d9.mm`), SDL display / audio, `main.cpp` |
| `ios/` | iOS / iPadOS platform (reuses `mac/source`; see below) |
| `tests/` | CTest suite (many tests need the game files) |
| `tools/` | EXE tools, x86 translator, audits (`decomp_empty_services.py`) |

### iOS port (`ios/`)

Reuses the Mac game: `mac/source/main.cpp`, `renderer.cpp`, `metal_d3d9.mm`,
`sdl_backend.cpp`, `audio.cpp`, `setup_display.cpp`. Only these are iOS-specific:

- `ios/source/ios_paths.mm`: the game folder is the app's **Documents** (Files
  app / Finder file sharing), or one folder inside it. Saves go to
  `Library/Application Support/OutRun/Saves`.
- `ios/source/ios_screens.cpp`: "Game files not found" and "Connect a game
  controller" notices (drawn with the setup screen). Also the
  background/foreground hold.
- `ios/source/ios_scene.mm`: **UIScene life cycle** (required by the iOS 27
  SDK, or iOS kills the app at launch). SDL2 predates scenes, so each SDL
  window is moved into the app's window scene.
- `ios/source/platform_api.cpp`: pad input. A game controller is
  **required**; the game holds (frozen frame, notice) while none is connected.
- `ios/source/display_rate.mm`: screen max FPS (120 on ProMotion).
- `ios/Info.plist.in`: landscape, full screen, file sharing, scene manifest,
  controller keys.
- Shared Mac files have small `#ifdef OR2_IOS` blocks (fullscreen windows).

## Building

macOS (Apple silicon):

```bash
sh mac/tools/build-dependencies.sh
cmake --preset mac-full
cmake --build --preset mac-full      # -> build/macos/mac/OutRunMacGame.app
```

iOS dependencies (SDL2, FFmpeg, Ogg, Vorbis for device and simulator):

```bash
sh ios/tools/build-dependencies.sh
```

iOS presets (`CMakePresets.json`):

- `ios-simulator`: Makefiles; app at `build/ios-simulator/ios/OutRun.app`.
- `ios-device`: Xcode project at `build/ios-device/OutRunSwitch_r155.xcodeproj`,
  one scheme **OutRunIOS**, **Release only** (Debug is far too slow), GPU
  validation / frame capture off. Needs `-DOR2_IOS_TEAM=<team id>` and
  `-DOR2_IOS_BUNDLE_ID=<unique id>` on first configure (cached afterwards; the
  owner's values are already in the local cache; never commit them).
- `ios-device-check`: unsigned device build, compile check only.

Only the `OutRunIOS` target matters for iOS. The top-level CMake also defines
many host test targets; don't build them for iOS.

## Running on iOS

The owner's devices are an iPhone 15 Pro Max and an iPad mini 6 (both iOS 27).
List them with `xcrun devicectl list devices`. Build, install and launch
without Xcode:

```bash
cd build/ios-device
xcodebuild -project OutRunSwitch_r155.xcodeproj -scheme OutRunIOS \
  -destination "id=<UDID>" -allowProvisioningUpdates -derivedDataPath <tmp> build
xcrun devicectl device install app --device <UDID> ios/Release-iphoneos/OutRun.app
xcrun devicectl device process launch --console --terminate-existing \
  --environment-variables '{"OR2_TRACE":"1"}' --device <UDID> <bundle id>
```

- Installing keeps the game files and saves on the device.
- `--console` streams stdout/stderr; run it in the background and read the log.
- Launch fails if the device is **locked** or the cable connection dropped.
- Pull files (logs, saves, crash reports) with `xcrun devicectl device copy from`
  (`--domain-type appDataContainer` or `systemCrashLogs`).
- Running from Xcode attaches the debugger, which is slower. Time things with
  the app started from its icon.

Simulator: install the app, symlink the Mac game folder into the app
container's `Documents/`, and launch with `xcrun simctl launch`. The
simulator has a virtual "Gamepad" controller, so the controller gate never
shows there. `SIMCTL_CHILD_OR2_IOS_ALLOW_NO_CONTROLLER=1` waives the gate.
iOS 26 simulators do **not** enforce the UIScene requirement; test launches
on an iOS 27 runtime.

## Controls

Keyboard (Mac and iPad): **Enter / Space = select (Cross)**, Backspace /
Delete = back (Circle), **P = Start**, arrows = steer / menus, W / S =
accelerate / brake. Controller: A = select, B = back.

In Options ▸ Settings, **Left/Right changes a value**, and A or Start saves
and leaves (that's the original game's design, not a bug).

## Diagnostics (environment variables)

- `OR2_TRACE=1`: device diagnostics on stderr: sound bank loads / unloads /
  effects (`[snd …]`, with a call stack on unload-all), slow frames, menu
  failures, passenger voice / comment gates (`[pas]`, `[cmt]`), robot fault
  latches (`[rob] latched: …`), controller buttons (`[pad]`, iOS).
  `OR2_TRACE_STACKS=1` adds stacks for passenger effects and stop-all.
- `OR2_HOST_LIVE=1`: live frame profiler every 300 race frames, with
  GPU time, CPU record time and fence wait.
- `OR2_METAL_SYNC=1`: turns off pipelined frames (see below).
- `OR2_DEBUG_RACE_END=<seconds>`: testing only. Once the race clock (from GO)
  passes `<seconds>`, the player car "crosses the goal" (the goal flag that
  `4513C0` sets at the goal line), so the race ends through the game's own
  goal path (`450170`, modes 19 / 33, ending 24, results 25, name entry 26).
  Tests the race-end screens without driving a route. Logs `[dbg]` under
  `OR2_TRACE`. It fires on the current stage (not stage 5), so there's no
  stage change and the results only hold the stages driven. Bugs that need a
  stage change still need a real race. `OR2_HOST_GOAL_COURSE=10..14` picks the ending.
- `OR2_IOS_ALLOW_NO_CONTROLLER=1`: skips the iOS controller gate (testing only).
- `OR2_MAC_SCREENSHOT_DIR` / `OR2_MAC_SCREENSHOT_FRAME`: dump frames (Mac).
  Frontend menus come out **blank white** in these dumps; use the simulator's
  `xcrun simctl io <id> screenshot` or ask the user instead.

Every robot (driver, flag man, passenger…) has a **fault latch**: the first
unmapped address or unported service switches that robot off for the rest of
the race, silently unless `OR2_TRACE` is set. Check for `[rob] latched` first
when a race character stops doing something mid-race.

## Testing

```bash
cmake --preset mac-full -DOR2_EXE="<game folder>/OR2006C2C.cache"
ctest --test-dir build/macos -j 8
```

- **27 tests fail on this Mac even on the untouched baseline**: they need
  game data in other layouts (Switch runtime data, Linux paths).
  Compare failure **lists** before and after a change, not just the count.
- `no_silent_service_holes` (the `tools/decomp_empty_services.py` audit) fails
  if new code skips a service callback silently. Count the miss instead
  (e.g. `if(!f.service||!f.service(...))++counter;`).
- The Metal tests (`ctest -L metal`) include a pixel comparison against the
  software D3D9 reference.

## Reading the original code

The Steam EXE on disk is packed. Use the unpacked image in the game folder's
`OR2006C2C.cache`: a 64-byte header (base at +12, size +16, packed size +20),
then zlib data mapping `0x400000..0x990000`. To disassemble, wrap the bytes
with `.incbin` in an i386 assembly file, build it with
`clang -target i386-linux-gnu -c`, then run
`objdump -d --x86-asm-syntax=intel --adjust-vma=<address>` (`llvm-mc` isn't
installed). Sound bank names, sound ids and rates are in tables in this image
(banks `0x6242E8`, ids `0x7763E0`). Sound `.PAK` files are
`{u32 id, u32 size, 16-bit mono PCM}` until id `0xFFFFFFFF`, at each id's own
sample rate.

## How to work with the owner

- **The owner tests on real devices and is the judge.** Autoplay scripts
  (`autoplay.txt`) and input replays (`replay.txt`) do **not** reliably
  reproduce their menu paths: menu timing depends on wall-clock movies and
  loads. Don't claim something works from autoplay alone; ask them to try it.
- Mac game windows launched for tests grab keyboard focus. The owner may be
  typing into them, so either ask them not to touch the windows or treat the
  results as contaminated.
- When they report a regression ("it used to work"), believe it and bisect
  against a clean build of the previous commit (`git worktree add`, and point
  `CMAKE_PREFIX_PATH` at `build/dependencies`). Remove the worktree afterwards.
- Commit or push only when asked. They push from GitHub Desktop (the CLI's
  saved HTTPS credential lacks write access).

## Outstanding issues

1. **Passenger reactions during big crashes** (ORC 345–365, air/landing
   voices): tied to the crash sequence (`car+0x2C8` crash timer, `+0x2F8`
   cooldown, `src/driving/pc_crash*.cpp`). Not yet seen firing in a test;
   unverified whether the port ever enters that sequence.
2. **Off-road comments** (ORC 110–119, 114/115): triggered by the surface
   under the car (`car+0x248`, bits `0x10F407C`; the port's names call it
   "drift"). Untested.
3. **Occasional hitches** (60–100 ms frames): likely sound banks loaded
   mid-race or Metal pipelines compiled on first use. Not investigated.
4. **1080p is near the limit on the iPhone 15 Pro Max** (about 16 ms of GPU per
   frame). Heavier scenes may drop frames; per-pixel shading of the generated
   PC shaders is the cost.
5. `FE.PAK` effects played after a race unloads it (ids 3, 4, 13, 16, 30)
   count as "unmapped": minor, not investigated.

## Done in this fork (for context)

- iOS / iPadOS port, including iOS 27 UIScene support and a required game controller.
- Metal renderer: fewer render passes, clears folded into the next pass,
  GPU-private back buffer, and **pipelined frames** (CPU builds frame N+1 while
  the GPU draws N; one frame of latency). 1080p FXAA went from 26–28 to 60 fps
  on the iPhone.
- Announcer voices: `4489C0` / `44C3D0` / `4F2210` no longer fall through to
  unload-all in `race_end_runtime.cpp` (it was emptying `FE.PAK` in the menus).
- Passenger "Let's go!" (ORC 130, `4249F0(0x82)` at race start) now plays.
- Passenger kept running past stage 1: her rival check's event records
  (`0x11A..0x133`) and the course-object works are now mapped for the robots.
- Keyboard: Enter selects (was Start).
- Race-end freeze in OutRun mode (stuck in mode 26 after `race end latched: PC
  47EE70`): traffic cars' destroy callback `470560` went to the ghost handler,
  which faulted for good when a stage change destroyed traffic. The traffic
  module now handles its own `470560`.
