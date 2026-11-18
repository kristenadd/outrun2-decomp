# Status #

It is not finished yet, so bugs are to be expected.
Some menus are incorrect, and some visual effects, performance-related aspects,
game behaviors, and functionality are not final yet.
**Clarissa mode is a work in progress:** it loads and races, but parts of it
(some mission items and the end-of-run results) are not ported yet.
Some early enhancements have already been added, including:
- multiple anti-aliasing methods
- multiple resolution options
- widescreen support
- 120 FPS support

Once the decompilation is complete and the remaining issues are fixed,
additional enhancements will be added, including features from at least:
https://github.com/emoose/OutRun2006Tweaks

# OutRun 2006 Coast 2 Coast: native port

A native port of the PC version of OutRun 2006 Coast 2 Coast (Sumo Digital /
Sega, 2006) to Nintendo Switch, macOS (Apple silicon), iOS / iPadOS, PS5
(homebrew) and Xbox One / Series (Developer Mode). The game code is a reconstruction of the
PC executable in C++ (part of it still translated from x86 by `tools/x86tr`,
the rest written by hand); the platform layer (graphics, audio, input,
files, network) is native to each system.

![PS5 at 120 Hz](images/ps5pro-120hz-1.jpg)
![Switch in handheld mode](images/switch_handheld-1.jpg)

This repository holds code only. **It contains no game data and no part of
the original executable.** To play, you need your own copy of the game
(Steam version).

## What you need from your game

- The folder of your OutRun 2006 Coast 2 Coast installation (Steam).
- Its `OR2006C2C.EXE`. The port reads code tables and constants from it at
  the first launch, checks every range it uses against SHA-256 hashes
  (`src/system/exe_image_ranges.inc`), then keeps a prepared image,
  `OR2006C2C.cache`, in the game folder. Later launches only read that file.

The game files are read in place and never modified; `OR2006C2C.cache` can be
deleted at any time and is rebuilt at the next launch.

Save files (`License1.dat`, `common.dat`, `options.ini`) are kept outside the
game folder:

- macOS: `~/Library/Application Support/OutRunMac/Saves` ([details](mac/README.md#save-files))
- iOS / iPadOS: `Library/Application Support/OutRun/Saves` in the app's data
  container ([details and how to copy them](ios/README.md#save-files))

## Controller button prompts (optional)

The game shows keyboard keys in its prompts ("ENTER Select", the title's
PRESS ENTER). With controller art in the game folder, the port shows Xbox,
PlayStation or Switch buttons instead, following the connected controller
(keyboard art when none is connected). The art is not part of this
repository; download it once into your game folder:

```bash
sh tools/button_prompts/fetch.sh "<game folder>"
```

Add `xbox`, `playstation` or `switch` to fetch only some styles (about 10 MB
each). The files go to `textures/load/<Xbox|PlayStation|Switch>/` in the game
folder, the layout OutRun2006Tweaks uses, so a copy of the pack's `Release`
folders works too. For iOS / iPadOS, run the script on an empty folder and
copy its `textures` folder next to the game files in the app's files.
`OR2_BUTTON_PROMPTS=xbox|playstation|switch|keyboard` forces a style.

## Hi-res textures (optional)

The port reads texture packs made for
[OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks) (its
`UITextureReplacement` / `SceneTextureReplacement`), such as the
[OR2006Sprites](https://github.com/Sonic-TV/OR2006Sprites) HD GUI pack, which
redraws the menus, HUD, speedometers and fonts at 4x. Packs are not part of this
repository.

**Installing a pack:** put its `textures` folder next to the game files, so the
replacement files end up in `textures/load/`:

```text
<game folder>/
  OR2006C2C.EXE
  Cars/ Sprite/ Stage/ ...
  textures/
    load/
      spr_sprani_sumo_fe_cvt_Exst/1762489B_512x128.dds
      spr_sprani_game_cvt_Exst/...
      Keyboard/ PlayStation/ Switch/ Xbox/   (button prompts, see above)
```

- macOS: copy the folder into the game folder (inside the app bundle:
  `Contents/Resources/Game/textures`, or wherever your game folder is).
- iOS / iPadOS / visionOS: copy `textures` next to the game files in the app's
  files (Finder ▸ the device ▸ **Files**, or the Files app), the same way as the
  game folder ([details](ios/README.md)). From a Mac with `devicectl`, one
  folder per call:

  ```bash
  xcrun devicectl device copy to --device <udid> --domain-type appDataContainer \
    --domain-identifier <bundle id> --source "textures/load/<folder>" \
    --destination "Documents/<game folder>/textures/load/<folder>"
  ```

Nothing else is needed: at launch the port lists `textures/load`, and every
texture that has a replacement (named `<hash>_<width>x<height>.dds`, the hash of
the original texture, in any folder) is swapped in. Menu art may show at its
original resolution for a moment before the hi-res version appears. Only the
EXE and data of your Steam installation are used; you don't need
OutRun2006Tweaks itself, and a game folder already set up for it works as is
(its `textures` folder is the one to copy).

**Resolution and memory:** the art is kept at up to `hd_textures` times the
original size, set in `options.ini` (next to the saves, [see above](#what-you-need-from-your-game);
on iOS it is not in the Files app, [copy it with devicectl](ios/README.md#save-files)):

```text
hd_textures=4   # 1..16: the largest scale kept; 0 turns packs off
```

The default is 4 on macOS and 2 on iOS, iPadOS and visionOS. Larger art takes
more memory, and menu art stays loaded once shown: the HD GUI pack's main menus
alone take about 320 MB at 2x and 1.3 GB at 4x. 2x has been tested on an iPhone
15 Pro Max, an iPad mini 6 (4 GB) and an Apple Vision Pro. If a device runs out
of memory, use `hd_textures=1` (the pack's art scaled to the original size).
`OR2_HD_TEXTURES=<n>` overrides the setting for one launch; with `OR2_TRACE=1`
the log has a `[tex]` line for every replaced texture.

## Layout

| Folder | Content |
| --- | --- |
| `src/driving`, `src/platform` | the game code, ported function by function (`*_tr.cpp`: code still translated from x86) |
| `src/*.cpp` (`control`, `asset_views`, `pc_pmt`) | input control, asset views and the PC model (PMT) loader shared by the game code |
| `src/recomp` | runtime of the translated code (`x86rt.hpp`: guest registers, memory, flags) |
| `src/system` | files, clocks, EXE image loading and checking, first-launch setup screen |
| `src/ports` | in-memory extraction of the Steam `OR2006C2C.EXE` (see its `README.md`) |
| `src/runtime` | the shared game runner (`game_host`) used by every platform |
| `src/input` | pad and lifecycle adapter shared by the Mac, iOS and PS5 builds |
| `src/enhancements` | port additions kept apart from the reconstructed game: extra Options > Settings rows (aspect ratio, resolution, anti-aliasing, frame rate); each platform lists what it supports (`enhancements_*.cpp` in its `source/`) |
| `switch/`, `mac/`, `ps5/`, `xbox/` | one folder per platform, same layout in each |
| `ios/` | iOS / iPadOS: reuses the Mac platform code (`mac/source`); `ios/source` holds only the iOS parts (folders, scene life cycle, controller notice) |
| `visionos/` | Apple Vision Pro: the iOS game in a resizable stereoscopic 3D window (SwiftUI / RealityKit; `visionos/source`) |
| `tests/` | unit and regression tests (CTest) |
| `tools/` | EXE image tools, leak check, x86 translator, decompilation tracking, generators, PS5 build and package scripts |
| `images/` | screenshots |

Each platform folder has `source/` (platform code), `tests/`, `tools/`,
`third_party/` where needed, its build file and a `README.md` on how to
build and install it. Shader sources are in `switch/shaders` and
`ps5/shaders`; the Mac, PS5 and Xbox builds generate their shaders from
them (Metal, SPIR-V, HLSL). `ps5/title` builds the PS5 native title,
`ps5/sce_sys` holds its tile and backgrounds, `xbox/package` the UWP
manifest and logos.

`icon.png` and `bg.png` are the project's icon and background.
`tools/make_icons.ps1` derives each platform's images from them, at the
size and format each one expects: the Switch NRO icon, the PS5 tile and
backgrounds, the Xbox package logos and the Mac application icon.

## Building

| Platform | Command | Details |
| --- | --- | --- |
| Nintendo Switch | `make -C switch` | `switch/README.md` |
| macOS | `cmake --preset mac-full` | `mac/README.md` |
| iOS / iPadOS | `sh ios/tools/build-dependencies.sh`, then `cmake --preset ios-device` (or `ios-simulator`) | `ios/README.md` |
| visionOS | `sh ios/tools/build-dependencies.sh xros xrsimulator`, then `cmake --preset visionos-device` (or `visionos-simulator`) | `visionos/README.md` |
| PS5 | `Build-PS5.ps1` (Windows + WSL; makes the `.pkg`) | `ps5/README.md` |
| Xbox | `cmake` with `-DOR2_XBOX=ON` (Visual Studio 2026, UWP) | `xbox/README.md` |

Linux host (runner and tests):

    cmake -S . -B build -G Ninja -DOR2_EXE=<your OR2006C2C.EXE> \
        -DOR2_PC_ASSET_ROOT=<your game folder>
    cmake --build build
    ctest --test-dir build

Tests that need the game files take them from these two settings.

## Leak check

`tools/leak_check.py <your OR2006C2C.EXE> <built binary> --allow tools/leak_check_allow.txt`
verifies that no byte run of the original executable is compiled into a binary.

## Credits

Inspired and using tweaks by [OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks)
by emoose. See `THIRD_PARTY_NOTICES.md`.

Controller button prompts: the texture swap follows OutRun2006Tweaks by emoose
and [OutRun 2006 C2C Multi Input](https://github.com/hyp36rmax/multi-device-input) by hyp36rmax.
The prompt art is from [OR2006Sprites](https://github.com/Sonic-TV/OR2006Sprites)
(maintained by Sonic-TV): button prompt textures by
[Zoukambada](https://github.com/Zoukambada) (Vitor Kormann), the per-controller
release set and earlier prompts by [envido32](https://github.com/envido32/OR2006Sprites),
with contributions by Tenome. The script downloads it from the authors' repository;
it is not redistributed here.

Hi-res textures: the replacement format and loader design follow OutRun2006Tweaks
by emoose. The HD GUI pack is a collaborative work collected in
[OR2006Sprites](https://github.com/Sonic-TV/OR2006Sprites) by Sonic-TV, with art by
its contributors (credited in the pack's README); it is not redistributed here.

OutRun is a trademark of Sega. This project is not affiliated with or endorsed by Sega or Sumo Digital.

This source code is freely available for use, modification, and redistribution, subject to the licenses of the original projects it is based on.

Feel free to reuse any part of this project in your own work. Please give proper credit where due.

If you reuse our modifications, please keep an appropriate reference to this project and its contributors.

This project is developed and maintained in my free time.

If you enjoy the project and would like to support its development, donations are appreciated but entirely optional:

https://ko-fi.com/r4dius
