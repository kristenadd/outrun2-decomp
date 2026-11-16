// Options > Settings enhancement rows (port addition): the shared video
// settings helpers and the row steps, with and without a platform backend.
#include "enhancements/settings_rows.hpp"
#include "enhancements/video.hpp"
#include "enhancements/gpu_backend.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace en=outrun::enhancements;
namespace {
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}

struct FakeBackend final : en::VideoBackend {
    en::VideoSettings s{false,{640,480},en::Antialiasing::Off};
    unsigned applied{};
    bool widescreen_available()const override{return true;}
    std::vector<std::uint32_t> heights()const override{return {480,720};}
    std::vector<en::Antialiasing> antialiasing_modes()const override{return {en::Antialiasing::Off,en::Antialiasing::Fxaa};}
    en::VideoSettings current()const override{return s;}
    void apply(const en::VideoSettings& v)override{s=v;++applied;}
};
}

int main(){
    check(en::resolution_label({1920,1080})=="1920x1080","label is the size");
    check(en::resolution_at(1080,true)==en::Resolution{1920,1080}&&en::resolution_at(1080,false)==en::Resolution{1440,1080},"width follows the aspect");
    check(en::resolution_at(480,true)==en::Resolution{854,480}&&en::resolution_at(504,true)==en::Resolution{896,504},"16:9 widths rounded to even");
    check(en::resolution_at(1080,1.5228f)==en::Resolution{1646,1080},"any aspect, even width");
    {en::VideoSettings v{};check(en::video_aspect(v,1.5f)==4.f/3.f,"4:3");v.widescreen=true;check(en::video_aspect(v,1.5f)==16.f/9.f,"16:9");
     v.fit_screen=true;check(en::video_aspect(v,1.5f)==1.5f&&en::video_aspect(v,0)==16.f/9.f,"fit: the screen's aspect when known");}
    {en::VideoSettings v{};check(en::GpuVideoBackend::parse_option("fit_screen","1",v)&&v.fit_screen,"fit_screen key");
     v.resolution={1440,1080};en::GpuVideoBackend mac("",v);
     check(!mac.current().fit_screen&&mac.screen_aspect()==0,"no screen aspect: no fit");
     mac.set_screen_aspect(1.5f);
     check(mac.current().fit_screen&&mac.current().widescreen&&mac.current().resolution==en::Resolution{1620,1080},"screen known: fit restored at its aspect");}
    en::Antialiasing a{};
    check(en::parse_antialiasing("4",a)&&a==en::Antialiasing::Msaa4x,"legacy MSAA value");
    check(en::parse_antialiasing("fxaa",a)&&a==en::Antialiasing::Fxaa,"FXAA value");
    check(!en::parse_antialiasing("bogus",a),"unknown AA value refused");
    en::Resolution r{};
    check(en::parse_resolution("1920x1080",r)&&r==en::Resolution{1920,1080},"resolution value");
    check(!en::parse_resolution("1920",r)&&!en::parse_resolution("10x10",r),"bad resolution refused");
    check(en::nearest_height({480,720,1080},1000)==1080&&en::nearest_height({},700)==700,"nearest height");

    const std::string path="test_enhancements_options.ini";
    {std::ofstream f(path,std::ios::binary);f<<"# comment\nreflections=1\nwidescreen=0\n";}
    check(en::write_options(path,{{"widescreen","1"},{"antialiasing","fxaa"}}),"options written");
    {std::ifstream f(path,std::ios::binary);std::stringstream t;t<<f.rdbuf();
     check(t.str()=="# comment\nreflections=1\nwidescreen=1\nantialiasing=fxaa\n","options rewritten in place, missing key appended");}
    std::remove(path.c_str());

    // Without a backend (or before the Settings screen listed its rows) a step does nothing.
    unsigned sounds=0;const auto sound=[&]{++sounds;return true;};
    check(en::settings_step(0,true,sound)&&sounds==0,"no backend: no added row");
    FakeBackend backend;en::set_video_backend(&backend);
    check(en::settings_step(0,true,sound)&&sounds==0&&backend.applied==0,"rows exist only once the Settings screen listed them");
    en::set_video_backend(nullptr);
    std::printf("enhancements: %u checks passed\n",checks);
    return 0;
}
