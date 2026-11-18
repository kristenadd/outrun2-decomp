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

A native **visionOS** port (`visionos/`, stereoscopic 3D in a window) was
added on 2026-11-17; see "visionOS port" below.

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
| `mac/source` | macOS platform: Metal D3D9 device (`metal_d3d9.mm`), SDL display / audio, `main.cpp`; game folder lookup in `mac_paths.mm` (bundle `Contents/Resources/Game` first, cache then goes to Application Support) |
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

### visionOS port (`visionos/`)

Reuses the iOS game (`ios/source/platform_api.cpp`, `ios_paths.mm`) and the
Mac renderer, but **not SDL's video**: SDL2's UIKit video layer doesn't compile
for visionOS (`UIScreen`), so SDL2 is built with `SDL_VIDEO=OFF`
`SDL_HIDAPI=OFF` (audio, events, MFi controllers only). Pieces:

- `OutRunApp.swift`: SwiftUI app with one plain window holding a RealityKit
  plane. `StereoMaterial.usda` (camera-index switch) shows each eye its own
  `LowLevelTexture`. The window resizes `.uniform` at the game's aspect
  (4:3 / 16:9; `screen_aspect()` is 0, so there's no FULL SCREEN).
- `vision_display.mm`: `SdlDisplay` reimplemented on a Metal compositor
  (3 frame slots). The app's `SceneEvents.Update` takes the newest slot
  (`vision_bridge.h`).
- `vision_main.mm`: `main.cpp` is built as `or2_game_main` (`-Dmain=…`) and
  runs on a 16 MB-stack thread.
- Stereo is in `MetalD3D9Device::set_stereo` (`metal_d3d9.mm`): a layered back
  buffer and vertex amplification, plus `pc_stereo` in the generated vertex
  shaders (`x ± s·max(w−C, −w)`, nothing at `w == 1`). The Mac test
  `mac_metal_d3d9_differential` checks it on the Mac GPU.
- Options ▸ Settings ▸ **3D DEPTH** (OFF, 1..5, `stereo_depth=` in options.ini,
  default 3): `VideoBackend::stereo_depths()`, visionOS only.
- **FRAME RATE 60 / 90** (default 60 until the owner confirms the fixes below): the headset refreshes at 90 Hz, and 60
  judders there (1 and 2 refreshes alternate). 90 uses the 120 fps
  interpolation (`frame_rate.hpp`) and is paced by the window's RealityKit
  updates (`or2_vision_take_frame` counts refreshes; `submit` waits for one).
- Frames stay on the GPU (`MetalD3D9Device::set_gpu_frames`: a ring of 3
  back-buffer copies the compositor samples, all on `metal_shared_queue()`);
  there's no CPU readback on visionOS.
- Measuring on the headset: `OR2_TRACE` logs `[visionos] N updates/s, M
  frames/s` (M of N refreshes got a new frame; 82–87 of 90 in a race at
  1600x900 stereo, 2026-11-18). **`OR2_HOST_LIVE` "hitch" lines are
  meaningless at 90 fps**: they time 60 Hz ticks, which hold one or two frames
  (11 / 22 ms). GPU time per frame is about 8–9 ms stereo, 5–7 ms mono, and
  barely depends on resolution (1080 vs 900).
- **Car / ending vanishing at 90 fps (2026-11-18), fixed on the Mac, not yet
  confirmed by the owner on the headset.** At stage 4's end and in the goal
  ending the player car (traffic too) vanished, leaving only its shadow; the
  ending had no characters.
  - **Cause:** the camera's between-tick replay (`note_camera_replay`,
    `pc_scene_renderer.cpp`) rebuilt the view with 482F20 and `apply_camera`
    also wrote the **culling frustum / planes** 482F20 does not compute. The
    special stage-change / goal cameras do not set them again every tick, so
    the car stayed culled until the next camera change. The replay now keeps
    the tick's frustum and planes, and only replays a tick whose view 482F20
    rebuilds exactly at blend 1 (`camera_replay_faithful`; in practice all of
    them: `camera replays N, skipped 0` in the race summary).
  - **Also found on the way (kept):** `pc_cw_crush_status` read 0x10 bytes past
    the 0x10F0-byte car on a hard wall hit (any frame rate; latched the car,
    `car_fault=4a8330`); `crash_entry_candidate_boundaries` now runs it on the
    real car size. Display-only frames no longer move the shared lane
    predictions (`display_preserve`). Cars whose `+16C` is more than 8 units
    from `+14` (traffic just spawned) are drawn at the tick matrix.
  - **Reproducing on the Mac** (`OR2_HOST_FRAME_HZ=90`: fixed game clock, 90 fps
    interpolation, deterministic): the 2SP autoplay path below with
    `script=1900-40000:ZR` drives itself to stage 5 with
    `OR2_HOST_HOLD_TIME=1` (race clock held). `OR2_HOST_CAR_PIXELS=1` logs
    `[car-px] frame=N red=M` (the car's red pixels, bottom centre, every 59
    frames): the vanishing was red≈0 at frames 34,000–35,800 and 43,400–45,000
    (the ending). Side-by-side runs: `OR2_HOST_WINDOW_TITLE=...`,
    `OR2_HOST_WINDOW_RECT=x,y,w,h`. Isolating switches: `OR2_FPS_NO_REPLAYS`,
    `OR2_FPS_NO_CAR_REPLAYS`, `OR2_FPS_NO_CAMERA_REPLAY`,
    `OR2_FPS_NO_ROBOT_REPLAYS`, `OR2_FPS_TICKS_ONE` (43FA00 answers 1).
    About 13 minutes per run (the game runs at 2/3 speed on a 60 Hz screen).
- visionOS holds the game only when the scene is in the **background**:
  `inactive` happens in ordinary use (system UI, focus) and holding there froze
  the game and its controller input.
- **Controller on visionOS** (2026-11-18, not yet confirmed): read straight from
  GameController every sample (`vision_gamepad.mm`), not SDL's MFi driver.
  visionOS sends controller input to what the player looks at, as pinches unless
  that view opts in. `GCEventInteraction` on the UIWindow didn't cover the
  RealityKit picture, so input froze mid-race (R2 stuck on: the car kept
  driving). Now the whole view opts in (`handlesGameControllerEvents`, visionOS
  26), and while the scene isn't `active` every control reads released.
  `OR2_TRACE` logs `[pad]` presses, controller changes and focus.
- `OR2_IOS` and `OR2_VISIONOS` are both defined. Shared files check
  `OR2_VISIONOS` where visionOS differs (`main.cpp`, `renderer_present.inc`,
  `sdl_backend.hpp`).
- Xcode generator only (Swift). Presets `visionos-simulator` and
  `visionos-device` (`-DOR2_VISIONOS_TEAM`, `-DOR2_VISIONOS_BUNDLE_ID`).
- The owner hasn't confirmed it on a Vision Pro yet. The stereo look (depth,
  comfort, HUD at zero parallax) is unverified on hardware; the simulator shows
  only the left eye.

## Building

macOS (Apple silicon):

```bash
sh mac/tools/build-dependencies.sh
cmake --preset mac-full
cmake --build --preset mac-full      # -> build/macos/mac/OutRun2006 Coast 2 Coast.app (target OutRunMacGame)
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
- `OR2_METAL_FFP=generic|sync`: fixed-function pipelines never specialised /
  specialised at once instead of in the background (see below; default async).
- `OR2_DEBUG_RACE_END=<seconds>`: testing only. Once the race clock (from GO)
  passes `<seconds>`, the player car "crosses the goal" (the goal flag that
  `4513C0` sets at the goal line), so the race ends through the game's own
  goal path (`450170`, modes 19 / 33, ending 24, results 25, name entry 26).
  Tests the race-end screens without driving a route. Logs `[dbg]` under
  `OR2_TRACE`. It fires on the current stage (not stage 5), so there's no
  stage change and the results only hold the stages driven. Bugs that need a
  stage change still need a real race. `OR2_HOST_GOAL_COURSE=10..14` picks the ending.
- `OR2_IOS_ALLOW_NO_CONTROLLER=1`: skips the iOS controller gate (testing only).
- `OR2_HD_TEXTURES=<0..16>`: texture-pack scale cap (overrides `hd_textures=`; 0 = off).
- `OR2_MAC_SCREENSHOT_DIR` / `OR2_MAC_SCREENSHOT_FRAME`: dump frames (Mac).
  Frontend menus come out **blank white** in these dumps; use the simulator's
  `xcrun simctl io <id> screenshot` or ask the user instead.

Every robot (driver, flag man, passenger…) has a **fault latch**: the first
unmapped address or unported service switches that robot off for the rest of
the race, silently unless `OR2_TRACE` is set. Check for `[rob] latched` first
when a race character stops doing something mid-race.

## Driving the Mac game unattended (autoplay)

How the goal-ending fixes were reproduced and checked without the owner
playing. It works for one run of one known path; it is not proof that the
owner's own path works (see "How to work with the owner").

**Game files for a dev build.** The owner's game lives inside
`/Applications/OutRun2006 Coast 2 Coast.app/Contents/Resources/Game`. A fresh
`build/macos/mac/OutRun2006 Coast 2 Coast.app` has none and stops at the folder
picker (no output, nothing happens). Link them in once:

```bash
ln -sfn "/Applications/OutRun2006 Coast 2 Coast.app/Contents/Resources/Game" \
  "build/macos/mac/OutRun2006 Coast 2 Coast.app/Contents/Resources/Game"
```

The saves, cache, `options.ini` and `autoplay.txt` are all in
`~/Library/Application Support/OutRunMac/Saves` (the `home()` of the runner).

**`autoplay.txt`** (read once at start, `Autoplay::load` in `game_host.cpp`):

- `script=<from>-<to>:<BUTTONS>,…`: buttons held from input frame `from` to
  `to` (a single frame `n:A` works too). Several `script=` lines are joined.
  A press is the first frame of a range, so for repeated presses use separate
  short ranges (`560-563:A,600-603:A,…`); one long range is a single press.
- Button names (Switch layout, combined with `+`): `A` (select), `B` (back),
  `X`, `Y`, `L`, `R`, `ZL`, `ZR`, `PLUS` (Start), `MINUS`, `LS`, `RS`,
  `LEFT`, `UP`, `RIGHT`, `DOWN`.
- `frames=N`: at frame N it holds L+R+Plus, the exit combination.
- `trace=1`: writes `trace.txt` next to it, one line per frame (mode, child, start state).
- Autoplay presses are added to the real pad, so a person touching the window
  changes the run.
- **Delete `autoplay.txt` afterwards**, or the owner's next launch replays it.

The **OutRun 2SP** path from launch (title → Start → Right ×2 → A for the defaults):

```text
script=400-403:PLUS,460-463:RIGHT,500-503:RIGHT,560-563:A,600-603:A,…,1060-1063:A
```

Stop the A presses before the race starts (it started at about frame 1030):
later presses skip screens (the ending's credits included). Pressing A blindly
from the title went into a mission mode (variant 6), which never reaches the
ending.

**Getting to the ending fast:** `OR2_DEBUG_RACE_END=4` finishes the stage 4 s
after GO; `OR2_HOST_GOAL_COURSE=10` (10..14 = goals A..E) picks a real goal
ending. Without it, the forced goal on stage 1 ends with an ending that has no
scene (a black screen). `OR2_HOST_TRACE=1` prints `[flow] mode a -> b` lines
with the frame number (2SP: 16 race → 19 → 33 → 24 ending → 25 map → 26 name
entry); `grep "\[flow\]"` gives the timeline.

```bash
OR2_HOST_GOAL_COURSE=10 OR2_DEBUG_RACE_END=4 OR2_HOST_TRACE=1 timeout 60 \
  "build/macos/mac/OutRun2006 Coast 2 Coast.app/Contents/MacOS/OutRun2006 Coast 2 Coast" > run.log 2>&1
```

With that setup, mode 24 ran from about frame 2000 to 3200, i.e. about 33 s
to 55 s after launch.

**Seeing the screen:** the `OR2_MAC_SCREENSHOT_DIR` dumps come out white for
much of the game, the ending included. Plain `screencapture -x -t jpg
<file>` of the desktop works (the shell has screen-recording access). Take one
every 1 to 2 s in a loop while the game runs in the background, then
`sips -Z 700` and `sips -c <h> <w> --cropOffset <y> <x>` to shrink and crop
before reading. Tell the owner first, because a game window will open over their work.

**Widescreen runs:** set `widescreen=1` in `options.ini` for the run, and
copy the file back afterwards (back it up first).

**Temporary tracing:** a guarded `fprintf` behind a throwaway env var (for
example `[end]` lines of the ending's state, autoscene frame and SPRANI status
every 30 frames) answered the timing question quickly. Two cautions: calling a
service from a trace can latch a fault (`4B5FC0` before the autoscene exists
latched the race end), so only read state that exists; and remove the
tracing before committing.

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

## Clarissa mode (work in progress)

Game variant **5** (`[780258]`). Its missions are run by the C2C **request
manager**, the object at `7F9460` (`55A930()` = `[7F94C0]` says it is active;
its config `7F94C4..7F94D4` is `event_config_7f94c4`, copied into the block by
`native_race_requests_map`). Most of the manager is still the bulk translation
(`46C1F0` init, `46C200` control → `482140`, `46C210` display), run on the
traffic module's memory through `native_race_requests_call`
(`race_traffic_runtime.cpp`); its leaves are answered by the traffic `Run`
service. Native ports so far: `481650` (lane-0 placement copy, START and the
area's stage change), `4FC390` (road Mu), `4FBBB0` (request cars, in
`race_traffic.cpp`), `4FDF20`, and the x87 leaves `449330..4493A0`, `449470`.

**Getting there** (from the title, 2 s between presses): A ×3, Down, A ×5.
In the race, **R2 / ZR accelerates** (A does not). Autoplay version:

```text
script=400-403:A,520-523:A,640-643:A,760-763:DOWN,880-883:A,1000-1003:A,1120-1123:A,1240-1243:A,1360-1363:A,1700-7400:ZR
```

START is mode 13 (about frame 1363); the race (mode 16) starts at about 1527.
`OR2_BULK_DEBUG=1` names the translated function an exception passed through
(`[bulk] 46C200 faulted`); the traffic summary line (`race traffic inits=…
latched=… fault=… missing:`) names the first unported piece.

Memory layout notes that cost time:
- The manager's block runs to `800AF8`; the traffic module's block starts at
  `800A00`. In variant 5 the manager's tail is mapped back over traffic's
  (`map_memory`), or the manager's own writes (road Mu `+764C`) go astray.
- `5217E0` keeps statics at `85B338` / `85B33C` (the cell list covers them).

Displays: `46C210` (3D: `481810`, its objects and the road markers
`505CA0`) runs through `native_race_requests_display`, hooked with the ghost /
course-object displays; render leaves are queued and run by the AREA's draw
list. `46C220` (2D: native `requests_2d_481a40`) runs from the HUD display
`4BEB00`'s variant-5 branch (`4BEC9B`) through `heart_run`, so its sprites,
digits and request rows (`4BB9E0`) reach the HUD. `505CA0` needed a hand-added
`RET 14` in `bulk_pops.inc` (the listing missed it).

Two traps found on 2026-11-17 (fixed):
- SPRANI banks START requests (`2B`: the Heart Attack / Clarissa HUD sprites,
  heart ring, request banner) are not handed to the sprite pool by the native
  START, so `428320` / `428460` / the UI resources' creates failed silently.
  `FrontendSprites::bind_missing` (installed in `game_host`) binds a bank on a
  create's first miss.
- The voice set `427DB0` reads `[535EF0()+208]`; `535EF0` is
  `lea eax,[ecx+4]` on the frontend owner, i.e. the owner's `+20C`, not the
  license. With the wrong word Clarissa got Holly's `ENGLISHH_` set, whose
  mission banks (`HAM_MIS_PASS`, `HCARS`) don't exist: no request voices.

Also fixed on 2026-11-17 (owner's report: in "Keep passing cars" the cars
slowed, had no collision, the request never ended, no more cars came, and
Quit hung): the traffic module latched on `521350`'s statics at `85B32C..`
(the manager's cell now starts at `85B324`). Quit from the pause menu (Down
×2, A) needed `46C270` (`481500`) in the title owner, and the manager's
teardown needed `44BF10`, `448990`, `4278C0`, `580C38` (operator delete) and
the scalar deleting destructors (`translated_scalar_deleting_dtor`: 187 in
the EXE, `RET 4`). `46C2D0` (a new manager) clears traffic latches left by
the last teardown, or the next START stalls.

**Known issues** (as of 2026-11-17):
- The race-end results screen calls `46C220` through its own binding
  (`race_end_runtime.cpp`), which still expects a translated `481A40` and
  lacks `4BB9E0` / `50ECC0`: expect it to latch at the end of a run.
- `race traffic latched: PC 50E140: traffic service not ported` (owner's
  Vision Pro run, 2026-11-18): about 4.5 minutes into a Clarissa race
  (variant 5, mode 16); traffic and requests stop for the rest of that race.
  Not investigated yet.
- Drifting on turns (owner, iPad): fixed on 2026-11-17, **not yet confirmed
  by the owner**. Slip-angle control (`4A3310`) reads `46C470` = `[800AAC]`,
  the manager's `+764C` (1.0); the port passed a stale 0, which drops the
  cornering force on any steering. Both road Mu and slip assist now read the
  manager's block.
- Stopped request cars (owner): fixed on 2026-11-17, **not yet confirmed by
  the owner**. Request cars (`+C5C` 0x40000) only drive once released:
  `46C3B0` = `[800AA0]` (manager `+7640`) in Clarissa mode, `45D820` =
  `[7F250C]` in Heart Attack. `45D820`, `45B510`, `45AFF0` and `45B810` were
  answered as constant 0 ("network globals"), so in Heart Attack's "Keep
  passing cars" request the cars stayed parked. In Clarissa mode a short
  stop is by design: cars wait for `+7640` and pull away together.
- Fixed on 2026-11-17, **not yet confirmed by the owner**:
  - Goal hang: mode 19 does nothing for variant 5 (as the PC); the manager's
    `4FEA00` ends the race through `43F8C0` (`0x22`; `0x18` without the C2C
    frontend flag), which the traffic service did not answer. Its start
    `46C370` needs `[+148]` = `7F95A8`, set at each stage start by the area
    (`44CF00` -> `46C360`: 1 on the one-exit, final stage). Testing it on
    stage 1 needs the flag forced (it is 0 there by design).
  - Per-stage hearts / no reset: the area's `46C4C0` / `46C260` / `46C2C0(1)`
    (the manager's stage reset) were "void services" counted and skipped, and
    `46C240` ran the raw translation; now all go through
    `native_race_requests_call`. The native `4829D0` reads the current course
    record `[7D3188]` (`44C830`), not START's selection.
  - The stage-2 request code needed the course queries `43D920`, `43CB40`,
    `43E7E0` (`EAX` = list; mode, x, &y, z, &polygon, &flags, type),
    `43F370`, the vector / matrix leaves (`40EEB0` / `40EFD0` return in `ST0`:
    CPU-level leaves), `449640`, and the course roots `780100..` mapped
    (`map_course_world_pc`, once the world is ready).
- The race-end results (`46C220` from `race_end_runtime.cpp`) still expect a
  translated `481A40` (the native port is `requests_2d_481a40`).
- Fixed on 2026-11-17, **not yet confirmed by the owner** (stage-3 stretched
  bubble, then a freeze at the stage change; no beach ball):
  - A latched manager no longer holds the AREA's stage change: `46C240` /
    `46C4D0` answer 1 and the stage resets 0 (`latched_answer` in
    `native_race_requests_call`), so the race goes on without requests.
  - Request objects' list `85B350` (count, then 250 entries of `2C` at
    `85B354..85DE4C`, `521A20` / `5219B0` / spawner `521AE0`) is mapped whole
    (it held 8; the meteors have 10–30).
  - Lane 0's handle `84D6CC` in variant 5 is START's copy of the placements
    (`481650` → `4F03A0`, `scene_owner_object_handle_84d6cc`) until a stage
    change replaces it: without it `4F03E0(0)` was 0 and no request object
    (beach ball, meteor) ever spawned on stage 1.
  - `40A7D0` / `40A820` / `40A840` / `40A8A0` are **(out, in)** (D3DX); the
    traffic service had them reversed (`5053A0` wrote over the car's `+14`).
  - New pieces: `40A240` (invert), `40A310`, `40A360`, `4536C0` / `4536F0`,
    native `4F6BD0` (segment / box) and `4AA370` (car touches object), CPU
    leaves `449800`, `40F140`, `46D110`, `46D180`, `46D220`.
- Request types seen (the def's `+0`, from the course's `Req_*.bin`; force one
  for a test by pointing every record's `+58` at a def): `3` keep passing
  cars, `F` dribble the beach ball (list entries type 8), `13` avoid the
  meteors (type 9, model `1EE0005`), `14` cut the line, `15` avoid abduction
  (UFO: a request car of kind 4, drawn by the native `4AE18B`; kind 5 too), `16`
  "Honk the horn!" (reverse courses only; object 21). The horn is race switch
  bit `0x10`: **B / Circle** on the Mac pad (the PC joystick config; L1 is gear
  down). Its road strip is drawn by `5064E0` straight through the device: the
  request display runs with a recording device (`RequestsRecordingDevice`) whose
  calls are replayed in draw-list order. Clarissa has no `HAM_MIS_HHORN` voice
  bank (only Holly's `ENGLISHH_` exists), so effect 420 is silent by data.
- Request types by voice bank (`481C92`): 1 MATCH, 2 DRIFT, 3 PASS, **4 COLLIDE
  ("Don't crash!")**, 5 ZONE, 6 CONEHIT, 7 COIN, 8 GATES, A NITRO, B GHOST /
  HCARS ("Hit the cars!"), C SLIP, D FOOD1..6, E MTOTAL, F BALL, 10 SOUND..CARS
  (sub-kinds), 11 HOLDLINE, 12 KEEPCONT, 13 METEOR, 14 CUTLINE, 15 UFO, 16 HHORN,
  17 DUCK, 18 PHOTO. Rank (`4FFE10`) = record `+28` (score) − `+2C` (penalty)
  against 0/10/20/30/45/60/80 (`6ADDAC`), capped by `+40/+44/+48`; 6 is best.
- "Don't crash!" ranked E (owner, fixed 2026-11-17, **not yet confirmed**): its
  score is the per-frame progress `[7F9460+7690] − [+7694]` (`4FC490` from
  `4A6CF0`), and `4A6CF0` returns in **ST0**; the service answered EAX, so the
  bulk code read 0. `TranslatedModule::st0_results` (bulk: `4A6CF0`, `449940`)
  pushes such a service's answer onto the translated FPU.
- Forcing a request for a test (2026-11-17): in `requests_stage_script_4829d0`,
  set `+140` (course id, `64EF38` names) before the path, and before `482820`
  write `base + def` into each record's `+58` (base = loader `+A4`, i.e.
  `[7F9460+A4]`; records are 0x74 bytes, e.g. Niagara_r `1518`×8 / def `1418`,
  Niagara `166C`×6 / UFO def `1578`); patch the records' u16 start / end to
  shorten the window. The def must be patched before `482820` (its `5053A0`
  prepares the def).

**Heart Attack** (OutRun 2SP, variant 2), from the title, 2 s apart: A,
Right ×2, A ×3, Right, A ×2. Autoplay:
`script=400-403:A,520-523:RIGHT,640-643:RIGHT,760-763:A,880-883:A,1000-1003:A,1120-1123:RIGHT,1240-1243:A,1360-1363:A,1700-7400:ZR`
(race at about frame 1573).

## Outstanding issues

See the Clarissa mode list above. The owner confirmed on 2026-11-16 that the
earlier list is resolved: passenger reactions in big crashes (ORC 345–365),
off-road comments (ORC 110–119), occasional 60–100 ms hitches, 1080p frame
time on the iPhone 15 Pro Max, and `FE.PAK` effects counted as unmapped after
a race. Don't chase them again unless the owner reports one.

## Done in this fork (for context)

- iOS / iPadOS port, including iOS 27 UIScene support and a required game controller.
- Controller button prompts (`src/platform/button_prompts.cpp`): the six keyboard-prompt
  sprite sheets are swapped by XXH32 for Xbox / PlayStation / Switch art from
  `<game>/textures/load/<Style>/` (downloaded by `tools/button_prompts/fetch.sh`, not in
  git), following the controller at runtime. `[btn]` lines under `OR2_TRACE`.
  They share the texture-pack loader below and keep the pack's resolution; with packs
  on, no controller shows the pack's `Keyboard/` (or unstyled) sheet.
- **Hi-res texture packs** (2026-11-18, `src/platform/texture_pack.cpp`): OutRun2006Tweaks
  packs in `<game>/textures/load/` (`<XXH32 of the original DDS>_<w>x<h>.dds`, any
  folders; the bank's own XST / PMT folder wins, the `Keyboard/Xbox/PlayStation/Switch`
  folders are prompts only). The owner's pack is the OR2006Sprites "HD GUI" pack (UI
  only: 289 files, 3 GB, mostly 4x DXT5 / A8B8G8R8; their copy is in
  `~/Projects/OutRun 2006 Coast 2 Coast Hi-Res/textures`, whose EXE differs from the
  Steam one, so test with the normal game files plus that `textures` folder).
  - **How:** `PcD3D9Device::create_replacement_texture` keeps the big texels but
    GetLevelDesc (`texture_level_size`) reports the **original** size, so the 2D
    renderer's texel → UV math (`42DDF0`, `42BE60`, ID3DXSprite rects) is unchanged.
    Metal drops the CPU copy after upload (`gpu_only`). Never read a replacement's
    texels through `texture_level_size` (the Metal fade check uses the real size).
  - Sprite banks: decoded on a worker thread (`TexturePackUi`), swapped in at the full
    2D flush (24 MB per frame budget), so the original shows for a moment. Scene (PMT)
    textures: synchronous in `pc_pmt_loader.cpp` with a box mip chain.
  - **Scale cap** `hd_textures=` in options.ini (0 off, 1..16; default 4 Mac, 2
    iOS / visionOS), `OR2_HD_TEXTURES` overrides; box-filtered down to the largest
    divisor of the file's scale. Banks are never unloaded, so memory grows with the
    menus visited: the frontend bank `sumo_fe` is ~320 MB RGBA at 2x, 1.3 GB at 4x.
  - `[tex]` lines under `OR2_TRACE` (one per replaced texture, failures with the reason).
  - Owner-tested at 2x on iPhone 15 Pro Max, iPad mini 6 (menus, a race to the goal
    ending, no jetsam) and Vision Pro (three races, 87–90 of 90 frames): 201–211
    textures replaced, none failed. Copy a pack to a device with `xcrun devicectl
    device copy to … --source <folder> --destination "Documents/[<game folder>/]textures/load/<folder>"`
    (one folder per call; the iPad's game files are in `Documents` itself, the
    iPhone's and Vision Pro's in `Documents/Outrun2006 Coast 2 Coast`).
- Metal renderer: fewer render passes, clears folded into the next pass,
  GPU-private back buffer, and **pipelined frames** (CPU builds frame N+1 while
  the GPU draws N; one frame of latency). 1080p FXAA went from 26–28 to 60 fps
  on the iPhone.
- **Specialised fixed-function pipelines** (2026-11-18, owner's report: Casino
  Town choppy once the road is wet, worst on Vision Pro). The wet road's spray
  (PART_EFC water, 419DC0 billboards behind the car) shades ~1.5 screens of
  blended fragments per frame, which is authentic, and every fixed-function draw ran
  `pc_ffp`, a generic shader interpreting the stage cascade per fragment. Now
  each fixed-function state (stage op / arg / misc, specular, alpha test, fog
  mode) gets its own pipeline with that state as function constants
  (`ffp_fixed`, `ffp_state0..12`, `generate_metal_shaders.py`), compiled in
  the background; the generic pipeline draws until it is ready (no hitches).
  Same arithmetic, so the same pixels (`mac_metal_d3d9_differential_ffp`).
  M3 Max, 1920x1440: race GPU 3.9 → 1.35 ms/frame, Casino Town 5.1 → 1.75 ms,
  the spray 0.6–1.0 → 0.2 ms. `OR2_TRACE` logs `[metal] N fixed-function
  pipelines specialised`. Confirmed by the owner on the Vision Pro (2026-11-18):
  a solid 90 fps through Casino Town (84.7 of 90 refreshes got a new frame in the log, min 77).
- Announcer voices: `4489C0` / `44C3D0` / `4F2210` no longer fall through to
  unload-all in `race_end_runtime.cpp` (it was emptying `FE.PAK` in the menus).
- Passenger "Let's go!" (ORC 130, `4249F0(0x82)` at race start) now plays.
- Passenger kept running past stage 1: her rival check's event records
  (`0x11A..0x133`) and the course-object works are now mapped for the robots.
- Keyboard: Enter selects (was Start).
- iOS fills the screen: Options ▸ Settings ▸ ASPECT RATIO has **FULL SCREEN**
  (default on iOS, `fit_screen=1` in options.ini): Hor+ at the device's own
  aspect. 4:3 / 16:9 still selectable (16:9 letterboxes). The SDL canvas
  width follows the aspect at runtime (`g_canvas_width`, `renderer_present.inc`).
  The 2D menus / HUD stay a centred 4:3, as in 16:9.
- Race-end freeze in OutRun mode (stuck in mode 26 after `race end latched: PC
  47EE70`): traffic cars' destroy callback `470560` went to the ghost handler,
  which faulted for good when a stage change destroyed traffic. The traffic
  module now handles its own `470560`.
- **120 fps on iPhone** (2026-11-18): `SdlDisplay::present` slept to 1/60 s per present
  whatever the FRAME RATE (Mac too); it now follows `display_rate()`. A ProMotion iPhone
  also needs a display link with a preferred rate range before a Metal layer goes past
  60 Hz (`screen_request_frames_per_second`, `ios/source/display_rate.mm`). The select-button
  overlay counts frames shown (not 60 Hz ticks) and lists the real settings. `OR2_TRACE`
  logs `[present] N frames/s`. Measured on the 15 Pro Max at 2342x1080 FXAA: ~119 fps while
  cool, 47–70 once warm (CPU record and GPU time per frame both about double: thermal).
- Goal ending (mode 24, staff roll): the scene stays in its 320x240 window
  (the car reflection cube's target switch reset the viewport), centred in the
  4:3 area with a 4:3 camera in widescreen; race-end modes no longer tick the
  SPRANI pool twice (credits and fade ran at double speed); full-screen flat
  fades cover the whole widescreen canvas (Metal `screen_fade`).
