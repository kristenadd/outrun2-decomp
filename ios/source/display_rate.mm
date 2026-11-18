// iOS implementation of mac/source/display_rate.hpp: 120 on ProMotion
// iPhones and iPads (with CADisableMinimumFrameDurationOnPhone), 60 elsewhere.
#include "display_rate.hpp"
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace outrun::mac_runtime {
unsigned screen_max_frames_per_second(){
    @autoreleasepool {
        NSInteger fps=UIScreen.mainScreen.maximumFramesPerSecond;
        // Low Power Mode caps ProMotion at 60 Hz (maximumFramesPerSecond does not always say so).
        if(NSProcessInfo.processInfo.lowPowerModeEnabled)fps=std::min<NSInteger>(fps,60);
        static bool once=false;
        if(!once&&std::getenv("OR2_TRACE")){once=true;std::fprintf(stderr,"[ios] screen max %ld fps, low power mode %d\n",
            long(UIScreen.mainScreen.maximumFramesPerSecond),int(NSProcessInfo.processInfo.lowPowerModeEnabled));}
        return fps>0?unsigned(fps):60u;
    }
}
// A display link that does nothing but carry the preferred rate: the system then
// lets the game's Metal layer present at up to 120 Hz.
void screen_request_frames_per_second(unsigned fps){
    static unsigned requested=0;
    if(fps==requested)return;
    requested=fps;
    dispatch_async(dispatch_get_main_queue(),^{
        static CADisplayLink* link=nil;
        static NSObject* target=nil;
        if(!link){
            target=[NSObject new];
            link=[CADisplayLink displayLinkWithTarget:target selector:@selector(description)];
            [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        }
        const float f=float(fps);
        link.preferredFrameRateRange=CAFrameRateRangeMake(fps>60u?80.f:f,f,f);
        if(std::getenv("OR2_TRACE"))std::fprintf(stderr,"[ios] preferred frame rate %u\n",fps);
    });
}
}
