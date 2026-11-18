// iOS adaptation of mac/source/platform_api.cpp: the same SDL pad mapping,
// with a game controller required (keyboards may add to it, never replace
// it), and no frame submitted while the application is in the background.
#include <SDL.h>
#include "platform_api.hpp"
#include "platform/button_prompts.hpp"
#include "mac_paths.hpp"
#include "ios_screens.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <algorithm>
#ifdef OR2_VISIONOS
#include "vision_gamepad.hpp"
#endif
namespace {
bool initialized=false,running=true;
std::string retail,cache;
SDL_GameController* controller=nullptr;
[[maybe_unused]] void open_controller(){   // iOS (visionOS reads GameController)
    if(controller&&!SDL_GameControllerGetAttached(controller)){SDL_GameControllerClose(controller);controller=nullptr;}
    if(!controller)for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){controller=SDL_GameControllerOpen(i);if(controller)break;}
}
void read_pad(outrun::ps5::PadSample& p){
    using namespace outrun::ps5;
    p=PadSample{};p.connected=true;
#ifdef OR2_VISIONOS
    // visionOS: the controller comes straight from GameController (vision_gamepad.hpp).
    {int type=-1;outrun::vision::gamepad_read(p,type);
     outrun::platform::button_prompts_set_style(outrun::platform::button_prompts_style_for_sdl_type(type));}
#else
    open_controller();
    // Button prompts follow the connected controller (keyboard art without one).
    outrun::platform::button_prompts_set_style(outrun::platform::button_prompts_style_for_sdl_type(controller?int(SDL_GameControllerGetType(controller)):-1));
    if(controller){
        auto axis=[&](SDL_GameControllerAxis a){const int v=SDL_GameControllerGetAxis(controller,a);return std::uint8_t(v<0?128+v*128/32768:128+v*127/32767);};
        p.left_x=axis(SDL_CONTROLLER_AXIS_LEFTX);p.left_y=axis(SDL_CONTROLLER_AXIS_LEFTY);
        p.right_x=axis(SDL_CONTROLLER_AXIS_RIGHTX);p.right_y=axis(SDL_CONTROLLER_AXIS_RIGHTY);
        p.l2=std::uint8_t(std::max(0,int(SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERLEFT)))*255/32767);
        p.r2=std::uint8_t(std::max(0,int(SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)))*255/32767);
        const struct {SDL_GameControllerButton key;u64 bit;} bindings[]={
            {SDL_CONTROLLER_BUTTON_A,Cross},{SDL_CONTROLLER_BUTTON_B,Circle},{SDL_CONTROLLER_BUTTON_X,Square},{SDL_CONTROLLER_BUTTON_Y,Triangle},
            {SDL_CONTROLLER_BUTTON_START,Options},{SDL_CONTROLLER_BUTTON_BACK,Share},{SDL_CONTROLLER_BUTTON_LEFTSHOULDER,L1},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,R1},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT,Left},{SDL_CONTROLLER_BUTTON_DPAD_RIGHT,Right},{SDL_CONTROLLER_BUTTON_DPAD_UP,Up},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,Down}};
        for(auto b:bindings)if(SDL_GameControllerGetButton(controller,b.key))p.buttons|=b.bit;
        // Device diagnostics ($OR2_TRACE): the controller's face buttons as SDL reports them.
        static const bool trace=std::getenv("OR2_TRACE")!=nullptr;static std::uint32_t last=0;
        if(trace){std::uint32_t now=0;for(int k=0;k<SDL_CONTROLLER_BUTTON_MAX;++k)if(SDL_GameControllerGetButton(controller,SDL_GameControllerButton(k)))now|=1u<<k;
            if(now&~last)for(int k=0;k<SDL_CONTROLLER_BUTTON_MAX;++k)if((now&~last)&(1u<<k))std::fprintf(stderr,"[pad] %s pressed\n",SDL_GameControllerGetStringForButton(SDL_GameControllerButton(k)));
            last=now;}
    }
#endif
    // A hardware keyboard (iPad) keeps the Mac bindings; Escape does not quit on iOS.
    const auto* keys=SDL_GetKeyboardState(nullptr);
    const struct {SDL_Scancode key;u64 bit;} bindings[]={
        {SDL_SCANCODE_LEFT,Left},{SDL_SCANCODE_RIGHT,Right},{SDL_SCANCODE_UP,Up},{SDL_SCANCODE_DOWN,Down},
        {SDL_SCANCODE_SPACE,Cross},{SDL_SCANCODE_RETURN,Cross},{SDL_SCANCODE_KP_ENTER,Cross},{SDL_SCANCODE_P,Options},{SDL_SCANCODE_BACKSPACE,Circle},{SDL_SCANCODE_DELETE,Circle},{SDL_SCANCODE_Q,L1},{SDL_SCANCODE_E,R1}};
    for(auto b:bindings)if(keys[b.key])p.buttons|=b.bit;
    if(keys[SDL_SCANCODE_LEFT])p.left_x=0;
    if(keys[SDL_SCANCODE_RIGHT])p.left_x=255;
    if(keys[SDL_SCANCODE_W])p.r2=255;if(keys[SDL_SCANCODE_S])p.l2=255;
    if(p.l2>127)p.buttons|=L2;if(p.r2>127)p.buttons|=R2;
}
// Holds the game (the frame stays on screen, the game clock is stopped by
// the wait) while it is in the background or has no controller.
void hold_while_unavailable(){
    for(;;){
        SDL_Event event;
        while(SDL_PollEvent(&event))if(event.type==SDL_QUIT)running=false;
        if(!running||outrun::ios::terminating()){running=false;return;}
        if(outrun::ios::in_background()){SDL_WaitEventTimeout(nullptr,250);continue;}
        if(outrun::ios::controller_connected())return;
        if(!outrun::ios::wait_for_controller(SDL_GetKeyboardFocus())){running=false;return;}
    }
}
}
bool application_initialize(std::string& error){
    if(!outrun::ios::sdl_ready()||SDL_InitSubSystem(SDL_INIT_AUDIO)<0){error=SDL_GetError();return false;}
    initialized=true;running=true;
    // Controller-only: nothing starts before one is connected.
    if(!outrun::ios::wait_for_controller()){error="closed before a game controller was connected";return false;}
    return true;
}
void application_shutdown(){if(controller)SDL_GameControllerClose(controller);controller=nullptr;if(initialized)SDL_Quit();initialized=false;running=false;}
bool application_running(){return running;}
void platform_sleep_ns(s64 ns){if(ns>0)std::this_thread::sleep_for(std::chrono::nanoseconds(ns));else std::this_thread::yield();}
std::string application_home(int argc,char** argv){
    if(const char* value=std::getenv("OR2_GAME_ROOT"))retail=value;
    for(int i=1;i<argc;++i){const std::string a=argv[i];if(a.rfind("--data=",0)==0)retail=a.substr(7);}
    outrun::mac::Paths paths;std::string error;
    if(!outrun::mac::user_paths(paths,error))throw std::runtime_error(error);
    if(retail.empty())throw std::runtime_error("game folder not set");
    // The EXE image goes next to the game files (Documents is writable); this
    // folder only takes it when the game folder is read-only.
    cache=paths.saves;
    return paths.saves;
}
std::string application_retail_root(){return retail;}
std::string application_cache(){return cache;}
void consoleInit(void*){std::setvbuf(stdout,nullptr,_IOLBF,0);}void consoleUpdate(void*){std::fflush(stdout);}void consoleExit(void*){std::fflush(stdout);}
void padConfigureInput(int,u64){}void padInitializeDefault(PadState* p){*p=PadState{};}
void padUpdate(PadState* p){
    hold_while_unavailable();
    read_pad(p->sample);const u64 held=p->sample.buttons;p->down=held&~p->previous;p->up=p->previous&~held;p->previous=held;
}
bool padIsConnected(const PadState* p){return p->sample.connected;}
u64 padGetButtons(const PadState* p){return p->sample.buttons;}u64 padGetButtonsDown(const PadState* p){return p->down;}u64 padGetButtonsUp(const PadState* p){return p->up;}
HidAnalogStickState padGetStickPos(const PadState* p,unsigned stick){const int x=stick?p->sample.right_x:p->sample.left_x,y=stick?p->sample.right_y:p->sample.left_y;return {(x-128)*256,-(y-128)*256};}
