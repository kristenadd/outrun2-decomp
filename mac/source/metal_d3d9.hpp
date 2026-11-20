#pragma once
#include "platform/pc_soft_d3d9.hpp"
#include <memory>
namespace outrun::mac {
// The Metal queue (an id<MTLCommandQueue>) every MetalD3D9Device submits on,
// shared with the visionOS compositor so that its reads of gpu_frame() are
// ordered with the device's later writes (one queue: Metal's hazard tracking).
void* metal_shared_queue();
// Translates the recovered vertex/pixel programs to Metal, retaining the
// CPU resource ownership and an explicit reference vertex path for audits.
class MetalD3D9Device final : public platform::PcSoftD3D9Device {
public:
    MetalD3D9Device(unsigned width,unsigned height,unsigned render_scale=1);
    ~MetalD3D9Device() override;
    std::uint32_t draw_indexed_primitive(std::uint32_t,std::int32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t) override;
    void draw_primitive_up(std::uint32_t,std::uint32_t,const void*,std::uint32_t) override;
    void clear(std::uint32_t,std::uint32_t,float,std::uint32_t) override;
    void set_render_target(std::uint32_t,std::uint32_t) override;
    void set_depth_stencil_surface(std::uint32_t) override;
    void unlock_rect(std::uint32_t) override;
    void release(std::uint32_t) override;
    void query_issue(std::uint32_t,std::uint32_t) override;
    bool stretch_rect(std::uint32_t,std::uint32_t,std::uint32_t) override;
    void set_rhw_full_canvas(bool on) override{rhw_full_canvas_=on;}
    std::uint32_t query_get_data(std::uint32_t,std::uint32_t&) override;
    std::uint8_t* lock(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t) override;
    void begin_frame();
    void end_frame();
    void readback();
    // One frame of latency: end_frame shows the previous frame while the GPU
    // renders this one (the game); off, every frame is waited for (tests).
    void set_pipelined(bool on);
    std::uint64_t gpu_nanoseconds() const;
    std::string device_name() const;
    unsigned presentation_width() const;
    unsigned presentation_height() const;
    // Port enhancements (Options > Settings): the back buffer at any pixel
    // size (the logical PC screen is stretched to it), the 2D layer kept in the
    // centred 4:3 rectangle of a wider back buffer (pretransformed draws, as
    // the Switch device's UI rectangle), and the 3D scene's antialiasing:
    // MSAA (the back buffer drawn multisampled, resolved when the 2D layer
    // starts) or FXAA over it then (finish_scene, PC 42D710; end_frame when no
    // 2D layer came). The setters apply between frames.
    void resize_back_buffer(unsigned width,unsigned height);
    void set_ui_rect(bool on){ui_rect_=on;}
    void set_scene_in_ui_rect(bool on)override{scene_in_ui_rect_=on;}
    void set_fxaa(bool on){fxaa_=on;}
    void set_msaa(unsigned samples){msaa_=samples==2||samples==4?samples:1;}   // 1, 2 or 4
    void finish_scene();
    // Port enhancement (visionOS, 3D DEPTH): stereoscopic frames. The back
    // buffer becomes two layers (left eye, right eye) and every draw into it
    // is rendered once per eye in the same pass (Metal vertex amplification).
    // Each eye's clip-space x moves by -/+ separation * (w - convergence): a
    // point at the view depth `convergence` has no parallax, one at infinity
    // `2 * separation` (NDC) between the eyes. Screen-space draws (w == 1:
    // the 2D layer, full-screen quads) stay at zero parallax. Offscreen
    // targets (reflections, shadows) stay mono. separation 0: mono. Applies at
    // the next begin_frame; a GPU without vertex amplification stays mono.
    void set_stereo(float separation,float convergence){stereo_separation_=separation>0?separation:0;stereo_convergence_=convergence;}
    void set_stereo_screen_depth(float depth)override{stereo_screen_depth_=depth>0?depth:0;}
    // The current frame is drawn in stereo (rgba_right() holds the right eye).
    bool stereo()const{return stereo_live_;}
    // The right eye of the last frame shown (rgba() is the left eye).
    const std::vector<std::uint8_t>& rgba_right()const{return right_colour_;}
    // visionOS (pipelined frames): the shown frame stays on the GPU. end_frame
    // copies the back buffer into a ring of three textures instead of reading
    // it back; gpu_frame(eye) is the shown frame's eye (an id<MTLTexture>,
    // 2D; eye 1 = eye 0 when mono), null before the first frame. rgba() and
    // rgba_right() are not updated then. Use it on metal_shared_queue().
    void set_gpu_frames(bool on){gpu_frames_=on;}
    void* gpu_frame(unsigned eye)const;
    std::uint32_t fxaa_passes{},msaa_resolves{};
private:
    bool shade_cached_vertex(std::uint32_t,std::int32_t,const platform::pc_shader::V4*,Vertex&);
    void raster(const Vertex*,unsigned) override;
    void flush();
    // The pretransformed batch covers the whole 640x480 screen in one flat colour
    // (no texture, or one colour over the texels it samples): a screen fade, which
    // the UI rectangle would shrink to a 4:3 box over the wide 3D scene.
    bool screen_fade();
    struct Impl;
    std::unique_ptr<Impl> metal_;
    std::vector<Vertex> batch_;
    std::vector<Vertex> vertex_cache_;
    std::vector<std::uint32_t> vertex_generation_;
    std::uint32_t generation_{};
    bool native_vertices_{};
    std::uint32_t native_type_{},native_start_{},native_count_{};
    std::int32_t native_base_{};
    std::vector<bool> buffer_dirty_;
    unsigned msaa_{1};
    bool pipelined_{};
    bool msaa_live_{};   // the back buffer is the multisampled scene target (begin_frame .. finish_scene)
    bool ui_rect_{},fxaa_{},scene_finished_{},batch_rhw_{};   // batch_rhw_: the batch came from pretransformed vertices
    bool rhw_full_canvas_{};   // set_rhw_full_canvas
    bool scene_in_ui_rect_{};   // 3D draws too go in the 4:3 rectangle (set_scene_in_ui_rect)
    float stereo_separation_{},stereo_convergence_{};
    float stereo_screen_depth_{};   // set_stereo_screen_depth (0: screen-space draws at zero parallax)
    bool stereo_live_{},gpu_frames_{};
    std::vector<std::uint8_t> right_colour_;
};
}
