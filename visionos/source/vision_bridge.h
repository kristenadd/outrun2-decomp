// The calls between the SwiftUI application (OutRunApp.swift) and the game
// (C++ / Objective-C++). Also the Swift bridging header of the OutRunVision target.
//
// The game runs on a thread of its own (or2_vision_start). It composes each
// frame, both eyes, into one of three frame slots (vision_display.mm); the
// application's RealityKit update, on the main thread, takes the newest slot,
// copies its eyes into the two textures of the window's stereo material and
// gives the slot back once the GPU has copied it.
#pragma once
#include <stdbool.h>
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Starts the game thread (once; later calls do nothing).
void or2_vision_start(void);
// The window's scene phase: false (background) holds the game.
void or2_vision_set_active(bool active);
// The scene is active (it has the controller's input): false reads every control released.
void or2_vision_set_focused(bool focused);
// The physical width of the window's game picture, in meters (the stereo
// shift keeps far objects' disparity under the distance between the eyes).
void or2_vision_set_window_width(float meters);
// The aspect (width / height) the game's canvas asks the window for: 4:3 or 16:9.
float or2_vision_window_aspect(void);
#ifdef __OBJC__
// The device the game renders with (the system default one).
id<MTLDevice> _Nonnull or2_vision_device(void);
// The newest composed frame: its slot (>= 0) and size, or -1 when none is new.
int or2_vision_take_frame(int* _Nonnull width,int* _Nonnull height);
// Encodes the copy of slot `slot`'s eyes into two textures of its size (any
// RGBA8 format; an sRGB one shows the game's colours unchanged).
void or2_vision_encode_frame(int slot,id<MTLCommandBuffer> _Nonnull command,id<MTLTexture> _Nonnull left,id<MTLTexture> _Nonnull right);
// The copy of `slot` finished (any thread): the game may compose into it again.
void or2_vision_release_frame(int slot);
#endif
#ifdef __cplusplus
}
#endif
