#pragma once
// visionOS: the game's screen composed with Metal into the frame slots the
// window shows (vision_bridge.h), in place of SDL's renderer (SDL2's UIKit
// video does not exist on visionOS). Called from the game thread only.
// Each frame has two eyes; a draw shows the same picture to both unless it
// gives a right-eye picture of its own (the stereo PC scene).
#include <cstdint>
#include <string>
namespace outrun::vision {
struct Vertex {float x,y,u,v,r,g,b,a;};   // canvas pixels, texture coordinates, colour
using Texture=void*;                      // a texture of the compositor (texture())
// A new frame on a canvas of this logical size (its aspect is the window's).
// Waits for a free slot (the window may still be copying the others).
void begin(unsigned canvas_width,unsigned canvas_height,unsigned pixel_height);
// An RGBA picture by key: uploaded once, or (update) again for this frame
// (one copy per slot, so the frames the window still reads keep theirs).
Texture texture(const void* key,const std::uint8_t* rgba,unsigned width,unsigned height,bool update);
void draw(Texture left,Texture right,const Vertex (&quad)[4],bool blend);
// Composes the frame on the GPU; the window takes it once the GPU is done.
bool submit(std::string& error);
// One RGBA picture over the whole window, letterboxed (the setup and notice screens).
void show_picture(const std::uint8_t* rgba,unsigned width,unsigned height);
// The window: its physical width (meters, 0 until known) and whether it is
// shown (its scene active), set by the application.
float window_width_meters();
bool window_active();
// The application asked the game to end (never on visionOS today).
bool terminating();
}
