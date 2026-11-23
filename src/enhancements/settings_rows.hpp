#pragma once
// Port enhancements in Options > Settings (not in the PC game): rows after the
// original four of 4D7440 / 4D86F0 / 4D60C0 for the platform's VideoBackend
// (video.hpp): ASPECT RATIO, RESOLUTION, ANTI-ALIASING and FRAME RATE, each only when the
// platform offers a choice. They use the screen's own list, value font, colour
// and arrows; the labels are list text rows styled like the sprite labels.
// Every value is applied (and saved) as soon as it changes. Without a backend
// the screen is the original one.
#include <cstddef>
#include <functional>
#include "driving/pc_driving.hpp"

namespace outrun::platform { class FrontendTitleWidgets; }
namespace outrun::enhancements {
struct SettingsAccess {
    // End of 4D7440: the added list rows and their value labels.
    static bool init(platform::FrontendTitleWidgets&);
    // End of 4D60C0: the added rows' values and arrows, from display row `row` on.
    static bool display(platform::FrontendTitleWidgets&,int row);
    // Scrolling (more than 8 rows): the first display row shown and how many are.
    static int top(platform::FrontendTitleWidgets&);
    static int shown(platform::FrontendTitleWidgets&);
    static driving::Bytes list_header(platform::FrontendTitleWidgets&);   // the list at owner +34
};
// After an Up / Down of the list (owner +34): the shown rows follow the selection
// at once (48DDA0 alone moves them one row per frame).
void settings_scroll_follow(driving::Bytes list_header);
// 4D86F0 Left / Right on an added row (`row` 0 = the first added row); `sound`
// is 4249F0(1), played when the value moved.
bool settings_step(std::size_t row,bool next,const std::function<bool()>& sound);
}
