// macOS adaptation of ps5/source/renderer_present.inc; reference baseline remains unchanged.
// Included inside outrun::mac_runtime after RendererImpl and the UI helpers.
namespace {
bool present_frame(RendererImpl& r){
    try{
        const auto begin=std::chrono::steady_clock::now();
        bool rasterize=true;
#ifdef OR2_MAC_HOST
        if(const char* n=std::getenv("OR2_MAC_RENDER_EVERY")){
            const auto every=std::max(1ul,std::strtoul(n,nullptr,10));rasterize=r.stats.frames%every==0u;
        }
        if(const char* first=std::getenv("OR2_MAC_RENDER_BEGIN")){
            rasterize=r.stats.frames>=std::strtoul(first,nullptr,10);
            if(const char* last=std::getenv("OR2_MAC_RENDER_END"))rasterize&=r.stats.frames<=std::strtoul(last,nullptr,10);
        }
#endif
        // Count-only is a host integration acceleration only. The real macOS
        // binary always executes the pixel shaders and presents every frame.
        r.device->count_only=!rasterize;
#ifndef OR2_MAC_OPENGL
        if(r.video_pending){r.video_pending=false;r.device->resize_back_buffer(r.video_width,r.video_height);}
        r.device->set_ui_rect(g_widescreen);r.device->set_fxaa(r.video_fxaa);r.device->set_msaa(r.video_msaa);
        // The canvas: 16:9 on the Mac; where the screen is fixed (iOS) the
        // widescreen aspect (16:9 or the screen's), or the screen's aspect for
        // the 4:3 screen (pillarboxed in it). SDL letterboxes a canvas narrower or wider than the screen.
        {const float screen=r.display->screen_aspect();
#ifdef OR2_VISIONOS
         // visionOS: the window takes the canvas's aspect (4:3 or 16:9, no full screen).
         const float aspect=g_widescreen?g_aspect:4.f/3.f;(void)screen;
#else
         const float aspect=screen<=0?16.f/9.f:g_widescreen?g_aspect:screen;
#endif
         const auto width=std::max(FramebufferHeight*4u/3u,(std::uint32_t(float(FramebufferHeight)*aspect+0.5f)+1u)&~1u);
         if(width!=g_canvas_width&&r.display->resize(width,FramebufferHeight,r.last_error))g_canvas_width=width;}
#endif
#ifdef OR2_VISIONOS
        // 3D DEPTH: the eye separation for the window's physical width (SdlDisplay::stereo, visionOS).
        {const auto eyes=r.display->stereo(r.stereo_depth);r.device->set_stereo(eyes.separation,eyes.convergence);}
#endif
        r.device->begin_frame();
        if(!r.start_loading_visible&&r.scene){
            ProfileScope record(profile_key(ProfileRenderer,0,ProfPcRecord));
            r.device->clear(3u,0xff000000u,1.0f,0u);
            r.scene(*r.device);
            if(!r.device->errors.empty())throw std::runtime_error(r.device->errors.back());
        }
        {ProfileScope fence(profile_key(ProfileRenderer,0,ProfPcFence));r.device->end_frame();}
        if(rasterize)r.stats.last_gpu_ns=r.device->gpu_nanoseconds();
        if(rasterize)frame_profile().add(profile_key(ProfileRenderer,0,ProfPcGpu),r.stats.last_gpu_ns);
        r.device->count_only=false;
        if(!rasterize)return true;
        auto& display=*r.display;display.clear();
        if(r.start_loading_visible){
            const auto index=r.loading_scene_index+r.loading_scene_offset;
            if(index>=r.frontend.scenes.size())throw std::runtime_error("loading scene index outside bank");
            const auto token=r.frontend.scenes[index].token;
            std::vector<platform::GameUiDraw> draws;
            if(!platform::game_ui_scene_frame_draws(r.loading_animation,token&0xffffu,r.loading_frame,draws,&r.last_error))return false;
            // The loading pack's textures follow the frontend's in the shared bank (append_start_loading).
            constexpr auto base=platform::FrontendPreviewSourceTextureCount;
            for(auto& d:draws){const auto slot=frontend_texture_slot(r.frontend,d.texture+base);
                if(slot>=r.frontend.textures.size())throw std::runtime_error("loading texture outside bank");
                const auto& texture=r.frontend.textures[slot];
                display.quad(make_animated_frontend_quad(r.frontend,d,base),display.texels(&texture,texture.format,texture.width,texture.height,texture.bytes),texture.width,texture.height);}
            r.stats.loading_animation_frame=r.loading_frame++;++r.stats.loading_animation_updates;
        }else{
            const auto& pc=*r.device;
            const auto ow=g_widescreen?g_canvas_width:FramebufferHeight*4u/3u;
            const auto ox=(g_canvas_width-ow)/2u;
#ifdef OR2_MAC_OPENGL
            display.pc_frame(pc.back_texture(),int(ox),0,int(ow),int(FramebufferHeight));
#else
            // D3D9 presents RGB regardless of the back buffer alpha. The PC
            // sprite flush writes RGB only; treating it as an overlay hides it.
#ifdef OR2_VISIONOS
            if(void* left=pc.gpu_frame(0))   // the frame stays on the GPU (set_gpu_frames)
                display.gpu_frame(left,pc.gpu_frame(1),pc.presentation_height(),int(ox),0,int(ow),int(FramebufferHeight));
            else if(pc.stereo()&&pc.rgba_right().size()==pc.rgba().size())
                display.rgba_stereo(r.device.get(),pc.rgba().data(),pc.rgba_right().data(),pc.presentation_width(),pc.presentation_height(),int(ox),0,int(ow),int(FramebufferHeight));
            else
#endif
            display.rgba(r.device.get(),pc.rgba().data(),pc.presentation_width(),pc.presentation_height(),int(ox),0,int(ow),int(FramebufferHeight),true);
#endif
            // The title movie stays 4:3 in a 16:9 frame.
            constexpr auto mw=FramebufferHeight*4u/3u;const auto mx=(g_canvas_width-mw)/2u;
            if(r.movie_visible&&!r.movie_rgba.empty())display.rgba(&r.movie_rgba,r.movie_rgba.data(),r.movie_width,r.movie_height,int(mx),0,int(mw),int(FramebufferHeight));
            if(r.frontend_visible){
                if(r.frontend_sprites)paint_frontend(r,display,r.stats.frames);
                else for(const auto& d:r.frontend_frame_draws){const auto slot=frontend_texture_slot(r.frontend,d.texture);
                    if(slot>=r.frontend.textures.size())throw std::runtime_error("frontend texture outside bank");
                    const auto& t=r.frontend.textures[slot];display.quad(make_animated_frontend_quad(r.frontend,d,0u),display.texels(&t,t.format,t.width,t.height,t.bytes),t.width,t.height);}
                if(r.frontend_overlay_token){std::vector<platform::GameUiDraw> draws;
                    if(!platform::game_ui_scene_frame_draws(r.frontend_animation,r.frontend_overlay_token&0xffffu,r.frontend_overlay_frame,draws,&r.last_error))return false;
                    for(const auto& d:draws){const auto slot=frontend_texture_slot(r.frontend,d.texture);if(slot>=r.frontend.textures.size())return false;
                        const auto& t=r.frontend.textures[slot];display.quad(make_animated_frontend_quad(r.frontend,d,0u),display.texels(&t,t.format,t.width,t.height,t.bytes),t.width,t.height);}}
            }
        }
#if defined(OR2_MAC_HOST)&&!defined(OR2_VISIONOS)
        // Testing (OR2_HOST_CAR_PIXELS=1): the player car's red pixels in the bottom
        // centre of the 3D frame, about once a second (car visibility, unattended runs).
        {static const bool probe=std::getenv("OR2_HOST_CAR_PIXELS")!=nullptr;
         if(probe&&r.stats.frames%59u==0u&&!r.start_loading_visible){   // 59: both tick and in-between frames at 90
            const auto& px=r.device->rgba();const unsigned w=r.device->presentation_width(),h=r.device->presentation_height();
            std::size_t red=0;
            if(px.size()>=std::size_t(w)*h*4)for(unsigned y=h*55/100;y<h*95/100;y+=2)for(unsigned x=w*30/100;x<w*70/100;x+=2){
                const auto* p=&px[(std::size_t(y)*w+x)*4];if(p[0]>140&&p[1]<70&&p[2]<70)++red;}
            std::fprintf(stderr,"[car-px] frame=%u red=%zu\n",r.stats.frames,red);}}
        // OR2_HOST_CAR_SHADE=1: every frame, the mean red of the car's red pixels in
        // the same box (the car's brightness; compare with a PC video).
        {static const bool probe=std::getenv("OR2_HOST_CAR_SHADE")!=nullptr;
         if(probe&&!r.start_loading_visible){
            const auto& px=r.device->rgba();const unsigned w=r.device->presentation_width(),h=r.device->presentation_height();
            std::size_t n=0,sum=0;
            if(px.size()>=std::size_t(w)*h*4)for(unsigned y=h*55/100;y<h*95/100;y+=2)for(unsigned x=w*30/100;x<w*70/100;x+=2){
                const auto* p=&px[(std::size_t(y)*w+x)*4];if(p[0]>p[1]+50&&p[0]>p[2]+50){sum+=p[0];++n;}}
            std::fprintf(stderr,"[car-shade] frame=%u n=%zu red=%.1f\n",r.stats.frames,n,n?double(sum)/n:0.0);}}
        // OR2_HOST_SKY_SHADE=1: every frame, the mean brightness of the blue sky pixels in
        // the upper right of the 3D frame (sky / glow / flare flicker; compare with a PC video).
        {static const bool probe=std::getenv("OR2_HOST_SKY_SHADE")!=nullptr;
         if(probe&&!r.start_loading_visible){
            const auto& px=r.device->rgba();const unsigned w=r.device->presentation_width(),h=r.device->presentation_height();
            std::size_t n=0,sum=0;
            if(px.size()>=std::size_t(w)*h*4)for(unsigned y=h*12/100;y<h*32/100;y+=2)for(unsigned x=w*53/100;x<w*97/100;x+=2){
                const auto* p=&px[(std::size_t(y)*w+x)*4];if(p[2]>p[0]+40&&p[2]>p[1]+10){sum+=p[0]+p[1]+p[2];++n;}}
            std::size_t all=0,m=0;   // the whole band across the top (the sun and its glow)
            if(px.size()>=std::size_t(w)*h*4)for(unsigned y=h*12/100;y<h*40/100;y+=2)for(unsigned x=0;x<w;x+=2){
                const auto* p=&px[(std::size_t(y)*w+x)*4];all+=p[0]+p[1]+p[2];++m;}
            std::fprintf(stderr,"[sky-shade] frame=%u n=%zu level=%.1f band=%.2f\n",r.stats.frames,n,n?double(sum)/n/3.0:0.0,m?double(all)/m/3.0:0.0);}}
#endif
        screen_request_frames_per_second(enhancements::display_rate());   // (iOS: lets ProMotion go past 60 Hz)
#ifndef OR2_VISIONOS
        display.set_frames_per_second(enhancements::display_rate());
#endif
        const auto present_begin=std::chrono::steady_clock::now();
        if(!display.present(r.last_error))return false;
        {   // Diagnostics (OR2_TRACE): frames shown per second, and the time spent in present (vsync waits).
            static const bool trace=std::getenv("OR2_TRACE")!=nullptr;
            static auto window=std::chrono::steady_clock::now();static unsigned shown=0;static double present_ms=0;
            if(trace){
                const auto now=std::chrono::steady_clock::now();++shown;
                present_ms+=std::chrono::duration<double,std::milli>(now-present_begin).count();
                if(now-window>=std::chrono::seconds(5)){
                    const double s=std::chrono::duration<double>(now-window).count();
                    std::fprintf(stderr,"[present] %.1f frames/s (rate %u, screen %u Hz), present %.2f ms/frame\n",
                        shown/s,unsigned(enhancements::display_rate()),r.screen_hz,present_ms/shown);
                    window=now;shown=0;present_ms=0;
                }
            }
        }
        if(r.screen_hz_age++%120u==0u)r.screen_hz=screen_max_frames_per_second();   // the window may move to another screen
        if(enhancements::display_rate()<=60u&&r.screen_hz>=100u){
            const auto due=r.last_present+std::chrono::microseconds(16667);
            if(std::chrono::steady_clock::now()<due)std::this_thread::sleep_until(due);
        }
        r.last_present=std::chrono::steady_clock::now();
        const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());
        ++r.raster_frames;r.raster_us+=us;r.raster_peak_us=std::max(r.raster_peak_us,us);
        return true;
    }catch(const std::exception& e){r.last_error=e.what();std::fprintf(stderr,"macOS renderer: %s\n",e.what());return false;}
}
}
platform::PcD3D9Device* mac_renderer_pc_device(MacRenderer& renderer){
    auto* r=static_cast<RendererImpl*>(renderer.impl);return r?r->device.get():nullptr;
}
bool mac_renderer_set_pc_scene(MacRenderer& renderer,std::function<void(platform::PcD3D9Device&)> scene){
    auto* r=static_cast<RendererImpl*>(renderer.impl);if(!r)return false;r->scene=std::move(scene);return true;
}
std::string mac_renderer_pc_error(const MacRenderer& renderer){
    const auto* r=static_cast<const RendererImpl*>(renderer.impl);return r?r->last_error:"macOS renderer not initialized";
}
