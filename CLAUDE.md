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
- **Stage-change birds jittering at 90 fps (2026-11-18), fixed; confirmed by the owner on
  the Vision Pro.** The birds are animated course objects (`0x137xxxx` /
  `0x1380000`, display `4A9DF0` in `race_traffic_objects.inc`). The traffic module's
  `4493E0` always answered 1.0, and `4A9DF0` ends with `+E0 = +0`, so on a frame without a
  tick the birds jumped to their tick position (steps 1.67 / 0.56 / 1.12 instead of an even
  1.12). `4493E0` now answers `display_blend()`, and `anim_previous_between_ticks` puts
  the tick's previous matrix back in `+E0` on frames without a tick.
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
- `OR2_DEBUG_REQUEST=course,set,record`: forces one C2C passenger request
  (testing only; see "Clarissa mode").
- `OR2_HOST_FAULTS=1` (Mac host): caught faults of ported code on stderr.
- `OR2_HD_TEXTURES=<0..16>`: texture-pack scale cap (overrides `hd_textures=`; 0 = off).
- `OR2_MAC_SCREENSHOT_DIR` / `OR2_MAC_SCREENSHOT_FRAME`: dump frames (Mac).
  `OR2_MAC_SCREENSHOT_RANGE=a-b` dumps every presented frame a..b (BMP at the window's pixels;
  autoplay runs are deterministic, so two runs with one switch changed compare frame by frame).
- `OR2_HOST_SCREEN_ASPECT=<w/h>` (Mac): act as a fixed screen of that aspect, so ASPECT RATIO
  offers FULL SCREEN as on iOS (with `fit_screen=1` in options.ini). iPad mini 6: 1.5228.
  Frontend menus come out **blank white** in these dumps; use the simulator's
  `xcrun simctl io <id> screenshot` or ask the user instead.

Every robot (driver, flag man, passenger…) has a **fault latch**: the first
unmapped address or unported service switches that robot off for the rest of
the race, silently unless `OR2_TRACE` is set. Check for `[rob] latched` first
when a race character stops doing something mid-race.

## Testing frame rates on the Mac

The game always ticks at 60 Hz; 90 / 120 show interpolated frames between
ticks (`frame_rate.hpp`). On the Mac:

- **60 / 120 for real:** Options ▸ Settings ▸ FRAME RATE offers 120 only when
  the screen refreshes at 100 Hz or more at launch (a ProMotion built-in
  display; a 60 Hz external monitor won't offer it). The launch line
  `[frame-rate] launch rate N fps (screen M Hz)` says which you got; a window
  moved to a 60 Hz screen drops back to 60. The choice is `framerate=` in
  `options.ini` (back it up before a test changes it).
- **Any rate on any screen (90 = the Vision Pro):** `OR2_HOST_FRAME_HZ=N` sets
  the display rate to N and makes the game clock advance 1/N s per presented
  frame, so the run is deterministic and the same on every Mac. Vsync still
  caps the presents: on a 60 Hz screen a 90 run plays at 2/3 speed, a 120 run
  at half speed (fine for checking the picture, not the feel). It overrides
  `framerate=` at launch; don't change FRAME RATE in the menu during such a run.
- **What it's showing:** `OR2_TRACE=1` logs `[present] N frames/s (rate R,
  screen S Hz), present X ms/frame` every 5 s; the controller's select / Share button toggles
  the overlay (FPS of frames shown, CPU, GPU, settings). `OR2_HOST_LIVE=1` adds
  the frame profile, but its numbers are **per 60 Hz tick**: divide by the
  frames per tick (`calls` / 300) for per-frame times; its "hitch" lines are
  meaningless above 60.
- **Comparing 60 against 90 / 120:** run the same autoplay script twice with
  `OR2_HOST_FRAME_HZ=60` and `=90`, side by side (`OR2_HOST_WINDOW_TITLE`,
  `OR2_HOST_WINDOW_RECT`), with `OR2_HOST_CAR_PIXELS=1` for car visibility. The
  `OR2_FPS_*` switches (visionOS section) turn off the in-between replays one
  kind at a time.

```bash
OR2_HOST_FRAME_HZ=90 OR2_TRACE=1 OR2_HOST_CAR_PIXELS=1 OR2_HOST_WINDOW_TITLE="90 fps" \
  "build/macos/mac/OutRun2006 Coast 2 Coast.app/Contents/MacOS/OutRun2006 Coast 2 Coast" > run90.log 2>&1
```

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

**`tools/autoplay_shots.py`** does all of the above in one call: it writes
`autoplay.txt`, runs the game in its **own home** (`build/autoplay-home`, a
copy of the owner's saves, so theirs are never touched), screenshots the
window at given input frames (it watches `trace.txt`), kills the game and
deletes `autoplay.txt`. Shots and `run.log` go to `build/autoplay-shots/<name>/`.

```bash
tools/autoplay_shots.py c2c "@A A A RIGHT DOWN"      # a press every 120 frames from 400, a shot 100 after each
RUN_X=820 tools/autoplay_shots.py ta "@A A RIGHT RIGHT RIGHT A"   # second window beside the first
tools/autoplay_shots.py race "@A A A A A A A A A 300* PLUS DOWN" 1600,1700   # "300*" waits; extra shot frames
```

`ZR:600` holds ZR for 600 frames. `EXIT_FRAME=N` exits the game cleanly at input frame N (L+R+Plus)
so it writes its result log, `<home>/SaveGame/OutRunMac.log` (`hud variant=…` NAVI misses, the
`race traffic … routed: … missing:` services), which a killed run never writes. Each line printed is `<shot frame> f=… mode=…
child=…` (the frontend's child screen, listed in the menu map below). Grep
`run.log` for `[stall] not ported` (a screen that isn't ported yet) and
`[flow]`.

## Menu map (Mac, mapped with autoplay on 2026-11-18)

Mapped with the owner's save copied (everything unlocked, AAA ranks). Paths
start at the **title screen** (logo; the attract movies before it skip to it);
with autoplay the first press is at frame 400, one press every 120 frames
(2 s). Pressing faster can lose presses during screen transitions. "mode" is
`mode_state.current`, "child" the frontend child state (`trace.txt` /
`autoplay_shots.py`; all frontend screens are mode 32 except the OutRun2SP
screens, mode 10).

**General rules.** A (or Start / PLUS) selects, B goes back one screen.
Horizontal menus (big title + a row of items at the bottom) move with
**Left/Right** and **do not wrap**; extra Rights stay on the last item. The
pop-up lists (pause menu, Options, Showroom) move with **Up/Down** and wrap.
Going back resets the parent's cursor to its first item (pause menu, Options)
in some screens, so re-press from the top. Pad prompts drawn as PC keys:
**Tab = ZR** (change class / original BGM / reverse, in menus), **F1 = X**
(car colour, random music), **F2 = Y** (license / OutRun2 ⇄ 2SP page), as
mapped in `game_host.cpp` `map_pad`.

```text
Title (mode 32 child 0)
└─ A → MAIN MENU (child 1): SINGLE PLAYER · MULTIPLAYER · OUTRUN2SP · RANKINGS · OPTIONS
   ├─ SINGLE PLAYER (A) → (child 2): COAST 2 COAST · OUTRUN · HEART ATTACK · TIME ATTACK · SHOWROOM
   │  ├─ COAST 2 COAST (A) → driver grid (child 58)
   │  │    top row (Left/Right):  FLAGMAN 1 (novice) · FLAGMAN 2 (intermediate) · FLAGMAN 3 (professional) · FLAGMAN 4 (OutRun)
   │  │    Down → bottom row:     CLARISSA · JENNIFER · HOLLY · HOLLY 2   (Up/Down switch rows; HOLLY 2 = MIX 2 COURSE,
   │  │                           shown once Holly 1 is rank A, see "Holly 2")
   │  │  ├─ Flagman N (A) → route map (child 59): Left/Right walk the nodes ("1 NOVICE", "2 NOVICE", …;
   │  │  │     Flagman 4 starts at "26 OUTRUN"); Up/Down pick a branch where the route forks.
   │  │  │     A on a node → its mission icons (1–5, e.g. "Race the Rivals!", "Test your Drift!"), Left/Right
   │  │  │     A on a mission → CAR SELECT (child 10) → A → TRANSMISSION pop-up (child 16: Left AT / Right MT)
   │  │  │     → A → MUSIC SELECT (child 4) → A → START (mode 13) → race (mode 16)
   │  │  └─ Girl (A) → request map (child 60): cursor on the "OUTRUN2SP COURSE" icon (the whole course);
   │  │        Right → stages ("SUNNY BEACH", …). A on it → CAR SELECT directly (no mission icons)
   │  │        → A → TRANSMISSION → A → MUSIC → A → START (variant 5, "Clarissa mode")
   │  ├─ OUTRUN (A) → COURSE SELECT (child 36): OUTRUN2 · OUTRUN2SP · OUTRUN2 15 STAGE CONTINUOUS · OUTRUN2SP 15 STAGE CONTINUOUS
   │  │     → A → CAR SELECT → A → TRANSMISSION → A → MUSIC → A → race
   │  ├─ HEART ATTACK (A) → COURSE SELECT (child 36): OUTRUN2 · OUTRUN2SP → A → CAR → A → TRANSMISSION → A → MUSIC → A → race
   │  ├─ TIME ATTACK (A) → COURSE SELECT (child 36): OUTRUN2 · OUTRUN2SP
   │  │     → A → MODE SELECT (child 37): GOAL · STAGE
   │  │        ├─ GOAL (A) → GOAL SELECT (child 38): GOAL A … GOAL E (+15 continuous; ZR = reverse)
   │  │        │     → A → CAR → A → TRANSMISSION → A → MUSIC → A → race
   │  │        └─ STAGE (A): NOT PORTED, hangs ([stall] frontend screen key 27, action 4450A0)
   │  └─ SHOWROOM (A) → (child 43) Up/Down list: FERRARI CARS · STAGES · GOALS · BGM · CAR COLORS
   │        → A → that list (e.g. STAGES: Sunny Beach, Palm Beach, … with SOLD / price), Up/Down
   ├─ MULTIPLAYER (A) → (child 6): ONLINE · LAN
   │     LAN (A) → (child 8): JOIN GAME · CREATE GAME   (not followed further: networking)
   ├─ OUTRUN2SP (A) → mode 10, the 2SP-style screens:
   │     SELECT YOUR CAR (Left/Right, 10 cars: F50, Enzo, 360 Spider, F40, Testarossa, 288GTO, 512BB,
   │       Dino 246GTS, 365GTS/4 Daytona, 250GTO; X = colour) → A → SELECT TRANSMISSION (Left AT / Right MT)
   │     → A → SELECT GAME MODE (Left/Right): OutRun · Heart Attack · Time Attack · OutRun 15 continuous ·
   │       Time Attack 15 continuous (Y = OutRun2 ⇄ OutRun2SP course) → A → SELECT YOUR MUSIC → A
   │     → mode 11 → 13 → race
   ├─ RANKINGS (A) → (child 49): SINGLE PLAYER · OUTRUN2SP · MULTIPLAYER
   │     SINGLE PLAYER (A) → (child 51): OUTRUN · HEART ATTACK · TIME ATTACK → A → table (child 57): OUTRUN2 · OUTRUN2SP
   │     MULTIPLAYER (A): NOT PORTED, hangs ([stall] frontend screen key 16, action 4442A0); B still backs out
   └─ OPTIONS (A) → (child 44) Up/Down: SETTINGS · CONTROLS · AUDIO OPTIONS · DONE
         SETTINGS: CAMERA · SPEEDOMETER · OUTRUN2SP DIFF. · DISPLAY GHOST CARS · ASPECT RATIO ·
                   RESOLUTION · ANTI-ALIASING · FRAME RATE   (Mac; iOS adds FULL SCREEN under ASPECT RATIO,
                   visionOS 3D DEPTH); Left/Right change a value, A / Start save and leave
         CONTROLS: CONFIGURATION · SENSITIVITY
         AUDIO OPTIONS: SOUND VOLUME · MUSIC VOLUME
```

**Shared screens.** CAR SELECT (C2C / OutRun / Heart Attack / Time Attack):
Left/Right cars (DINO 246 GTS first, then 365 GTS/4 DAYTONA, F50, 360 SPIDER,
F355 SPIDER, …), ZR (Tab) changes class, X colour. MUSIC SELECT: Left/Right
tracks (SPLASH WAVE first, MAGICAL SOUND SHOWER, …), ZR original BGM, X random.

**In the race** (mode 16): ZR accelerates, ZL brakes (B too; B is also the
horn), Left/Right steer, L/R shift (MT), X view. **PLUS = pause** (mode 18), Up/Down list (wraps):
RETURN TO GAME · RETRY · QUIT · SETTINGS · CONTROLS · AUDIO OPTIONS.
RETRY and QUIT ask "Are you sure?" (A = Yes, B = No). The pause SETTINGS has
SPEEDOMETER · DISPLAY GHOST CARS · ASPECT RATIO · RESOLUTION · ANTI-ALIASING ·
FRAME RATE. B closes the pause menu.

**Ready-made paths from the title** (2 s apart, i.e. `autoplay_shots.py "@…"`;
then hold ZR to drive):

| Destination | Presses | Notes |
| --- | --- | --- |
| C2C Flagman 1, node 1, mission 1 | A ×9 | START (mode 13) ≈ frame 1363, race ≈ 1528 |
| C2C Flagman 4 (OutRun routes) | A A A, Right ×3, A ×6 | node "26 OUTRUN" |
| Clarissa / Jennifer / Holly | A ×3, Down, (Right ×0/1/2), A ×5 | see "Clarissa mode" |
| OutRun mode, OutRun2 course, defaults | A A, Right, A ×5 | Right ×1–3 on the course page for 2SP / 15-stage |
| Heart Attack, OutRun2 | A A, Right ×2, A ×5 | |
| Time Attack, OutRun2, Goal A | A A, Right ×3, A ×7 | |
| OutRun2SP menus, OutRun mode | A, Right ×2, A ×5 | mode 10 |
| Pause ▸ Quit | PLUS, Down ×2, A, A | in a race |
| Options ▸ Settings | A, Right ×4, A, A | |

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
- **When the owner wants to test on a device** (iPhone, iPad, Vision Pro), build and
  install it for them without asking: Release build of that device's preset (`ios-device`
  / `visionos-device`, signing is in the local cache), then `xcrun devicectl device install
  app` to the device from `xcrun devicectl list devices`. Tell them it's installed; they
  launch it from its icon.
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

**All three girls** are variant 5 with the same manager; only the course (and
its `Req_*.bin`) and the voice set differ. From the title, 2 s apart (autoplay
press frames 400, 520, … as above):
- Clarissa: A ×3, Down, A ×5 (Beach, race at about frame 1527).
- Jennifer: A ×3, Down, Right, A ×5 (Palm Beach, race at about 1648).
- Holly: A ×3, Down, Right ×2, A ×5 (Easter Island **reverse**, race at about
  1767; requests `10`/6 at cs 110 and `D` FOOD at cs 480).
- Pause ▸ Quit: PLUS, Down ×2, A, then **A again** for "Are you sure? Yes"
  (modes 18 → 34 → 27 → 28 → 32). Works in all three (autoplay, 2026-11-18).

Fixed on 2026-11-18 (autoplay of all three girls, **not yet confirmed by the
owner**):
- **Holly's car never moved** (latched at the first tick, `road-info course
  view mismatch`): the car's ground probe at init (`519300`, before the start
  position is set) runs at (0, 0); Easter Island reverse has no road there, so
  the miss answers course type 1, a lane START never loads. The native course
  world handed such a lane a view with `load_type` 0; it now presents unloaded
  lanes as empty roots of their own type (`CourseWorldRuntime::tables`), and
  the lookups answer 0 as the PC's do with a null header (`43D6E0`, `43E3B0`).
- **`50E140` not ported** (the owner's Vision Pro latch, really Holly's first
  request `10`/6 at about 40 s): native `leaf_50e140` (the def's `+7C`
  placements into the handler's two lists, then `50D600` for each), chained in
  `requests_clock_module`; the traffic service's own bulk calls chain it too.
- `405360 object bank not loaded: 01EE0007` (request display latch, every
  course with road markers): `505CA0` drew straight on the renderer without
  `448810`; it now loads the bank when resident and draws nothing otherwise
  (`native_race_area_ensure_bank`), as the AREA's draw list does.
- Meteors (`13`): a hit scales the player's body work `82E84C` (`50B226`); the
  body `82E7F0` is now mapped in the traffic module.
- The heart run (`46C220`, HUD) reads the course roots `780110`: mapped
  (`map_course_world_pc`) as in the manager's other entries.

**Forcing a request** (testing only): `OR2_DEBUG_REQUEST=course,set,record`
loads that course's `Req_*.bin` for the first stage of the process and runs
that record's def in its own cs window (other records never start);
`python3 tools/request_catalogue.py "<game folder>"` lists one record per
type / sub-kind. Use Clarissa's path (Beach), hold ZR; the window starts about
3 cs per second after GO. `OR2_TRACE` prints `[req] active <type>/<sub> cs a..b
score=N stageK(course c)=records/hearts/shown` whenever it changes, which tells
"the request ran" apart from "autoplay failed it". Result of forcing all 33
type / sub-kinds (2026-11-18): every one starts and ends without a latch except
the photo (`18`). Hearts earned by a ZR-only drive: `3`/1, `4`, `5`/0..2, `6`,
`C`, `D`, `10`/1, 2, 3, 5, `15`. The rest scored 0 (they need steering,
drifting, coins, gates, the horn…); `16` honk scored 0 even with B pressed (the
horn sounds) on Beach, a forward course: unconfirmed whether the request counts
it. Parallel autoplay runs: pass `--home=<dir>` (saves in `<dir>/SaveGame`,
`autoplay.txt` there; symlink the cache to `<dir>/OR2006C2C.cache`) with
`OR2_GAME_ROOT=<game folder>`; `OR2_HOST_FAULTS=1` prints caught faults
(`4a8330: …`).

**Known issues** (as of 2026-11-18):
- The race-end results screen calls `46C220` through its own binding
  (`race_end_runtime.cpp`), which still expects a translated `481A40` and
  lacks `4BB9E0` / `50ECC0`: expect it to latch at the end of a run.
- **Photo request (type `18`) is not ported** (latches the traffic module when
  it starts: `race traffic latched: … 0x89bd60`). Its reset `507100` →
  `507180` makes a 256x256 render target (`D3DXCreateTexture` `4393F4`,
  `GetSurfaceLevel`) and its display `507DE0` re-renders the whole 3D scene
  from a photo camera into it (`449220` mid-display, camera `483C10` /
  `484DF0` / `484C90`), then the HUD shows it. Needs an offscreen scene pass in
  the native renderer. The first real one is Palm Beach reverse.
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

**Holly 2 ("MIX 2 COURSE", 2026-11-19, not yet confirmed by the owner).** Not in the retail
game: OutRun2006Tweaks' `EnableHollyCourse2` (owner's setting 3) turns on the PSP version's second
Holly course, and the owner's PC videos ("Holly Mode B") are of it. Port option `holly_course2=`
(0..3, default 3, as Tweaks). The EXE still holds its whole eighth entry (5CE480 position 274/365,
icons 14/15, pictures 4/5, nav row 7, title frame 73, footer 2) and START already loads
`Req_Course_MIX_PSP` (lane 3 of 84B7E0); it's group 7 in root `+20C` bits 8..10, and 4EEB50
selector 7 gives `event_config_7f94c4` {2, …, 2} (MIX_PSP course list, Holly's voices). Port
pieces: `frontend_category_count`, the categories screen's 8th icon with the large picture moved
to a port-side `+674` slot (`picture_674`, as Tweaks grows the object), lane 3 in
`LicenseProgressTables`. From the title: A ×3, Down, Right ×3, A ×5 (Metropolis, then
Coniferous Forest). It follows the PC video's timing (the same requests at the same times).

**Request objects drawn in 3D (food, math numbers, answer boards; 2026-11-19, not yet
confirmed by the owner).** They are `4295D0` sprites from the request objects' 2D displays
(`481A40`), replayed in the NAVI display. `4295D0` writes `[780068]` (read-only EXE here) and reads
the projection `95D8A0` (unmapped): the fault ended the whole NAVI display, so no request sprites
and, from the first one on, no HUD (TIME digits, speedometer). Both now mapped by the HUD
(`sprite_zbias_780068`, `projection_95d8a0`). Holly 1 (Easter Island, stage 0) had no HUD at all:
the NAVI init `45E050` reads quest 44, past 5A94D8's 41 records; the PC reads on into 5AC818.

**Request scoring, checked 2026-11-20.** The scoring is the bulk-translated PC code; what
matters is the services it reads. Findings:
- Math / memory / count (`10`/x) decide "correct" in `50BFD0`: the car's position across the road
  (43D920 run, 43D1D0 corners, 40EFA0) against the answer slot; each correct board adds 80 × 1.1 /
  count to the record's `+28`. The hearts shown are about 1.4 × the score at AAA (88 → 123). ZR-only
  runs differ from the PC's because the paths differ (different gates collected: the PC's answer
  boards read 11 | 7, the port's 12 | 8); with steering the port scores 0..108, so the check is
  not stuck.
- A stage with no mistakes earns `4FFAA0`'s bonus (10 × up to 5, plus 2 × (top speed − 250)): it
  needs the per-course mistake counters 0. They come from `4FB390`, which reads the player car's
  `+4` bits (0x100000 / 0x400000 car hit, 0x4000000, 0x80000), `+2F0` (crash) and `+DD4` (the car
  hit); the native car code sets them (`race_traffic.cpp` collisions, `pc_crash_entry`), and the
  manager sees them live at 7804B0.
- Penalties (`4FB4xx`) subtract hearts from the stage (`+3A20`), so the stage hearts can go down.

**`tools/request_sweep.py`** (2026-11-20) forces requests in parallel games (own homes under
`build/request-sweep/`) and reports per run the request's [req] states, latches, NAVI misses
and the traffic module's missing services: `--one-per-type`, `--type 10/3` (`10/` = every
sub-kind, `all` = all ~370 records), or real Holly 2 stages from the map with `--holly2
"node,list;…"` (node 0 = the whole course; `OR2_HOST_HOLD_TIME=1 SWEEP_END_CS=3000` to reach stage
5). `SWEEP_STEER="LEFT:20,RIGHT:20"` adds a repeating steering pattern. Results on 2026-11-20:
every type / sub-kind and all 104 type-`10` records run without a latch or NAVI miss except the
photo (`18`, unported); the 7 Holly 2 stages with math, ZR-only and with 4 steering patterns, and
4 whole-course runs to stage 4: no NAVI miss. **Not reproduced:** the owner saw a Holly 2 math
stage lose its objects and HUD on the iPhone (after the 780068 / 95D8A0 fixes). `OR2_TRACE` now
prints a `[hud] first miss <pc> at frame N` line the moment the NAVI display first misses something
(devices never write the exit summary), so a traced device session shows it.

**Heart Attack** (OutRun 2SP, variant 2), from the title, 2 s apart: A,
Right ×2, A ×3, Right, A ×2. Autoplay:
`script=400-403:A,520-523:RIGHT,640-643:RIGHT,760-763:A,880-883:A,1000-1003:A,1120-1123:RIGHT,1240-1243:A,1360-1363:A,1700-7400:ZR`
(race at about frame 1573).

## Sun, lens flare and glare (2026-11-18, not yet confirmed by the owner)

**Lens flare** (sun pass `40CBC0`, `race_course_passes.cpp`) was ported but never
showed. Four causes, all fixed:
- `lens_flare_offset.bin` is in `Media/`, the PC reads `\common\` (the PC bug
  OutRun2006Tweaks' `FixLensFlarePath` patches): now `\common\` then `\media\`.
  `[flare]` under `OR2_TRACE`.
- The renderer global `740C98` (screen y scale, `.data` 1.0) was 0: the sun's
  screen y was divided by 0, so it was never "on screen" (`PcRenderGlobals`).
- **The course environment was never applied in races**: `CourseEnvironmentRuntime`
  (START's `44AA80` records) had no per-frame caller, so the sun record kept
  defaults (range `899CC4` = 0: the sun sat at the camera). The renderer's
  `44A8DF` update now runs over the course records
  (`PcSceneRenderer::update_course_environment`, bound by `game_host`), which also
  gives the race the **course's own sun and fog** (and stage blends). Watch for
  lighting / fog changes the owner may notice.
- Occlusion queries: Metal had none (the base device answers "pending"), and the
  sun's occlusion object `570010` is an **indexed triangle fan** (type 6), which
  neither device drew. Metal queries now sum the per-draw visibility counts
  between BEGIN and END, delivered when the command buffer completes; both
  devices draw indexed fans.
- **Strength depends on anti-aliasing, as on the PC.** The sun's visibility
  `95AEF8` is occlusion *samples* / `73E2AC` (11500, raised only once a ratio
  passes 0.22, capped at 0.22). MSAA multiplies the samples: at the OutRun2SP
  start (16:9, 1920x1080) AA off gives 1437 samples (0.125, flare 0.034), MSAA 4x
  5748 (0.22, flare 0.059). The owner's PC reference shots are anti-aliased.
  **Port change (owner's call):** Metal reports query samples as a 2560x1440, 4x MSAA
  target would count them (the owner's modded Windows build: Tweaks sets 2560x1440,
  antialiasing 2), scaled by MSAA, resolution and eye count, so the start reaches 0.22
  on every device and setting (1920x1080 AA off: 10204 samples). The software device is
  unscaled.
- **Edge fade depends on resolution too (fixed, 2026-11-19).** `40CBC0` divides the sun's
  screen position (640x480 basis, camera `+B0` / `+B4`) by `740C94` / `740C98`, which the
  PC sets to width / 640 and height / 480 (`40EAED`). At the reference's 2560x1440 (4.0,
  3.0) the sun counts as near the centre, so the flare isn't faded at the edges; at 1.0
  the ghosts were ~2.4x weaker (start: flare 0.059 instead of 0.141) and invisible. The
  sun pass now uses 4.0 / 3.0 (the global stays 1.0 for everything else). The green
  ring left of the car at the OutRun2SP start then matches the owner's shot.
- **Car brightness strobing (owner, iPad, 2026-11-19; fixed, confirmed by the owner on the iPad).** The car's
  sun light (record `899CD8`) is scaled by its light rate `+58` (`46BD30`), which `4A45F0`
  sets from the road polygon's baked corner shades (`43D130`, root `780160`). The port read
  that table from the wrong place: `CourseCollisionPack::pc_coli0200` starts with a 4-byte
  size prefix, and the PC roots (`43DC06`) are `header + [header + field]` with the header
  after it. Reading `data + [data + 0x30]` gave another table (noise, the "repeating start
  shade" the owner saw), which the course lighting (diffuse 0.6) made a strobe. Fixed with
  `pc_coli0200_section` (player car shade, its surface codes `+0x24` for the wall code, and
  traffic shade, which all had it). Checked against the owner's PC video (OutRun2SP start,
  ZR held): per-frame car red change 1.15 mean / 5.0 p95 vs the PC's 1.22 / 3.7 (was 5.8 /
  34.7). `OR2_HOST_CAR_SHADE=1` logs `[car-shade]` (mean red of the car's pixels, every
  frame) for such comparisons. Port option `car_shade=0` keeps `+58` at 1.0 (no road shade).
  Also ported on the way: Tweaks' `FixIncorrectShading` (`fix_incorrect_shading=`, default
  1): double-sided lighting kinds 2 / 3 link the `VSULT_DOUBLE_*` fragments (cutscene
  characters, some stage models; the player car doesn't use them).
- **Sky / whole-screen flicker with the glare on (owner, iPad and Mac, 2026-11-19; fixed, confirmed
  by the owner on the iPad).** The bloom switched fully on and off for stretches of
  13 to 37 frames (presented frames, glare on minus glare off: +2 then exactly 0). Cause: the
  Metal device folds the constant `D3DRS_DEPTHBIAS` into clip z, and a bias left set by an
  earlier draw (-1.06e-5) pushed the glare composite (`415ED0`, a pretransformed quad at z 0,
  depth off) out of the clip volume. D3D9 biases after clipping, so on the PC it never removes a
  draw. The bias is now folded in only while `ZENABLE` is on with a depth surface
  (`metal_d3d9.mm` `flush`). Not the sun: its glow follows each occlusion result as on the PC
  (checked in the disassembly and against the owner's video: the PC's glow shrinks when palm
  fronds cover the sun), and an eased visibility tried first was taken out again.
  `OR2_HOST_SKY_SHADE=1` logs `[sky-shade]` (blue sky level and the top band's mean, every
  frame), but it reads the 3D frame, which missed this; compare presented frames instead
  (`OR2_MAC_SCREENSHOT_RANGE`, below). The probe frame numbers equal the autoplay input frames.
- **No fog in races (owner, 2026-11-19; fixed on the Mac, not yet confirmed).** Distant land
  (OutRun2SP's Golden Gate bridge at about 1'30" in the owner's video) was hazy on the PC and
  clear in the port. The fog records 7D3A10 / 7D3A2C / 7D3A48 have an on word at +0 that
  START's `4518A0` sets (`4518C0` clears it in the menu scenes); the port's START kept it in
  `start_mode.first_race_flags_7d3a10` only, so `40D870` never enabled fog. The course
  environment update now copies it into the records (`CourseEnvironmentBinding::fog_on_7d3a10`).
  Also `740C88` (the PC's DX/FOG setting, on in the owner's Tweaks build) is now 1
  (`g_pc_pixel_fog`, options.ini `pixel_fog=`): the stages' fog is exponential (mode 1,
  density ~0.0003), drawn as table fog; 0 takes `40D870`'s linear fallback.
- The sun pass also draws with the projection's near plane at 0.05 (Tweaks'
  `FixZBufferPrecision` does this around `40BE70`); no visible change measured.
- Trap when tracing: the first frames of a race have the flare at strength 0 (no
  occlusion result yet), so traces capped at the first N draws show alpha 0.
  Ghost positions match the reference (checked with the visibility forced to 1).
  The ghosts' strength is the flare material's diffuse alpha (`4056D0` →
  colour record `+0C` → c18), so they stay faint. `OR2_SUN_TRACE=1` logs `[sun]`
  screen position, samples / calibration and strengths every 30 passes.

**Glare / bloom** (`414E50` / `414F00`, new `src/platform/pc_glare.cpp`). On the
PC it is gated by `95AF09`, the second byte of the `DX/WINDOWED` value, so the
retail game never ran it (the Hi-Res folder's Tweaks DLL does **not** restore it;
only the flare path). Ported natively: bright pass `4152E0` (`ps 622348`, glow
record `8A8C18..8A8C5C` from `4160F0`), `[8A8C40]-1` blur passes `415630`
(`622428`, blend factor `[8A8C4C]`), composite `415ED0` (`6224B4`, blend from
`6237F0`, usually screen). Port option `glare=1` in options.ini (**default on** since the
exposure restoration below, 2026-11-19; it was off while it washed out),
`OR2_GLARE=0/1`; `[glare]` lines under `OR2_TRACE`. Device additions:
`stretch_rect` / `get_back_buffer` (Metal: a pass, the MSAA scene resolved first,
the left eye in stereo), `D3DBLEND_BLENDFACTOR` (14/15), drawing with no depth
surface, `set_rhw_full_canvas` (the composite covers the widescreen canvas).
**Look:** the bright pass keys on the back buffer's alpha, and opaque geometry
writes alpha 1 (sky 0), so on many stretches (OutRun2SP Palm Beach) the road and
buildings bloom to near white; that follows the PC code and data, with no
reference to compare against. The warm road glow in the owner's Tweaks shots (Legend) is the
sun pass's own full-screen glow sprite `570008` and the course lighting, not this pass.
**Why it washes out, and how Tweaks fixed it** (OutRun2006Tweaks `RestoreSkyGlow`,
`src/hooks_graphics.cpp`, 2026-10, newer than the owner's DLL). Frame alpha is meant to be
an HDR *exposure* per material (`clamp(((intensity+1)*0.25)*glow_param[1]*255)`, 63 for
ordinary scenery), written by the alpha-op fragment for key `10101230`/`4C00` (stage 4,
`c7.a`). On PC that fragment is **empty** in both tables (`61F240` entry 0 → `61ED30`,
`61F2B0` → `61F184`, checked), so opaque scenery keeps texture alpha 255 and blooms.
Tweaks' fixes: (1) that entry → `mul r0.a, r0.a, c7.a` (ps_1_4: `r4.a`); (2) the alpha-ref
compare / reassert immediates `40B027` / `40B039` 0x80 → 1, and per draw that writes the
exposure, `ALPHAREF = exposure / 2` (hook on `40AF80`, exposure from `410F4F`);
(2c) `408F56` `and al,1` → `xor al,al` (no material keeps its real alpha); (3) during
races the frame clear's alpha = `0.25 * [8A8C18] * 255` (D3D9 blends alpha with colour
factors; that value is the blend's fixed point); (4) `95AF09` = 1, glow targets sized
screen / `SkyGlowFactor` (4) with the extract / blur quads, two-step reduce, blur offsets
× width/640. The port's linker (`pixel_shader_40b200`) reads the same tables, but its
shaders run as pre-translated EXE fragments (`pc_shader_split`, generators), so (1) needs
a port fragment added to `tools/generate_pc_shaders.py` and the Metal / GLSL output.
**Ported 2026-11-19** (all only with the glare on, so `glare=0` renders as before):
`PORT_BLOBS` in `generate_pc_shaders.py` (ps11 `ps11_60`, ps14 `ps14_44`, addresses
`F0000000` / `F0000040`, tokens emitted inline), linked by `pixel_shader_40b200` for
key `10101230`/`4C00`; `apply_pixel_40af80` sets `ALPHAREF` 1, or half the material's
exposure (`g_material_exposure`, from `410DD0`) when the block writes it; `skip_final`
(408F56) is 0; `frame_begin_448db0` seeds the clear alpha in the race modes (0D, 10, 12,
13, 14, 1B, 22, 23). Not ported: Tweaks' glow buffers at screen / 4 and two-step reduce
(the port keeps 640x480 targets, close to Tweaks' 640x360 at 2560x1440; its blur offsets
over 640 equal Tweaks' width/640 scaling). To regenerate the shaders without the pinned
EXE, feed the generator the cache image (`Image.u32` over the decompressed
`OR2006C2C.cache`, base 0x400000); it reproduces the committed files byte for byte.
Switch / PS5 shader outputs changed too (new blobs) but were not built here.

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
