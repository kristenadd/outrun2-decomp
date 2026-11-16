# OutRun 2006 Coast 2 Coast on macOS (Apple silicon)

## Install

Put the application (`OutRunMacGame.app`) in your OutRun 2006 Coast 2 Coast installation folder
(Steam version) and open it. Started elsewhere, it asks for the game folder.

The first launch reads your `OR2006C2C.EXE`, checks it and saves the
prepared data in the game folder as `OR2006C2C.cache` (in
`~/Library/Application Support/OutRunMac/Saves` when the game folder is
read-only; text screen with the steps). Later launches start directly. Saves go to
`~/Library/Application Support/OutRunMac/Saves`.

## Save files

Folder: `~/Library/Application Support/OutRunMac/Saves`

| File | Contents |
| --- | --- |
| `License1.dat` | the player license |
| `common.dat` | the game's shared save data |
| `options.ini` | port settings (Options ▸ Settings) |
| `OutRunMac.log` | log of the last session |

`License1.dat` and `common.dat` use the PC game's format: saves from the
Windows game's `SaveGame` folder can be copied in, and the other way round.
Quit the game first, or it may write over the copied files when it saves.

## Build

    sh mac/tools/build-dependencies.sh
    cmake --preset mac-full
    cmake --build --preset mac-full

`build-dependencies.sh` downloads and builds SDL2, FFmpeg, Ogg, Vorbis and
pkgconf at the versions and SHA-256 listed in `tools/dependencies.lock`
(licences in `third_party/`). The application is
`build/macos/mac/OutRunMacGame.app`.

`source/` holds the platform layer (Metal renderer, SDL input and audio,
paths), `tools/` the Metal shader generators and the dependency scripts.

## Credits

Inspired and using tweaks by [OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks)
by emoose. See `THIRD_PARTY_NOTICES.md` at the repository root.

OutRun is a trademark of Sega. This project is not affiliated with or
endorsed by Sega or Sumo Digital.

This source code is be freely available for use, modification, and redistribution, subject to the licenses of the original projects it is based on.

Feel free to reuse any part of this project in your own work. Please give proper credit where due.

If you reuse our modifications, please keep an appropriate reference to this project and its contributors.

This project is developed and maintained in my free time.

If you enjoy the project and would like to support its development, donations are appreciated but entirely optional:

https://ko-fi.com/r4dius
