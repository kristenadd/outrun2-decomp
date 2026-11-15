#include "ios_screens.hpp"
#include "setup_display.hpp"
#include "system/setup_screen.hpp"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace outrun::ios {
namespace {
std::atomic<bool> background{false},quitting{false};
// iOS delivers these synchronously from the application delegate: the flag
// is set before any later frame can reach Metal.
int SDLCALL watch(void*,SDL_Event* e){
    switch(e->type){
    case SDL_APP_WILLENTERBACKGROUND:case SDL_APP_DIDENTERBACKGROUND:background=true;break;
    case SDL_APP_WILLENTERFOREGROUND:break;
    case SDL_APP_DIDENTERFOREGROUND:background=false;break;
    case SDL_APP_TERMINATING:case SDL_QUIT:quitting=true;break;
    case SDL_WINDOWEVENT:
        if(e->window.event==SDL_WINDOWEVENT_SHOWN||e->window.event==SDL_WINDOWEVENT_EXPOSED)attach_window(SDL_GetWindowFromID(e->window.windowID));
        break;
    default:break;
    }
    return 1;
}
// Testing in the simulator without a controller: OR2_IOS_ALLOW_NO_CONTROLLER=1
// (simctl launch with SIMCTL_CHILD_OR2_IOS_ALLOW_NO_CONTROLLER=1).
bool controller_check_waived(){
    const char* v=std::getenv("OR2_IOS_ALLOW_NO_CONTROLLER");
    return v&&*v&&*v!='0';
}
// One notice: draws it, then pumps the events until done() or the end. Over
// the game (over: its window), it is drawn with the game's own renderer (a
// second window would stay behind the game's on iOS); the game redraws its
// frame when it continues. Before the game, in a window of its own.
bool notice(const std::string& title,const std::vector<std::string>& lines,const std::function<bool()>& done,SDL_Window* over=nullptr){
    if(done())return true;
    platform::SetupScreen screen;
    screen.set_error(title,lines);
    screen.render(0.0);
    SDL_Renderer* renderer=over?SDL_GetRenderer(over):nullptr;
    SDL_Texture* texture=renderer?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,screen.width(),screen.height()):nullptr;
    if(texture){
        SDL_UpdateTexture(texture,nullptr,screen.pixels(),screen.width()*4);
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
        bool shown=false;
        while(!quitting){
            if(!background&&!shown){
                SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer,texture,nullptr,nullptr);SDL_RenderPresent(renderer);shown=true;
            }
            SDL_Event e;
            while(SDL_PollEvent(&e))if(e.type==SDL_QUIT)quitting=true;
            if(done())break;
            if(background)shown=false;
            SDL_Delay(100);
        }
        SDL_DestroyTexture(texture);
        return !quitting;
    }
    mac_runtime::SetupDisplay display;
    bool shown=false;
    while(!quitting){
        if(!background&&!shown){display.show(screen);shown=true;}
        if(!display.pump())quitting=true;
        if(done())break;
        if(background)shown=false;   // redrawn on return
        SDL_Delay(100);
    }
    display.close();
    return !quitting;
}
}
bool sdl_ready(){
    static bool ready=false;
    if(ready)return true;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR,"2");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK,"0");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY,"playback");
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)<0)return false;
    SDL_AddEventWatch(watch,nullptr);
    wait_for_scene();
    ready=true;return true;
}
bool in_background(){return background;}
bool terminating(){return quitting;}
bool wait_for_game_files(const std::function<bool()>& found){
    if(!sdl_ready())return false;
    unsigned ticks=0;
    return notice("Game files not found",{
        "OutRun needs the files of your own copy of OutRun 2006 Coast 2 Coast (Steam version).",
        "",
        "Copy the contents of the game's installation folder, including OR2006C2C.EXE, into the OutRun folder of this device:",
        "- from a Mac: Finder, select this device, Files tab, OutRun;",
        "- from a PC: Apple Devices (or iTunes), File Sharing, OutRun;",
        "- or on this device: Files app, On My iPhone / On My iPad, OutRun.",
        "",
        "The game starts by itself once the files are in place."},
        [&]{return (ticks++%10u)==0u&&found();});   // checks about once a second
}
bool controller_connected(){
    if(controller_check_waived())return true;
    static std::string last;
    std::string names;
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){
        const char* n=SDL_GameControllerNameForIndex(i);
        names+=std::string(names.empty()?"":", ")+(n?n:"?");
    }
    if(names!=last){std::fprintf(stderr,"[ios] game controllers: %s\n",names.empty()?"none":names.c_str());last=names;}
    return !names.empty();
}
bool wait_for_controller(SDL_Window* over){
    if(!sdl_ready())return false;
    return notice("Connect a game controller",{
        "OutRun 2006 Coast 2 Coast is played with a game controller: Xbox, PlayStation (DualShock 4, DualSense), Switch Pro or MFi.",
        "",
        "Pair it in Settings > Bluetooth, or connect it with a cable.",
        "",
        "The game continues as soon as a controller is connected."},
        []{SDL_PumpEvents();return controller_connected();},over);
}
}
