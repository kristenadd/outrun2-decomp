// visionOS game controller (vision_gamepad.hpp) on GameController.
#include "vision_gamepad.hpp"
#include "vision_bridge.h"
#import <GameController/GameController.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace outrun::vision {
namespace {
using namespace ps5;
std::atomic<bool> g_focused{true};
std::mutex g_mutex;
GCController* g_controller;   // the controller read (main thread picks it, under g_mutex)
std::string g_names;          // the connected extended gamepads (under g_mutex)
const bool g_trace=std::getenv("OR2_TRACE")!=nullptr;
std::atomic<std::uint64_t> g_changes{0};   // diagnostics: value-change callbacks the system delivered

// The system's current controller, else the last connected one with an extended gamepad.
void pick(){
    GCController* chosen=GCController.current.extendedGamepad?GCController.current:nil;
    std::string names;
    for(GCController* c in GCController.controllers)if(c.extendedGamepad){
        if(!chosen)chosen=c;
        const char* n=c.vendorName.UTF8String;names+=std::string(names.empty()?"":", ")+(n?n:"?");
    }
    // Diagnostics: count the system's value changes on every gamepad (frozen input shows as none).
    if(g_trace)for(GCController* c in GCController.controllers)if(c.extendedGamepad)
        c.extendedGamepad.valueChangedHandler=^(GCExtendedGamepad*,GCControllerElement*){g_changes.fetch_add(1,std::memory_order_relaxed);};
    std::lock_guard lock(g_mutex);g_names=names;
    if(chosen!=g_controller&&g_trace)std::fprintf(stderr,"[pad] controller: %s\n",chosen?chosen.vendorName.UTF8String?:"?":"none");
    g_controller=chosen;
}
// Notifications are posted on the main thread; the first read installs them there.
void watch(){
    static std::once_flag once;
    std::call_once(once,[]{
        dispatch_async(dispatch_get_main_queue(),^{
            auto* center=NSNotificationCenter.defaultCenter;
            for(NSNotificationName name in @[GCControllerDidConnectNotification,GCControllerDidDisconnectNotification,
                                             GCControllerDidBecomeCurrentNotification,GCControllerDidStopBeingCurrentNotification])
                [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){pick();}];
            pick();
        });
    });
}
std::uint8_t axis(float v){   // -1 .. 1 to 0 .. 255, 128 centred (as the SDL mapping)
    if(!(v==v))return 128;
    v=v<-1.f?-1.f:v>1.f?1.f:v;
    return std::uint8_t(v<0?128.f+v*128.f:128.f+v*127.f);
}
std::uint8_t trigger(float v){if(!(v==v)||v<=0)return 0;return std::uint8_t(v>=1?255:v*255.f);}
int sdl_type_of(GCController* c){
    // SDL_GameControllerType: 2 Xbox One, 4 PS4, 7 PS5, 5 Switch Pro.
    GCExtendedGamepad* g=c.extendedGamepad;
    if([g isKindOfClass:GCDualSenseGamepad.class])return 7;
    if([g isKindOfClass:GCDualShockGamepad.class])return 4;
    if([g isKindOfClass:GCXboxGamepad.class])return 2;
    NSString* category=c.productCategory.lowercaseString;
    if([category containsString:@"switch"]||[category containsString:@"joy-con"])return 5;
    return 2;
}
}

bool gamepad_read(ps5::PadSample& p,int& sdl_type){
    watch();
    GCController* c;{std::lock_guard lock(g_mutex);c=g_controller;}
    sdl_type=-1;
    GCExtendedGamepad* g=c.extendedGamepad;
    if(!g)return false;
    sdl_type=sdl_type_of(c);
    // Not the focused scene: visionOS sends the presses (and releases) elsewhere,
    // the values here stay frozen. Everything reads released meanwhile.
    if(!g_focused)return true;
    p.left_x=axis(g.leftThumbstick.xAxis.value);p.left_y=axis(-g.leftThumbstick.yAxis.value);   // SDL: y down
    p.right_x=axis(g.rightThumbstick.xAxis.value);p.right_y=axis(-g.rightThumbstick.yAxis.value);
    p.l2=trigger(g.leftTrigger.value);p.r2=trigger(g.rightTrigger.value);
    const struct {GCControllerButtonInput* b;std::uint32_t bit;const char* name;} buttons[]{
        {g.buttonA,Cross,"A"},{g.buttonB,Circle,"B"},{g.buttonX,Square,"X"},{g.buttonY,Triangle,"Y"},
        {g.buttonMenu,Options,"menu"},{g.buttonOptions,Share,"options"},{g.leftShoulder,L1,"L1"},{g.rightShoulder,R1,"R1"},
        {g.dpad.left,Left,"left"},{g.dpad.right,Right,"right"},{g.dpad.up,Up,"up"},{g.dpad.down,Down,"down"},
        {g.leftThumbstickButton,StickL,"L3"},{g.rightThumbstickButton,StickR,"R3"}};
    for(const auto& b:buttons)if(b.b&&b.b.isPressed)p.buttons|=b.bit;
    if(g_trace){   // every 2 s: what is read, what the system calls current, its deliveries
        static auto last_report=std::chrono::steady_clock::now();static std::uint64_t last_changes=0;
        const auto now=std::chrono::steady_clock::now();
        if(now-last_report>=std::chrono::seconds(2)){
            last_report=now;const auto changes=g_changes.load();
            std::fprintf(stderr,"[pad] state: read=%p current=%p controllers=%lu changes+%llu buttons=%x lt=%u rt=%u lx=%u focused=%d\n",
                (__bridge void*)c,(__bridge void*)GCController.current,(unsigned long)GCController.controllers.count,(unsigned long long)(changes-last_changes),
                p.buttons,p.l2,p.r2,p.left_x,int(g_focused.load()));
            last_changes=changes;
        }
    }
    if(g_trace){   // device diagnostics: newly pressed buttons
        static std::uint32_t last=0;const std::uint32_t now=p.buttons;
        for(const auto& b:buttons)if((now&~last)&b.bit)std::fprintf(stderr,"[pad] %s pressed\n",b.name);
        last=now;
    }
    return true;
}
std::string gamepad_names(){watch();std::lock_guard lock(g_mutex);return g_names;}
}

extern "C" void or2_vision_gamepad_reclaim(void){
    outrun::vision::watch();outrun::vision::pick();   // main thread (the tap)
}
extern "C" void or2_vision_set_focused(bool focused){
    if(outrun::vision::g_focused.exchange(focused)!=focused&&outrun::vision::g_trace)
        std::fprintf(stderr,"[pad] input %s\n",focused?"focused":"not focused: all released");
}
