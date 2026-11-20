// The frame renderer's glare (bloom): 414E50 / 414F00 and their passes
// 4152E0 (bright pass), 415630 (blur) and 415ED0 (composite). See pc_glare.hpp.
#include "platform/pc_glare.hpp"
#include "platform/pc_render_flush.hpp"
#include "platform/pc_d3d9.hpp"
#include "system/exe_image.hpp"
#include "driving/pc_d3dx.hpp"
#include "driving/pc_x87.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <stdexcept>
#include <vector>
namespace outrun::platform {
namespace {
using X=driving::X87;
float st(X v){return driving::x87_float(v);}
PcGlareState state_;
// The render-state cache 893D20 the passes save into and restore from: render
// states (406810 / 406870, +state*4), texture stage states (4068D0 / 406990,
// +348 + (stage*0x21+type)*4) and sampler states (406930 / 406A00, +558 +
// (sampler*0xE+type)*4).
struct StateCache {
    std::array<std::uint32_t,256> rs{};
    std::array<std::array<std::uint32_t,0x21>,8> tss{};
    std::array<std::array<std::uint32_t,0x0e>,8> samp{};
};
StateCache cache_;
// 41B5C0 / 41B7A0: the render states the particle display saves (its bss
// 8A92C4..) and the stage 0..3 textures.
constexpr std::uint32_t SavedStates41b5c0[19]{0x16,0x18,0x1b,0x0f,0xab,0x13,0x14,0x07,0x0e,0x89,0x1c,0xa8,0x1d,0x9c,0x9d,0x9a,0x9e,0x9f,0xa0};
struct Saved41b5c0 {std::array<std::uint32_t,19> rs{};std::array<std::uint32_t,4> textures{};};
struct Pass {
    PcFlushContext& c;PcD3D9Device& d;PcGlareState& s;
    explicit Pass(PcFlushContext& cc):c(cc),d(cc.device),s(state_){}
    float f(std::uint32_t address){return c.g.f(address);}
    std::uint32_t u(std::uint32_t address){return c.g.w(address);}
    // "get; set when different" without the cache (414F00's inline states).
    void rs(std::uint32_t state,std::uint32_t v){if(d.get_render_state(state)!=v)d.set_render_state(state,v);}
    // 406810 (ESI = state, EDI = value) / 406870 (ESI = state).
    void rs_save(std::uint32_t state,std::uint32_t v){const auto old=d.get_render_state(state);if(old!=v)d.set_render_state(state,v);cache_.rs[state&0xff]=old;}
    void rs_restore(std::uint32_t state){const auto v=cache_.rs[state&0xff];if(d.get_render_state(state)!=v)d.set_render_state(state,v);}
    // 4068D0 (EAX = stage, ESI = type, EBX = value) / 406990 (EBX = stage, EDI = type).
    void tss_save(std::uint32_t stage,std::uint32_t type,std::uint32_t v){
        const auto old=d.get_texture_stage_state(stage,type);if(old!=v)d.set_texture_stage_state(stage,type,v);cache_.tss[stage&7][type%0x21]=old;}
    void tss_restore(std::uint32_t stage,std::uint32_t type){
        const auto v=cache_.tss[stage&7][type%0x21];if(d.get_texture_stage_state(stage,type)!=v)d.set_texture_stage_state(stage,type,v);}
    // 406930 (EAX = sampler, ESI = type, EBX = value) / 406A00 (EBX = sampler, EDI = type).
    void samp_save(std::uint32_t sampler,std::uint32_t type,std::uint32_t v){
        const auto old=d.get_sampler_state(sampler,type);if(old!=v)d.set_sampler_state(sampler,type,v);cache_.samp[sampler&7][type%0x0e]=old;}
    void samp_restore(std::uint32_t sampler,std::uint32_t type){
        const auto v=cache_.samp[sampler&7][type%0x0e];if(d.get_sampler_state(sampler,type)!=v)d.set_sampler_state(sampler,type,v);}
    // 406A70(surface, depth): the current target, depth and viewport kept in
    // 8999AC / 8999A8 / 860EBC (once), then the new ones.
    std::array<std::uint32_t,6> viewport_860ebc{};
    void push_target_406a70(std::uint32_t surface,std::uint32_t depth){
        if(u(0x8999acu)||u(0x8999a8u))return;
        c.g.w(0x8999acu)=d.get_render_target(0);c.g.w(0x8999a8u)=d.get_depth_stencil_surface();
        d.get_viewport(viewport_860ebc.data());
        d.set_depth_stencil_surface(depth);d.set_render_target(0,surface);
    }
    // 406AF0: back to them (references released).
    void pop_target_406af0(){
        const auto rt=u(0x8999acu),ds=u(0x8999a8u);
        if(!rt&&!ds)return;
        d.set_depth_stencil_surface(ds);d.set_render_target(0,rt);d.set_viewport(viewport_860ebc.data());
        if(rt)d.release(rt);
        if(ds)d.release(ds);
        c.g.w(0x8999acu)=0;c.g.w(0x8999a8u)=0;
    }
    // DrawPrimitiveUP (strip, 2) of a 4-vertex quad.
    void quad(const std::vector<std::uint8_t>& v,std::uint32_t stride){d.draw_primitive_up(5,2,v.data(),stride);}
};
// 4393F4 D3DXCreateTexture(device, 640, 480, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT).
std::uint32_t target_4393f4(PcD3D9Device& d){return d.create_texture(640,480,1,1,0x15,0);}
// CreatePixelShader (106) over the EXE's tokens (up to 0x0000FFFF).
std::uint32_t shader(PcD3D9Device& d,std::uint32_t va){
    std::vector<std::uint32_t> tokens;
    for(std::uint32_t k=0;k<0x400u;++k){
        const auto* b=exe_image_bytes(va+k*4,4);
        if(!b)throw std::runtime_error("glare: pixel shader outside the EXE image");
        std::uint32_t t;std::memcpy(&t,b,4);tokens.push_back(t);
        if(t==0x0000ffffu)return d.create_pixel_shader(tokens.data());
    }
    throw std::runtime_error("glare: pixel shader without an end token");
}
// A vertex table of the EXE (6236B0, 623720, 744990).
std::vector<std::uint8_t> exe_table(std::uint32_t va,std::uint32_t size){
    const auto* b=exe_image_bytes(va,size);
    if(!b)throw std::runtime_error("glare: vertex table outside the EXE image");
    return std::vector<std::uint8_t>(b,b+size);
}
// 414C00 (the four targets) and 414CE0 (the four pixel shaders), at device
// setup on the PC; here on first use, once per device.
bool ensure_objects(Pass& p){
    auto& s=p.s;
    if(s.device==&p.d)return !s.unavailable;
    const auto runs=s.runs;s=PcGlareState{};s.runs=runs;s.device=&p.d;
    try{
        s.copy_8a8c14=target_4393f4(p.d);s.bright_8a8c10=target_4393f4(p.d);
        s.blur_8a8c68[0]=target_4393f4(p.d);s.blur_8a8c68[1]=target_4393f4(p.d);
        s.ps_8a8c70=shader(p.d,0x622348u);s.ps_8a8c64=shader(p.d,0x622428u);
        s.ps_8a8c0c=shader(p.d,0x6224b4u);s.ps_8a8c60=shader(p.d,0x6224d0u);
    }catch(const std::exception& e){s.error=e.what();}
    if(!s.copy_8a8c14||!s.bright_8a8c10||!s.blur_8a8c68[0]||!s.blur_8a8c68[1]||!s.ps_8a8c70||!s.ps_8a8c64||!s.ps_8a8c0c){
        s.unavailable=true;if(s.error.empty())s.error="glare: the device cannot create its targets or pixel shaders";
        if(std::getenv("OR2_TRACE"))std::fprintf(stderr,"[glare] off: %s\n",s.error.c_str());
        return false;
    }
    return true;
}
// 414E50: the back buffer into 8A8C14 (StretchRect, linear), shown by default.
// Port (stereo): each eye into its own copy (the right eye's in copy_right).
bool copy_eye(Pass& p,std::uint32_t copy,int eye){
    if(eye>=0)p.d.set_stereo_eye(eye);
    const auto dst=p.d.get_surface_level(copy,0);
    const auto src=p.d.get_back_buffer();
    const bool done=dst&&src&&p.d.stretch_rect(src,dst,2);
    if(src)p.d.release(src);
    if(dst)p.d.release(dst);
    if(eye>=0)p.d.set_stereo_eye(-1);
    return done;
}
void capture_414e50(Pass& p){
    auto& s=p.s;
    std::uint32_t viewport[6];p.d.get_viewport(viewport);                     // read and unused, as the PC
    unsigned eyes=p.d.stereo_eyes()>1?2u:1u;
    if(eyes==2&&!s.copy_right){try{s.copy_right=target_4393f4(p.d);}catch(const std::exception&){}if(!s.copy_right)eyes=1;}
    bool done=copy_eye(p,s.copy_8a8c14,eyes==2?0:-1);
    if(done&&eyes==2)done=copy_eye(p,s.copy_right,1);
    if(!done){++s.skipped;s.error="glare: StretchRect from the back buffer unsupported";return;}
    s.current_8a8c08=s.copy_8a8c14;++s.copies;s.captured=true;s.captured_eyes=eyes;
}
// 4152E0: the bright pass, 8A8C14 through 622348 into 8A8C10, then copied to 8A8C68.
void bright_4152e0(Pass& p){
    auto& s=p.s;auto& d=p.d;
    const X k=X(p.f(0x8a8c20u))*X(0.125f);                                    // 6280E0
    // [esp+0C]: the record's colour +0C..+14 normalised (4393E8 in place).
    driving::PcVec3 n{p.f(0x8a8c24u),p.f(0x8a8c28u),p.f(0x8a8c2cu)};
    const float c0[4]{st(X(p.f(0x8a8c24u))*k+X(0.5f)),st(X(p.f(0x8a8c28u))*k+X(0.5f)),st(X(p.f(0x8a8c2cu))*k+X(0.5f)),1.0f};
    n=driving::pc_d3dx_vec3_normalize(n);
    const float c1[4]{st((X(n[0])+X(1.0f))*X(0.5f)),st((X(n[1])+X(1.0f))*X(0.5f)),st((X(n[2])+X(1.0f))*X(0.5f)),1.0f};
    const X level=X(p.f(0x8a8c30u));
    const float c2[4]{st(X(p.f(0x8a8c34u))*level*X(0.25f)),st(X(p.f(0x8a8c38u))*level*X(0.25f)),st(X(p.f(0x8a8c3cu))*level*X(0.25f)),0.0f};
    const float c6[4]{0,0,0,0},c7[4]{1,1,1,0.5f};
    const auto bright=d.get_surface_level(s.bright_8a8c10,0);
    p.push_target_406a70(bright,0);
    p.rs_save(0x1b,0);p.rs_save(0x0f,0);
    d.set_texture(0,s.copy_8a8c14);
    d.set_fvf(0x144);
    d.set_pixel_shader(s.ps_8a8c70);
    d.set_pixel_shader_constant_f(0,c0,1);d.set_pixel_shader_constant_f(1,c1,1);d.set_pixel_shader_constant_f(2,c2,1);
    d.set_pixel_shader_constant_f(6,c6,1);d.set_pixel_shader_constant_f(7,c7,1);
    p.quad(exe_table(0x6236b0u,4*0x1c),0x1c);
    d.set_pixel_shader(0);d.set_texture(0,0);
    p.rs_restore(0x0f);p.rs_restore(0x1b);
    p.pop_target_406af0();
    const auto blur=d.get_surface_level(s.blur_8a8c68[0],0);
    if(!(bright&&blur&&d.stretch_rect(bright,blur,0))){++s.skipped;s.error="glare: StretchRect between targets unsupported";}
    if(blur)d.release(blur);
    if(bright)d.release(bright);
    s.current_8a8c08=s.bright_8a8c10;
}
// 415630: [8A8C40]-1 blur passes between 8A8C68 and 8A8C6C, four taps each.
void blur_415630(Pass& p){
    auto& s=p.s;auto& d=p.d;
    const auto saved_rt=d.get_render_target(0),saved_ds=d.get_depth_stencil_surface();
    std::uint32_t viewport[6];d.get_viewport(viewport);
    const std::uint32_t surfaces[2]{d.get_surface_level(s.blur_8a8c68[0],0),d.get_surface_level(s.blur_8a8c68[1],0)};
    auto vertices=exe_table(0x623720u,0xd0);                                   // 4 x 0x34 (XYZRHW, diffuse, 4 coordinate sets)
    p.rs_save(0x1b,1);p.rs_save(0xab,1);p.rs_save(0x13,2);p.rs_save(0x14,0xe);p.rs_save(0xc1,0);
    for(std::uint32_t k=0;k<4;++k){
        if(d.get_sampler_state(k,1)!=3)d.set_sampler_state(k,1,3);
        if(d.get_sampler_state(k,2)!=3)d.set_sampler_state(k,2,3);
        if(d.get_texture_stage_state(k,0xb)!=k)d.set_texture_stage_state(k,0xb,k);
        if(d.get_texture_stage_state(k,0x18)!=0)d.set_texture_stage_state(k,0x18,0);
    }
    d.set_fvf(0x444);
    d.set_pixel_shader(s.ps_8a8c64);
    unsigned last=0;
    const std::int32_t passes=std::int32_t(p.u(0x8a8c40u));
    {static unsigned n=0;if(std::getenv("OR2_TRACE")&&n++<3)std::fprintf(stderr,"[glare] passes %d spread %.4f grow %.4f keep %.3f blend %u/%u\n",passes,p.f(0x8a8c44u),p.f(0x8a8c48u),p.f(0x8a8c4cu),p.u(0x8a8c54u),p.u(0x8a8c58u));}
    if(passes>1){
        X step=X(p.f(0x8a8c44u))/X(640.0f);                                   // 740C8C
        // The previous contents kept at the blend factor ftol([8A8C4C] * 255) (grey).
        const std::uint32_t g=std::uint32_t(driving::x87_ftol32(X(p.f(0x8a8c4cu))*X(255.0f)))&0xffu;
        const std::uint32_t factor=(((g<<8)|g)<<8)|g;
        auto add=[&](unsigned vertex,unsigned word,X delta){
            const std::size_t at=std::size_t(vertex)*0x34+0x14+word*4;
            float v;std::memcpy(&v,vertices.data()+at,4);v=st(X(v)+delta);std::memcpy(vertices.data()+at,&v,4);};
        unsigned source=0;
        for(std::int32_t pass=1;pass<passes;++pass){
            for(std::uint32_t k=0;k<4;++k)d.set_texture(k,s.blur_8a8c68[source]);
            last=source^1u;
            d.set_render_target(0,surfaces[last]);
            d.set_depth_stencil_surface(0);
            const float c7[4]{0.25f,0.25f,0.25f,0.25f};
            d.set_pixel_shader_constant_f(7,c7,1);
            const float sv=st(step);
            // Taps: (-d,-d) (+d,-d) (+d,+d) (-d,+d), added to every vertex's sets.
            for(unsigned v=0;v<4;++v){
                add(v,0,-X(sv));add(v,1,-X(sv));add(v,2,X(sv));add(v,3,-X(sv));
                add(v,4,X(sv));add(v,5,X(sv));add(v,6,-X(sv));add(v,7,X(sv));
            }
            step=X(st(X(p.f(0x8a8c48u))/X(640.0f)+X(sv)));
            p.quad(vertices,0x34);
            if(d.get_render_state(0xc1)!=factor)d.set_render_state(0xc1,factor);
            source=last;
        }
    }
    d.set_pixel_shader(0);
    for(std::uint32_t k=0;k<4;++k)d.set_texture(k,0);
    p.rs_restore(0x1b);p.rs_restore(0xab);p.rs_restore(0x13);p.rs_restore(0x14);p.rs_restore(0xc1);
    d.set_render_target(0,saved_rt);d.set_depth_stencil_surface(saved_ds);d.set_viewport(viewport);
    if(saved_rt)d.release(saved_rt);
    if(saved_ds)d.release(saved_ds);
    for(auto h:surfaces)if(h)d.release(h);
    s.current_8a8c08=s.blur_8a8c68[last];
}
// 415ED0: the composite, [8A8C08] through 6224B4 over the screen with the
// stage's blend (6237F0 maps 0..10 to D3DBLEND 1..11).
void composite_415ed0(Pass& p){
    auto& s=p.s;auto& d=p.d;
    auto blend=[&](std::uint32_t index){
        const auto* b=exe_image_bytes(0x6237f0u+(index&0xfu)*4,4);
        if(!b)throw std::runtime_error("glare: blend table outside the EXE image");
        std::uint32_t v;std::memcpy(&v,b,4);return v;};
    p.rs_save(0xa8,7);p.rs_save(0x1b,1);p.rs_save(0xab,1);
    p.rs_save(0x13,blend(p.u(0x8a8c54u)));p.rs_save(0x14,blend(p.u(0x8a8c58u)));
    d.set_texture(0,s.current_8a8c08);
    d.set_fvf(0x444);
    d.set_pixel_shader(s.ps_8a8c0c);
    // 744990: 0..640 x 0..480 (414CE0 scales it by 740C94 / 740C98, 1.0).
    // Port: the quad covers the 3D viewport, the whole widescreen canvas
    // (set_rhw_full_canvas), where the PC's 4:3 frame is 640x480.
    auto quad=exe_table(0x744990u,0xd0);
    std::uint32_t viewport[6];d.get_viewport(viewport);                       // D3DVIEWPORT9: X, Y, Width, Height are DWORDs
    const float w=float(viewport[2]),h=float(viewport[3]),x0=float(viewport[0]),y0=float(viewport[1]);
    for(unsigned v=0;v<4;++v){
        float x,y;std::memcpy(&x,quad.data()+v*0x34,4);std::memcpy(&y,quad.data()+v*0x34+4,4);
        x=x0+x/640.0f*w;y=y0+y/480.0f*h;
        std::memcpy(quad.data()+v*0x34,&x,4);std::memcpy(quad.data()+v*0x34+4,&y,4);
    }
    d.set_rhw_full_canvas(true);
    p.quad(quad,0x34);
    d.set_rhw_full_canvas(false);
    d.set_pixel_shader(0);d.set_texture(0,0);
    p.rs_restore(0xa8);p.rs_restore(0x1b);p.rs_restore(0xab);p.rs_restore(0x13);p.rs_restore(0x14);
}
// 414F00: the glare, around 41B5C0 / 41B7A0 and its stage 0 states.
void glare_414f00(Pass& p,std::uint32_t layer){
    auto& d=p.d;
    Saved41b5c0 saved;
    for(unsigned k=0;k<19;++k)saved.rs[k]=d.get_render_state(SavedStates41b5c0[k]);
    for(std::uint32_t k=0;k<4;++k)saved.textures[k]=d.get_texture(k);          // 41B5C0
    p.samp_save(0,5,2);p.samp_save(0,6,2);p.samp_save(0,7,0);p.samp_save(0,1,3);p.samp_save(0,2,3);
    p.tss_save(0,0xb,0);p.tss_save(0,0x18,0);p.tss_save(0,1,4);p.tss_save(0,2,0);p.tss_save(0,3,2);p.tss_save(0,4,1);
    p.rs(0x1b,1);p.rs(0x0f,0);p.rs(0xab,1);p.rs(0x13,2);p.rs(0x14,2);p.rs(0x07,0);p.rs(0x0e,0);
    p.rs(0x16,1);p.rs(0xa8,0xf);p.rs(0x89,0);p.rs(0x1c,0);
    // Port (stereo): the bright pass, blur and composite once per eye, each from
    // that eye's copy into that eye only; one composite over both eyes put the
    // left eye's glow at the wrong place in the right eye.
    const unsigned eyes=p.s.captured_eyes==2u&&d.stereo_eyes()>1?2u:1u;
    for(unsigned eye=0;eye<eyes;++eye){
        struct Eye{Pass& p;unsigned eye,eyes;
            Eye(Pass& o,unsigned e,unsigned n):p(o),eye(e),eyes(n){if(eye==1)std::swap(p.s.copy_8a8c14,p.s.copy_right);if(eyes==2)p.d.set_stereo_eye(int(eye));}
            ~Eye(){if(eyes==2)p.d.set_stereo_eye(-1);if(eye==1)std::swap(p.s.copy_8a8c14,p.s.copy_right);}} at{p,eye,eyes};
        bright_4152e0(p);
        blur_415630(p);
        composite_415ed0(p);
    }
    for(std::uint32_t k=0;k<4;++k)d.set_texture(k,0);
    d.set_pixel_shader(0);
    p.rs(0x1b,1);p.rs(0x89,1);
    for(std::uint32_t t:{0xbu,0x18u,1u,2u,3u,4u})p.tss_restore(0,t);
    for(std::uint32_t t:{5u,6u,7u,1u,2u})p.samp_restore(0,t);
    for(unsigned k=0;k<19;++k)p.rs(SavedStates41b5c0[k],saved.rs[k]);         // 41B7A0
    for(std::uint32_t k=0;k<4;++k){d.set_texture(k,saved.textures[k]);if(saved.textures[k])d.release(saved.textures[k]);}
    p.c.g.w(0x89ede0u)=0;p.c.g.w(0x89ede8u)=0;
    render_reset_states_408880(p.c,layer);
    ++p.s.runs;
}
}
bool pc_glare_enabled(){
    static const char* env=std::getenv("OR2_GLARE");
    if(env&&*env)return std::atoi(env)!=0;
    return g_pc_glare;
}
PcGlareState& pc_glare_state(){return state_;}
bool pc_glare_leaf(PcFlushContext& c,std::uint32_t pc,std::uint8_t flags_79fcd5,std::uint32_t layer_7d25f0){
    if(pc!=0x414e50u&&pc!=0x414f00u)return false;
    // Both: [79FCD5] & 3 == 2 (event 397 live) and [8A8C40] != 0 (the stage has a glare).
    if((flags_79fcd5&3u)!=2u||c.g.w(0x8a8c40u)==0u)return true;
    Pass p(c);
    if(!ensure_objects(p))return true;
    if(pc==0x414e50u){p.s.captured=false;capture_414e50(p);}
    else if(p.s.captured){p.s.captured=false;glare_414f00(p,layer_7d25f0);}   // port: not over a stale copy
    return true;
}
}
