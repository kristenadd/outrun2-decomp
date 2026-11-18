#pragma once
// Port enhancements (not in the PC game): display options a platform can offer
// beyond the original 640x480 4:3 screen. The game code never reads these; the
// platform layer applies them and extra rows of Options > Settings
// (settings_rows.hpp) edit them. Each platform registers one VideoBackend
// listing only what its renderer really supports.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace outrun::enhancements {
enum class Antialiasing : std::uint8_t { Off, Msaa2x, Msaa4x, Fxaa };

// A 3D render resolution of the scene, in pixels (the 2D layer stays at the
// output resolution on every platform).
struct Resolution {
    std::uint32_t width{},height{};
    bool operator==(const Resolution& o)const{return width==o.width&&height==o.height;}
    bool operator!=(const Resolution& o)const{return !(*this==o);}
};

// The resolution of a height in an aspect: 4:3 or 16:9 width (even).
Resolution resolution_at(std::uint32_t height,bool widescreen);
// The resolution of a height at any aspect (width / height): even width.
Resolution resolution_at(std::uint32_t height,float aspect);

struct VideoSettings {
    bool widescreen{};          // 16:9 (Hor+) instead of the PC's 4:3
    Resolution resolution{};    // resolution_at(one of VideoBackend::heights(), widescreen)
    Antialiasing antialiasing{Antialiasing::Off};
    std::uint32_t frame_rate{60};   // frames shown per second (frame_rate.hpp); the game ticks at 60 Hz
    // The screen's own aspect (Hor+, widescreen set too) where the platform
    // knows it (VideoBackend::screen_aspect), e.g. an iPad's 3:2-ish screen.
    bool fit_screen{};
    // Stereoscopic 3D depth (visionOS): 0 = off (mono), 1 .. VideoBackend::stereo_depths().
    std::uint32_t stereo_depth{};
};

class VideoBackend {
public:
    virtual ~VideoBackend()=default;
    // false: the platform shows the PC's 4:3 screen only (no aspect row).
    virtual bool widescreen_available()const=0;
    // 3D resolution heights, smallest first (the width follows the aspect); fewer than two: no row.
    virtual std::vector<std::uint32_t> heights()const=0;
    // Supported modes in menu order (Off, FXAA, MSAA 2x, MSAA 4x); fewer than two: no row.
    virtual std::vector<Antialiasing> antialiasing_modes()const=0;
    // The screen's width / height when the platform fills a fixed screen (iOS):
    // the aspect row then offers FULL SCREEN. 0: no such choice.
    virtual float screen_aspect()const{return 0;}
    // Frames per second offered, smallest first (60, 120); fewer than two: no row.
    virtual std::vector<std::uint32_t> frame_rates()const{return {};}
    // The highest 3D DEPTH level where the platform shows stereoscopic 3D
    // (visionOS), the row offering OFF, 1 .. this; 0: no such row.
    virtual std::uint32_t stereo_depths()const{return 0;}
    virtual VideoSettings current()const=0;
    // Applies (live when the platform can, else at the next launch) and saves.
    virtual void apply(const VideoSettings&)=0;
    // Shown under the options when a change only takes effect after a restart.
    virtual bool needs_restart(const VideoSettings&)const{return false;}
};

// The aspect (width / height) of the PC screen these settings show: 4:3, 16:9
// or, with fit_screen, `screen_aspect` (when it is known, > 0).
float video_aspect(const VideoSettings&,float screen_aspect);

void set_video_backend(VideoBackend*);
VideoBackend* video_backend();

std::string resolution_label(const Resolution&);   // "1920x1080"
const char* antialiasing_label(Antialiasing);
// options.ini values: off / msaa2 / msaa4 / fxaa (the older 0 / 2 / 4 too).
const char* antialiasing_key(Antialiasing);
bool parse_antialiasing(const std::string&,Antialiasing&);
bool parse_resolution(const std::string&,Resolution&);   // "1920x1080"
std::string resolution_key(const Resolution&);
// The height of `list` nearest to `want` (`want` if the list is empty).
std::uint32_t nearest_height(const std::vector<std::uint32_t>& list,std::uint32_t want);

// Rewrites `key=value` lines of an options.ini in place (comments and other
// keys kept, missing keys appended). False when the file cannot be written.
bool write_options(const std::string& path,const std::vector<std::pair<std::string,std::string>>& values);
}
