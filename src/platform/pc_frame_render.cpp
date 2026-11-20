#include "enhancements/frame_rate.hpp"
#include "platform/pc_frame_render.hpp"
#include "platform/pc_glare.hpp"
#include <algorithm>
#include "driving/pc_x87.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace outrun::platform {
namespace {
// x87 register value: every arithmetic step rounds to the precision control
// (24-bit after the Direct3D device is created without FPU_PRESERVE).
using X=driving::X87;
// MSVC _ftol2 (582194): truncation; out of range and NaN give 0x80000000.
std::uint32_t ftol(X v){return std::uint32_t(driving::x87_ftol32(v));}
constexpr std::int32_t kLayerEnvironment5a27b4[9]{1,1,-1,0,1,1,2,2,2};
constexpr std::uint32_t kOne=0x3f800000u;
}
void frame_clear_40ec70(PcFlushContext& c,const PcFrameState& s,std::uint32_t target){
    c.device.clear(6u+(target!=0u?1u:0u),s.clear_colour_89bd5c,1.f,0);
}
void frame_begin_448db0(PcFlushContext& c,PcFrameState& s,const PcFrameInputs& in){
    c.device.get_viewport(s.viewport_7d25f8.data());
    s.backbuffer_7d25ec=c.device.get_render_target(0);
    s.depth_7d25f4=c.device.get_depth_stencil_surface();
    s.current_colour_7d25e8=s.backbuffer_7d25ec;s.current_depth_7d2610=s.depth_7d25f4;
    // (The 79FCCF scan of the layer targets that follows ends at the clear
    // on every path.)
    // Port (glare restoration, OutRun2006Tweaks' RestoreSkyGlow): in a race the
    // clear's alpha is the stage's exposure 0.25 * [8A8C18]. D3D9 blends alpha
    // with the colour factors (srcA*srcA + dstA*(1-srcA)), and that value is the
    // blend's fixed point; it also covers pixels no sky draws (the sky is drawn
    // with depth off, transparent scenery writes RGB only).
    const auto saved=s.clear_colour_89bd5c;
    const auto mode=in.game_mode_78026c;
    const bool race=mode==0x0du||mode==0x10u||mode==0x12u||mode==0x13u||mode==0x14u||mode==0x1bu||mode==0x22u||mode==0x23u;
    if(race&&pc_glare_enabled()){
        const float exposure=0.25f*c.g.f(0x8a8c18u);
        const std::int32_t a=std::max(0,std::min(255,std::int32_t(exposure*255.0f)));
        s.clear_colour_89bd5c=(saved&0x00ffffffu)|(std::uint32_t(a)<<24);
    }
    frame_clear_40ec70(c,s,1);
    s.clear_colour_89bd5c=saved;
}
void frame_targets_448ed0(PcFlushContext& c,PcFrameState& s,std::uint8_t flags_79fccf,std::uint32_t layer){
    const auto& l=s.layers.at(layer);
    std::uint32_t edi=s.current_colour_7d25e8;bool changed=false;
    const bool same_colour=l.colour==0u||l.colour==edi;
    const bool same_depth=l.depth==0u||l.depth==s.current_depth_7d2610;
    if(!same_colour||!same_depth){
        c.device.set_depth_stencil_surface(l.depth);
        c.device.set_render_target(0,l.colour);
        edi=s.current_colour_7d25e8;changed=true;
    }
    if(l.colour){edi=l.colour;s.current_colour_7d25e8=edi;}
    if(l.depth)s.current_depth_7d2610=l.depth;
    if(!changed)return;
    std::uint32_t flags=s.backbuffer_7d25ec==edi?6u:7u;
    if(s.depth_7d25f4==edi)flags&=~6u;
    for(std::uint32_t j=0;j<layer;++j){
        if(s.layers[j].colour==edi)flags&=~1u;
        if(s.layers[j].depth==edi)flags&=~6u;
    }
    if(layer==3u&&(flags_79fccf&3u)==2u)flags&=~1u;
    if(flags)c.device.clear(flags,0,1.f,0);
}
void frame_end_448e40(PcFlushContext& c,PcFrameState& s){
    if(s.backbuffer_7d25ec!=s.current_colour_7d25e8||s.depth_7d25f4!=s.current_depth_7d2610){
        c.device.set_depth_stencil_surface(s.depth_7d25f4);
        c.device.set_render_target(0,s.backbuffer_7d25ec);
    }
    c.device.release(s.backbuffer_7d25ec);
    c.device.release(s.depth_7d25f4);
    s.backbuffer_7d25ec=0;s.depth_7d25f4=0;
    c.device.set_viewport(s.viewport_7d25f8.data());
}
namespace {
// 40BD80: course passes before the layer's displays.
void course_pre_40bd80(PcFlushContext& c,PcFrameState& s,const PcFrameInputs& in,std::uint8_t course_79fccb,const PcFrameLeaf& leaf,std::uint32_t layer){
    if(!(course_79fccb&3u))return;
    const auto work=in.course_work_79f5ec;const auto f=in.course_flags;
    s.course_73e2a4=0xffffffffu;s.course_73e2a8=0xffffffffu;
    if(layer==2u){if(f&4u)leaf(0x422820,work+0xb0);return;}
    if(layer<=4u||layer>8u)return;
    auto& g=c.g;
    g.w(0x89edd4)=1;g.w(0x89edd8)=0;g.w(0x89eddc)=0;
    if(f&4u)g.w(0x89eddc)=1;
    if(f&8u){g.w(0x89eddc)=2;g.w(0x89edd8)=1;}
    if(f&1u)leaf(0x40bf10,work+0x10);
    if((f&4u)&&!(layer==7u&&in.game_mode_78026c==0x18u))leaf(0x40c1b0,work+0xb0);
    if((f&8u)&&layer!=8u&&layer!=7u)leaf(0x40c550,work+0x130);
}
// 40BE70: course passes after the layer's displays.
void course_post_40be70(PcFlushContext& c,const PcFrameInputs& in,std::uint8_t course_79fccb,const PcFrameLeaf& leaf,std::uint32_t layer){
    if(!(course_79fccb&3u))return;
    const auto work=in.course_work_79f5ec;const auto f=in.course_flags;
    if(layer==2u){if(f&4u)leaf(0x422f20,0);return;}
    if(layer>8u)return;
    if(layer>4u){
        if(f&1u)leaf(0x40c150,0);
        if((f&8u)&&layer!=8u)leaf(0x40c8d0,0);
        if(f&4u)leaf(0x40c4a0,0);
        auto& g=c.g;g.w(0x89edd4)=0;g.w(0x89edd8)=0;g.w(0x89eddc)=0;
    }
    if(layer==8u&&(f&2u))leaf(0x40cbc0,work+0x90);
}
}
void frame_render_449050(PcFlushContext& c,PcFrameState& s,const PcFrameInputs& in,PcFrameServices& sv){
    auto& g=c.g;
    if(!s.pause_7d2614&&enhancements::display_ticks())++g.w(0x89edb8);   // 40ED60 (port: not on display-only frames)
    frame_begin_448db0(c,s,in);
    std::uint32_t saved_viewport[6]{};
    // Mode 24 (the goal ending): the 3D scene in a 320x240 window at the left, the staff roll beside it.
    const bool ending=in.game_mode_78026c==0x18u;
    const std::uint32_t ending_viewport[6]{0,ftol(X(in.screen_740c98)*X(120.0f)),ftol(X(in.screen_740c94)*X(320.0f)),
                                           ftol(X(in.screen_740c98)*X(240.0f)),0,kOne};
    if(ending){
        c.device.get_viewport(saved_viewport);
        c.device.set_viewport(ending_viewport);
        c.device.set_scene_in_ui_rect(true);   // port: the window lines up with the 4:3 staff roll in widescreen
    }
    std::int32_t environment=-1;
    for(std::uint32_t layer=0;layer<9u;++layer){
        s.layer_7d25f0=layer;
        frame_targets_448ed0(c,s,sv.events.slots[391].flags,layer);
        if(in.game_mode_78026c==0x20u&&layer==8u)sv.leaf(0x4c50a0,0);
        g.w(0x89ede0)=0;g.w(0x89ede8)=0;                       // 410670
        render_pass_defaults_404540(g,layer);
        const std::int32_t e=kLayerEnvironment5a27b4[layer];
        if(e!=environment&&e!=-1){
            environment=e;
            render_environment_40d840(c,e,sv.events.slots[386].flags,sv.environment);
            render_environment_matrix_4089a0(c,e,in.matrix_7d2da0);
        }
        course_pre_40bd80(c,s,in,sv.events.slots[387].flags,sv.leaf,layer);
        switch(layer){
        case 0:
            sv.leaf(0x414340,0);
            // Port: the car reflection cube's render-target switches (414340 / 413FC0) reset
            // the viewport to the whole back buffer (the D3D9 SetRenderTarget rule), which
            // filled the screen with the ending's scene; it goes back in its window.
            if(ending)c.device.set_viewport(ending_viewport);
            break;
        case 1:case 4:break;
        case 2:event_shadow_display_4400c0(sv.events,sv.scene,sv.display);break;
        default:event_display_43fb40(sv.events,1u<<layer,0,0x199,sv.sun_light,sv.scene,sv.display);break;
        }
        course_post_40be70(c,in,sv.events.slots[387].flags,sv.leaf,layer);
        if(layer==6u&&in.glare_95af09){sv.leaf(0x414e50,0);sv.leaf(0x414f00,0);}
        if(s.layers[layer].callback)sv.leaf(s.layers[layer].callback,0);
    }
    if(ending){c.device.set_viewport(saved_viewport);c.device.set_scene_in_ui_rect(false);}
    frame_end_448e40(c,s);
    sv.leaf(0x42d710,0x15);
    const auto& player=sv.events.slots[8];
    const std::uint32_t mode_84a318=sv.mode_84a318?sv.mode_84a318():in.mode_84a318;
    if(mode_84a318==2u&&(player.flags&3u)==2u)sv.leaf(0x49f4d0,player.work_token);
}
}
