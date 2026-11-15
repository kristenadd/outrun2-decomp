# OutRun 2006 Coast 2 Coast on iPhone and iPad

The Mac port (`mac/source`: Metal D3D9 device, SDL display and audio, game
entry) built for iOS / iPadOS 16 or later on arm64 devices and the Apple
silicon simulator. `ios/source` only replaces what is specific to the system:
the folders, the screen refresh rate, and the notices below.

**A game controller is required.** There are no touch controls. Xbox,
PlayStation (DualShock 4, DualSense), Switch Pro and MFi controllers work.
Until one is connected the game shows "Connect a game controller" and waits.
If the controller disconnects during play, the game stops on that notice and
picks up where it was once a controller is back. A hardware keyboard (iPad)
keeps the Mac key bindings but does not count as a controller.

## Install the game files

The app needs the files of your own copy of the game (Steam version), the
same folder the Mac app uses, including `OR2006C2C.EXE` (about 1 GB).
Copy them into the app's **OutRun** folder on the device:

- from a Mac: Finder, select the device, **Files** tab, drag the files (or
  the whole game folder) onto **OutRun**;
- from a PC: Apple Devices (or iTunes), **File Sharing**, **OutRun**;
- on the device: **Files** app, **On My iPhone / On My iPad**, **OutRun**.

The folder appears once the app has been opened once. The app looks for the
game in that folder or in any folder directly inside it, so either layout
works:

Copy the whole game folder (simplest from the Finder: drag the folder onto
**OutRun**):

    OutRun/
    ├── Copy your OutRun 2006 game files here.txt
    └── Outrun2006 Coast 2 Coast/
        ├── OR2006C2C.EXE
        ├── Cars/
        ├── Scripts/
        ├── Sound/
        ├── Sprite/
        ├── Stage/
        ├── Text/
        └── ...

Or copy only its contents:

    OutRun/
    ├── Copy your OutRun 2006 game files here.txt
    ├── OR2006C2C.EXE
    ├── Cars/
    ├── Scripts/
    ├── Sound/
    └── ...

`OR2006C2C.EXE` must sit next to the game's data folders (`Cars`,
`Scripts`, `Stage`, ...), as in the Steam installation. Do not go deeper
than one folder: `OutRun/Games/Outrun2006 Coast 2 Coast/` is not found.
The folder's name does not matter. Until it finds the
game it shows "Game files not found" and starts by itself once the copy is
done. At the first launch it checks the EXE and writes `OR2006C2C.cache`
next to the game files, as on the Mac. Saves and `options.ini` go to the
app's Application Support folder (kept in device backups).

## Build

Dependencies (SDL2, FFmpeg, Ogg, Vorbis, from the same pinned sources as the
Mac: `mac/tools/dependencies.lock`), for the device and the simulator:

    sh ios/tools/build-dependencies.sh

### Simulator

    cmake --preset ios-simulator
    cmake --build --preset ios-simulator

The app is `build/ios-simulator/ios/OutRun.app`. To run it with the game
files of the Mac build (the simulator shares the Mac's disk, so a link is
enough):

    xcrun simctl boot "iPad Pro 13-inch (M4)"
    xcrun simctl install booted build/ios-simulator/ios/OutRun.app
    docs=$(xcrun simctl get_app_container booted org.outrun.ios data)/Documents
    mkdir -p "$docs" && ln -sfn "$PWD/../Outrun2006 Coast 2 Coast" "$docs/Game"
    xcrun simctl launch booted org.outrun.ios

The simulator passes the Mac's game controllers through. Without one,
`SIMCTL_CHILD_OR2_IOS_ALLOW_NO_CONTROLLER=1 xcrun simctl launch ...` skips
the controller check (for testing only).

### Device

Signing needs an Apple developer account (a free one works, with 7-day
profiles). Generate the Xcode project with your team ID and a bundle
identifier of your own:

    cmake --preset ios-device -DOR2_IOS_TEAM=ABCDE12345 -DOR2_IOS_BUNDLE_ID=com.example.outrun
    open build/ios-device/OutRunSwitch_r155.xcodeproj

In Xcode, choose the **OutRunIOS** scheme (the only one the project has; do
not let Xcode autocreate schemes for the test targets) and your device, then
Run. The device project has a Release configuration only, so Run builds the
optimised game. Or from the command line:

    cmake --build --preset ios-device -- -allowProvisioningUpdates

`cmake --preset ios-device-check && cmake --build --preset ios-device-check`
builds an unsigned device binary (compile and link check only).

## Notes

- Same features as the Mac build: Options > Settings aspect ratio,
  resolution, MSAA / FXAA, and 120 fps on ProMotion iPhones and iPads.
- The app is landscape only and full screen (no iPad split view).
- Leaving the app (home, app switcher) holds the game: nothing is drawn in
  the background, and it continues on return.
- LAN races ask for local network permission the first time.
- Apps built with the iOS 27 SDK must use the UIScene life cycle (iOS stops
  them at launch otherwise). SDL2 predates it: `Info.plist.in` declares one
  window scene and `source/ios_scene.mm` moves each SDL window into it.
