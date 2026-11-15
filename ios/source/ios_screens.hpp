#pragma once
// iOS full-screen notices drawn with the first-launch setup screen
// (system/setup_screen): the game files are missing, or no game controller
// is connected. Each waits, pumping the system events, until its condition
// holds; false when the system ends the application meanwhile.
#include <functional>
struct SDL_Window;
namespace outrun::ios {
// SDL with video, events and game controllers (reference counted, kept until
// exit), the landscape and home-indicator hints, and the background watch.
bool sdl_ready();
// The UIScene life cycle (ios_scene.mm): waits for the application's window
// scene, and moves an SDL window into it (done for every window SDL shows).
bool wait_for_scene();
void attach_window(SDL_Window* window);
// Between "will enter background" and "did enter foreground": nothing may
// be submitted to Metal (iOS ends the application for it).
bool in_background();
// Set by the system's terminate notice.
bool terminating();
bool wait_for_game_files(const std::function<bool()>& found);
// The game is controller-only: true once a game controller is attached.
bool controller_connected();
// Shows the notice when needed; returns at once when a controller is there.
// over: the game's window, drawn over with its renderer (null before the game).
bool wait_for_controller(SDL_Window* over=nullptr);
}
