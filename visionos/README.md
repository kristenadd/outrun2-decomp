# OutRun 2006 Coast 2 Coast on Apple Vision Pro

A native visionOS app (not the iPad app in compatibility mode): the Mac / iOS
game in one resizable window, drawn in **stereoscopic 3D**. It needs visionOS
2 or later.

- **Window, not full screen.** The game plays in an ordinary visionOS window.
  You can move it and resize it from its corners. It keeps the aspect chosen in
  **Options ▸ Settings ▸ ASPECT RATIO**: **4:3** or **16:9**. There is no FULL
  SCREEN choice and no immersive mode.
- **3D.** The road, cars and scenery have depth behind the window. Menus, the
  HUD and full-screen effects stay on the window's surface.
  **Options ▸ Settings ▸ 3D DEPTH** sets the strength: **OFF** (flat), **1** to
  **5** (default **3**). At 5 the farthest scenery appears at its real
  distance. Lower levels flatten the depth.
- **90 frames per second** (Options ▸ Settings ▸ FRAME RATE; 60 also
  offered), to match the headset's 90 Hz display. The game still runs at
  60 Hz and draws the frames in between by interpolation.
- **A game controller is required**, as on iOS (Xbox, PlayStation, MFi). There
  are no hand or keyboard controls. The controller drives the window you are
  looking at.

## Install the game files

The app needs the files of your own copy of the game (Steam version),
including `OR2006C2C.EXE`, in its **OutRun** folder (the same layouts as on
iOS, see `ios/README.md`):

- **Files** app on the Vision Pro: **On My Apple Vision Pro ▸ OutRun**. Copy the
  game folder there from iCloud Drive, or AirDrop it from a Mac.
- From a Mac with Xcode (the device paired in Xcode's Devices window):

  ```bash
  xcrun devicectl device copy to --device <UDID> --domain-type appDataContainer \
    --domain-identifier <bundle id> --source "<game folder>" --destination Documents
  ```

Until the files are there the window shows "Game files not found" and starts
by itself once they are. Saves go to `Library/Application Support/OutRun/Saves`,
as on iOS.

## Build

```bash
sh ios/tools/build-dependencies.sh xros xrsimulator
```

That builds SDL2 (without its video layer, see below), Ogg, Vorbis and FFmpeg
for the device and the simulator into `build/ios-dependencies/xros` /
`xrsimulator`.

Apple Vision Pro (Xcode project, scheme **OutRunVision**, Release):

```bash
cmake --preset visionos-device -DOR2_VISIONOS_TEAM=<team id> -DOR2_VISIONOS_BUNDLE_ID=<unique id>
cmake --build --preset visionos-device
```

The app is `build/visionos-device/visionos/Release-xros/OutRun.app`. Install
and launch it as on iOS (`xcrun devicectl device install app …`,
`xcrun devicectl device process launch --console …`), or open the Xcode
project and run **OutRunVision**.

Simulator (a visionOS simulator runtime: `xcodebuild -downloadPlatform visionOS`):

```bash
cmake --preset visionos-simulator
cmake --build --preset visionos-simulator
xcrun simctl install <device> build/visionos-simulator/visionos/Release-xrsimulator/OutRun.app
```

Symlink a game folder into the app container's `Documents/` as for the iOS
simulator. The simulator shows one eye (the left).

## Icon

`visionos/Assets.xcassets/AppIcon.solidimagestack` holds the three layers of
the visionOS icon, made from `icon.png` by `visionos/tools/make_icon.swift`
(macOS 14+, Vision framework). **Front** is the car (the picture's subject
mask). **Middle** is the "OutRun 2006" logo (the subject of the picture's top
band). **Back** is the scene, with those two softened away so their edges
don't show twice when the layers move. To regenerate after changing `icon.png`:

```bash
swift visionos/tools/make_icon.swift icon.png visionos/Assets.xcassets/AppIcon.solidimagestack
```

## How it works

- `visionos/source/OutRunApp.swift`: the SwiftUI app. It has one `WindowGroup`
  window (plain style) holding a RealityKit plane the size of the window. The
  window resizes uniformly
  (`UIWindowScene.GeometryPreferences.Vision`, `.uniform`) at the aspect the
  game asks for.
- `visionos/source/StereoMaterial.usda`: the plane's unlit ShaderGraph
  material. RealityKit's camera-index switch shows the left eye's texture to
  the left eye and the right one's to the right (two `LowLevelTexture`s).
- `mac/source/metal_d3d9.mm` (`set_stereo`): the 3D renderer draws the back
  buffer as two layers in one pass (Metal vertex amplification). Each eye's
  clip-space x moves by ∓ *s* · (*w* − *C*), the automatic-stereo method.
  *C* = 2.5 view units has no parallax. Things farther away go behind the window
  and nearer things come out of it (capped). Screen-space draws (*w* = 1: the 2D
  layer, fades) stay flat on the window. *s* follows the window's physical width,
  so the parallax at infinity is 3D DEPTH / 5 × 63 mm. Offscreen targets
  (reflections) stay mono.
- `visionos/source/vision_display.mm`: replaces SDL's renderer, which does not
  exist on visionOS (SDL2's UIKit video layer uses `UIScreen`). It composes the
  PC frame (per eye), movies and menus with Metal into one of three frame slots.
  The app's RealityKit update copies the newest slot into the eye textures.
- `visionos/source/vision_main.mm`: the game's `main()` (`mac/source/main.cpp`,
  built as `or2_game_main`) runs on a thread of its own with a 16 MB stack.
  SwiftUI keeps the main thread.
- `visionos/source/vision_screens.cpp`: the "Game files not found" and "Connect
  a game controller" notices, and the first-launch setup screen, drawn in the
  window. Leaving the window (scene not active) holds the game, as iOS's
  background hold does.
- `visionos/source/vision_gamepad.mm`: the controller, read from Apple's
  GameController every frame (not SDL). The whole game view takes the
  controller's events (`handlesGameControllerEvents`, visionOS 26; the window's
  `GCEventInteraction` before). Otherwise visionOS turns them into pinches for
  whatever you look at. While the window isn't the active scene, every
  control reads released.
- Reused: `ios/source/platform_api.cpp` (pad mapping, controller gate; visionOS reads the pad above),
  `ios/source/ios_paths.mm` (Documents / saves), and the Mac renderer, audio
  and game host.

Testing overrides: `OR2_VISION_SEPARATION=<s>` (per-eye NDC shift at infinity)
and `OR2_VISION_CONVERGENCE=<C>` replace the 3D DEPTH values.
`OR2_IOS_ALLOW_NO_CONTROLLER=1` waives the controller gate (as on iOS).
