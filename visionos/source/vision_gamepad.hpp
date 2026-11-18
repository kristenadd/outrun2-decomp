#pragma once
// visionOS game controller, read straight from Apple's GameController
// framework every input sample (in place of SDL's MFi joystick, whose
// connect / disconnect handling runs on the main thread while the game polls
// from its own). The newest connected extended gamepad is used (the system's
// "current" one first). While the window's scene is not active (system UI or
// another window has the focus), visionOS stops delivering controller input:
// every control then reads released, so no held button (R2: accelerate)
// stays stuck on.
#include "input/dual_sense.hpp"
#include <string>
namespace outrun::vision {
// The pad's state into `pad` (buttons, sticks, triggers; L2 / R2 bits are the
// caller's). False without a controller (pad left neutral). `sdl_type`: the
// SDL_GameControllerType the controller corresponds to (button prompts), -1 without one.
bool gamepad_read(ps5::PadSample& pad,int& sdl_type);
// The connected controllers' names, ", " separated (empty: none).
std::string gamepad_names();
}
