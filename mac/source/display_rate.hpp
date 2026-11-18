#pragma once
// The refresh of the screen the game shows on (Options > Settings FRAME RATE,
// src/enhancements/frame_rate.hpp): 120 on ProMotion and high-refresh
// displays, 60 elsewhere. Before the window exists: the main screen.
namespace outrun::mac_runtime {
unsigned screen_max_frames_per_second();
// The frames per second the game presents (FRAME RATE). iOS: a Metal layer on a
// ProMotion iPhone presents at 60 Hz until the app asks for more with a display
// link's preferred frame rate range. Elsewhere it does nothing.
void screen_request_frames_per_second(unsigned fps);
}
