#include "enhancements/gpu_backend.hpp"
#include "enhancements/frame_rate.hpp"
#include <algorithm>
#include <cstdlib>

namespace outrun::enhancements {
GpuVideoBackend::GpuVideoBackend(std::string path,const VideoSettings& start):path_(std::move(path)),saved_fit_(start.fit_screen),saved_rate_(start.frame_rate),saved_stereo_(start.stereo_depth),start_hd_(start.hd_textures){settings_=supported(start);}
void GpuVideoBackend::set_effects(EffectsPush push,std::function<bool()> hd_present){
    effects_push_=std::move(push);hd_present_=std::move(hd_present);hd_known_=-1;
    if(effects_push_)effects_push_(settings_);}
bool GpuVideoBackend::hd_textures_available()const{
    if(!effects_push_||!hd_present_)return false;
    if(hd_known_<0)hd_known_=hd_present_()?1:0;
    return hd_known_==1;}
void GpuVideoBackend::set_screen_aspect(float aspect){
    screen_aspect_=aspect>0?aspect:0;settings_.fit_screen=saved_fit_;settings_=supported(settings_);}
void GpuVideoBackend::attach(Push push){push_=std::move(push);this->push();}
void GpuVideoBackend::set_stereo_depths(std::uint32_t levels,StereoPush push){
    stereo_levels_=levels;stereo_push_=std::move(push);settings_.stereo_depth=saved_stereo_;settings_=supported(settings_);
    if(stereo_push_)stereo_push_(settings_.stereo_depth);}
void GpuVideoBackend::set_frame_rates(std::vector<std::uint32_t> rates,RatePush pace){
    rates_=rates.empty()?std::vector<std::uint32_t>{60}:std::move(rates);settings_.frame_rate=saved_rate_;settings_=supported(settings_);
    pace_=std::move(pace);
    // Without a pacing call the display mode is chosen at launch: a new rate
    // applies at the next one.
    start_rate_=settings_.frame_rate;set_display_rate(start_rate_);
    if(pace_)pace_(start_rate_);}
VideoSettings GpuVideoBackend::supported(VideoSettings s)const{
    if(screen_aspect_<=0)s.fit_screen=false;
    if(s.fit_screen)s.widescreen=true;
    s.resolution=resolution_at(nearest_height(heights(),s.resolution.height),video_aspect(s,screen_aspect_));
    const auto modes=antialiasing_modes();
    if(std::find(modes.begin(),modes.end(),s.antialiasing)==modes.end())s.antialiasing=Antialiasing::Off;
    if(std::find(rates_.begin(),rates_.end(),s.frame_rate)==rates_.end())s.frame_rate=rates_.front();
    s.stereo_depth=std::min(s.stereo_depth,stereo_levels_);
    return s;
}
void GpuVideoBackend::push()const{
    if(push_)push_(settings_.resolution.width,settings_.resolution.height,settings_.widescreen,
        settings_.antialiasing==Antialiasing::Msaa4x?4u:settings_.antialiasing==Antialiasing::Msaa2x?2u:1u,
        settings_.antialiasing==Antialiasing::Fxaa);
}
void GpuVideoBackend::apply(const VideoSettings& s){
    settings_=supported(s);push();
    if(stereo_push_)stereo_push_(settings_.stereo_depth);
    if(pace_){set_display_rate(settings_.frame_rate);pace_(settings_.frame_rate);}
    saved_fit_=settings_.fit_screen;saved_stereo_=settings_.stereo_depth;
    std::vector<std::pair<std::string,std::string>> values{{"widescreen",settings_.widescreen?"1":"0"},
        {"resolution",resolution_key(settings_.resolution)},{"antialiasing",antialiasing_key(settings_.antialiasing)},
        {"framerate",std::to_string(settings_.frame_rate)},{"timer",settings_.race_timer?"1":"0"}};
    if(screen_aspect_>0)values.push_back({"fit_screen",settings_.fit_screen?"1":"0"});
    if(stereo_levels_>0)values.push_back({"stereo_depth",std::to_string(settings_.stereo_depth)});
    if(effects_push_){
        effects_push_(settings_);
        for(const auto& [k,v]:{std::pair<const char*,bool>{"glare",settings_.glare},{"lens_flare",settings_.lens_flare},
                               {"reflections",settings_.reflections},{"pixel_fog",settings_.pixel_fog}})
            values.push_back({k,v?"1":"0"});
        values.push_back({"hd_textures",std::to_string(settings_.hd_textures)});
    }
    if(!path_.empty())write_options(path_,values);
}
bool GpuVideoBackend::parse_option(const std::string& key,const std::string& value,VideoSettings& s){
    if(key=="widescreen"){s.widescreen=std::strtoul(value.c_str(),nullptr,10)!=0;return true;}
    if(key=="glare"||key=="lens_flare"||key=="reflections"||key=="pixel_fog"){
        const bool on=std::strtoul(value.c_str(),nullptr,10)!=0;
        (key=="glare"?s.glare:key=="lens_flare"?s.lens_flare:key=="reflections"?s.reflections:s.pixel_fog)=on;return true;}
    if(key=="hd_textures"){const auto v=std::strtoul(value.c_str(),nullptr,10);if(v>16)return false;s.hd_textures=std::uint32_t(v);return true;}
    if(key=="timer"){s.race_timer=std::strtoul(value.c_str(),nullptr,10)!=0;return true;}
    if(key=="fit_screen"){s.fit_screen=std::strtoul(value.c_str(),nullptr,10)!=0;return true;}
    if(key=="resolution")return parse_resolution(value,s.resolution);
    if(key=="antialiasing")return parse_antialiasing(value,s.antialiasing);
    if(key=="stereo_depth"){const auto d=std::strtoul(value.c_str(),nullptr,10);if(d>9)return false;s.stereo_depth=std::uint32_t(d);return true;}
    if(key=="framerate"){const auto r=std::strtoul(value.c_str(),nullptr,10);if(r!=60&&r!=90&&r!=120)return false;s.frame_rate=std::uint32_t(r);return true;}
    return false;
}
}
