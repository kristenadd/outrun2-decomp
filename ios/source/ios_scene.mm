// iOS 27 SDK: applications must use the UIScene life cycle (Info.plist
// UIApplicationSceneManifest), which SDL2 predates. SDL creates its UIWindows
// with initWithFrame: (no scene), and such windows are never shown: each one
// is moved into the application's window scene when SDL shows it. SDL's
// background / foreground events need nothing here: SDL follows the
// UIApplication notifications, which iOS still posts in the scene life cycle.
#include "ios_screens.hpp"
#include <SDL.h>
#include <SDL_syswm.h>
#import <UIKit/UIKit.h>
namespace outrun::ios {
namespace {
UIWindowScene* window_scene(){
    for(UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        if([scene isKindOfClass:[UIWindowScene class]])return (UIWindowScene*)scene;
    return nil;
}
}
bool wait_for_scene(){
    // The scene connects while the run loop turns after launch (SDL may start
    // the game first): up to 5 seconds.
    for(int i=0;i<500&&!window_scene();++i)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,false);
    return window_scene()!=nil;
}
void attach_window(SDL_Window* window){
    if(!window)return;
    SDL_SysWMinfo info;SDL_VERSION(&info.version);
    if(!SDL_GetWindowWMInfo(window,&info)||info.subsystem!=SDL_SYSWM_UIKIT)return;
    UIWindow* ui=info.info.uikit.window;
    UIWindowScene* scene=window_scene();
    if(!ui||!scene||ui.windowScene==scene)return;
    ui.windowScene=scene;
    if(!ui.hidden)[ui makeKeyAndVisible];
}
}
