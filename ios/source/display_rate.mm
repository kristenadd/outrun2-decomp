// iOS implementation of mac/source/display_rate.hpp: 120 on ProMotion
// iPhones and iPads (with CADisableMinimumFrameDurationOnPhone), 60 elsewhere.
#include "display_rate.hpp"
#import <UIKit/UIKit.h>

namespace outrun::mac_runtime {
unsigned screen_max_frames_per_second(){
    @autoreleasepool {
        const NSInteger fps=UIScreen.mainScreen.maximumFramesPerSecond;
        return fps>0?unsigned(fps):60u;
    }
}
}
