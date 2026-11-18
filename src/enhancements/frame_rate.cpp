#include "enhancements/frame_rate.hpp"
#include "driving/pc_matrix_stack.hpp"
#include "driving/pc_wrecker.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

namespace outrun::enhancements {
namespace {
struct Replay {const void* key;std::function<void()> replay,restore;bool owed{},car{};};
struct Preserved {const void* key;void* bytes;std::size_t size;const char* name;std::vector<std::uint8_t> saved;bool held{};std::uint64_t changes{};};
struct State {
    std::uint32_t rate{60};
    float blend{1.f};
    std::uint32_t ticks{1};
    std::vector<Replay> replays;          // builders of the last tick, in order
    std::vector<std::function<void()>> pending;   // restores owed before the next tick
    std::vector<Preserved> preserved;     // display_preserve regions
    std::uint64_t stale_previous{};       // car replays skipped (display_stale_previous)
    std::array<std::uint8_t,64u*64u> arena{};
    driving::PcMatrixStack scratch{driving::Bytes(arena.data(),arena.size()),0,0,64};
};
State& state(){static State s;return s;}
// A display-only frame is over: its displays' changes to the preserved regions are undone.
void restore_preserved(){
    for(auto& p:state().preserved)if(p.held){
        if(std::memcmp(p.bytes,p.saved.data(),p.size)!=0){++p.changes;std::memcpy(p.bytes,p.saved.data(),p.size);}
        p.held=false;
    }
}
}
std::uint32_t display_rate(){return state().rate;}
void set_display_rate(std::uint32_t rate){
    auto& s=state();
    if(rate<=60u){display_before_tick();s.replays.clear();rate=60u;}
    restore_preserved();
    s.rate=rate;
}
bool display_frames(){return state().rate>60u;}
float display_blend(){return state().blend;}

void display_before_tick(){
    restore_preserved();
    auto& s=state();
    s.blend=1.f;
    // Newest first: a value changed by two replays gets its tick value back last.
    for(auto it=s.pending.rbegin();it!=s.pending.rend();++it)(*it)();
    s.pending.clear();
    s.replays.clear();
}
std::uint32_t display_ticks(){return display_frames()?state().ticks:1u;}
void display_after_ticks(float blend,std::uint32_t ticks){
    auto& s=state();
    if(!display_frames())return;
    restore_preserved();   // two display-only frames in a row: the first one's changes go first
    if(ticks==0u)for(auto& p:s.preserved){p.saved.assign(static_cast<std::uint8_t*>(p.bytes),static_cast<std::uint8_t*>(p.bytes)+p.size);p.held=true;}
    s.ticks=ticks;
    s.blend=std::clamp(blend,0.f,1.f);
    // The values stay interpolated through the draw; display_before_tick
    // puts them back. A builder that fails keeps its tick result on screen.
    // Cars first: the characters and the camera follow their replayed +B0.
    for(const bool cars:{true,false})for(auto& r:s.replays){
        if(r.car!=cars)continue;
        try{r.replay();}catch(const std::exception&){}
        if(r.restore&&!r.owed){s.pending.push_back(r.restore);r.owed=true;}
    }
}
void display_note(const void* key,std::function<void()> replay,std::function<void()> restore){
    auto& s=state();
    if(!display_frames())return;
    // Testing (isolating 90 fps issues): OR2_FPS_NO_REPLAYS=1 replays nothing between ticks.
    static const bool none=std::getenv("OR2_FPS_NO_REPLAYS")!=nullptr;if(none)return;
    for(auto& r:s.replays)if(r.key==key){r.replay=std::move(replay);r.restore=std::move(restore);return;}
    s.replays.push_back({key,std::move(replay),std::move(restore)});
}
driving::PcMatrixStack& display_matrices(){return state().scratch;}
bool display_stale_previous(driving::Bytes car){
    float now[3],before[3];std::memcpy(now,car.data()+0x14,12);std::memcpy(before,car.data()+0x16c,12);
    const double dx=double(now[0])-before[0],dy=double(now[1])-before[1],dz=double(now[2])-before[2];
    const double d=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(std::isfinite(d)&&d<=DisplayMaxTickMotion)return false;
    auto& s=state();++s.stale_previous;
    static const bool trace=std::getenv("OR2_TRACE")!=nullptr;
    if(trace&&(s.stale_previous<=20u||s.stale_previous%600u==0u))
        std::fprintf(stderr,"[fps] car %p drawn unblended (%llu): previous point %.1f away, (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f)\n",
            static_cast<const void*>(car.data()),(unsigned long long)s.stale_previous,d,before[0],before[1],before[2],now[0],now[1],now[2]);
    return true;
}
std::uint64_t display_stale_previous_count(){return state().stale_previous;}
void display_preserve(const void* key,void* bytes,std::size_t size,const char* name){
    auto& s=state();
    for(auto& p:s.preserved)if(p.key==key){
        if(p.bytes!=bytes||p.size!=size){p.held=false;p.bytes=bytes;p.size=size;}
        p.name=name;return;
    }
    s.preserved.push_back({key,bytes,size,name,{}});
}
std::vector<DisplayPreserveStats> display_preserve_stats(){
    std::vector<DisplayPreserveStats> out;
    for(const auto& p:state().preserved)out.push_back({p.name,p.changes});
    return out;
}
void display_note_car(driving::Bytes car,std::function<float()> blend,std::uint8_t scene_code){
    if(!display_frames())return;
    // Testing: OR2_FPS_NO_CAR_REPLAYS=1 draws cars at their tick matrices (no blend).
    static const bool none=std::getenv("OR2_FPS_NO_CAR_REPLAYS")!=nullptr;if(none)return;
    // +B0 (the display matrix) gets the tick's value back before the next
    // tick: tick code run before this car's control reads it (the camera).
    auto tick_b0=std::make_shared<std::array<std::uint8_t,64>>();std::memcpy(tick_b0->data(),car.data()+0xb0,64);
    display_note(car.data(),[car,blend=std::move(blend),scene_code]{
        auto& s=state();
        // 4A2650 blends the tick position (+14) with the previous one (+16C), which
        // the driving controls refresh each tick. Code that moves cars otherwise
        // (the stage-change zone, the goal) leaves +16C behind: the car would be
        // drawn between here and a stale point, off the screen. A previous point
        // farther than any tick's motion keeps the tick's matrix (no blend).
        if(display_stale_previous(car))return;
        std::array<std::uint8_t,12> d28{};std::memcpy(d28.data(),car.data()+0xd28,12);
        const float t=blend();const auto depth=s.scratch.depth;
        float before[16];std::memcpy(before,car.data()+0xb0,64);
        driving::PcDispMatrixContext display{s.scratch,t,scene_code};
        driving::pc_calc_disp_matrix(car,display);
        std::memcpy(car.data()+0xd28,d28.data(),12);
        // Diagnostics (OR2_TRACE): a replayed matrix far from the tick's, or not a unit-scale rotation.
        static const bool trace=std::getenv("OR2_TRACE")!=nullptr;
        if(trace){
            float m[16];std::memcpy(m,car.data()+0xb0,64);
            const double move=std::sqrt(double(m[12]-before[12])*(m[12]-before[12])+double(m[13]-before[13])*(m[13]-before[13])+double(m[14]-before[14])*(m[14]-before[14]));
            double scale_min=1e9,scale_max=0;for(unsigned r=0;r<3;++r){const double l=std::sqrt(double(m[r*4])*m[r*4]+double(m[r*4+1])*m[r*4+1]+double(m[r*4+2])*m[r*4+2]);scale_min=std::min(scale_min,l);scale_max=std::max(scale_max,l);}
            static std::uint64_t odd=0;
            if((!(move<=8.0)||!(scale_min>0.9&&scale_max<1.1))&&(++odd<=40||odd%500==0))
                std::fprintf(stderr,"[fps] car replay odd (%llu): car %p blend %.3f move %.1f scale %.3f..%.3f flags %08x scene %u depth %u->%u tick (%.1f,%.1f,%.1f) replay (%.1f,%.1f,%.1f)\n",
                    (unsigned long long)odd,static_cast<const void*>(car.data()),t,move,scale_min,scale_max,car.u32(4),unsigned(scene_code),unsigned(depth),unsigned(s.scratch.depth),
                    before[12],before[13],before[14],m[12],m[13],m[14]);
        }
    },[car,tick_b0]{std::memcpy(car.data()+0xb0,tick_b0->data(),64);});
    for(auto& r:state().replays)if(r.key==car.data())r.car=true;
}
}
