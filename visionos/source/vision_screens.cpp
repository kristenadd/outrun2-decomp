// visionOS implementation of ios/source/ios_screens.hpp (the notices and the
// background hold) and of mac/source/setup_display.hpp (the first-launch
// setup screen): both drawn in the game's window with the compositor
// (vision_compositor.hpp). SDL runs without video here: events, game
// controllers and (platform_api.cpp) audio only.
#include "ios_screens.hpp"
#include "setup_display.hpp"
#include "vision_compositor.hpp"
#include "vision_gamepad.hpp"
#include "system/setup_screen.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace outrun::ios {
namespace {
bool controller_check_waived(){
    const char* v=std::getenv("OR2_IOS_ALLOW_NO_CONTROLLER");
    return v&&*v&&*v!='0';
}
// One notice in the window until done(); redrawn when the window comes back.
bool notice(const std::string& title,const std::vector<std::string>& lines,const std::function<bool()>& done){
    if(done())return true;
    platform::SetupScreen screen;
    screen.set_error(title,lines);
    screen.render(0.0);
    bool shown=false;
    while(!terminating()){
        if(!in_background()&&!shown){vision::show_picture(reinterpret_cast<const std::uint8_t*>(screen.pixels()),unsigned(screen.width()),unsigned(screen.height()));shown=true;}
        SDL_Event e;while(SDL_PollEvent(&e)){}
        if(done())break;
        if(in_background())shown=false;
        SDL_Delay(100);
    }
    return !terminating();
}
}
bool sdl_ready(){
    static bool ready=false;
    if(ready)return true;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK,"0");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY,"playback");
    if(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)<0)return false;
    ready=true;return true;
}
bool wait_for_scene(){return true;}       // the SwiftUI window exists before the game starts
void attach_window(SDL_Window*){}
bool in_background(){return !vision::window_active();}
bool terminating(){return vision::terminating();}
bool wait_for_game_files(const std::function<bool()>& found){
    if(!sdl_ready())return false;
    unsigned ticks=0;
    return notice("Game files not found",{
        "OutRun needs the files of your own copy of OutRun 2006 Coast 2 Coast (Steam version).",
        "",
        "Copy the contents of the game's installation folder, including OR2006C2C.EXE, into the OutRun folder of this Apple Vision Pro:",
        "- in the Files app: On My Apple Vision Pro, OutRun (AirDrop or iCloud Drive from a Mac or a PC);",
        "- from a Mac with Xcode: xcrun devicectl device copy to (visionos/README.md).",
        "",
        "The game starts by itself once the files are in place."},
        [&]{return (ticks++%10u)==0u&&found();});   // checks about once a second
}
bool controller_connected(){
    if(controller_check_waived())return true;
    static std::string last;
    const std::string names=vision::gamepad_names();   // GameController (vision_gamepad.mm)
    if(names!=last){std::fprintf(stderr,"[visionos] game controllers: %s\n",names.empty()?"none":names.c_str());last=names;}
    return !names.empty();
}
bool wait_for_controller(SDL_Window*){
    if(!sdl_ready())return false;
    return notice("Connect a game controller",{
        "OutRun 2006 Coast 2 Coast is played with a game controller: Xbox, PlayStation (DualShock 4, DualSense) or MFi.",
        "",
        "Pair it in Settings > Bluetooth.",
        "",
        "The game continues as soon as a controller is connected."},
        []{SDL_PumpEvents();return controller_connected();});
}
}

namespace outrun::mac_runtime {
void SetupDisplay::show(const platform::SetupScreen& screen){
    pump();
    vision::show_picture(reinterpret_cast<const std::uint8_t*>(screen.pixels()),unsigned(screen.width()),unsigned(screen.height()));
}
bool SetupDisplay::pump(){
    SDL_Event e;
    while(SDL_PollEvent(&e))if(e.type==SDL_QUIT)quit_=true;
    if(ios::terminating())quit_=true;
    return !quit_;
}
void SetupDisplay::wait_for_close(){
    while(!quit_){
        SDL_Event e;
        if(ios::terminating()){quit_=true;break;}
        if(!SDL_WaitEventTimeout(&e,100))continue;
        if(e.type==SDL_QUIT||e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_KEYDOWN)quit_=true;
    }
}
void SetupDisplay::close(){}
platform::SetupPlatform SetupDisplay::platform(){
    platform::SetupPlatform p;
    p.present=[this](const platform::SetupScreen& s){show(s);};
    p.keep_running=[this]{return pump();};
    p.wait_for_exit=[this]{wait_for_close();};
    p.exit_hint="Press a controller button to quit.";
    return p;
}
}
