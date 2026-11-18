// visionOS: the game's screen composed with Metal (vision_compositor.hpp), the
// frame slots the window copies (vision_bridge.h), and mac/source's SdlDisplay
// (the renderer's display) implemented on them.
#include "vision_compositor.hpp"
#include "vision_bridge.h"
#include "sdl_backend.hpp"
#include "metal_d3d9.hpp"
#include "platform/pc_dds.hpp"
#include "enhancements/frame_rate.hpp"
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <mach/mach.h>
// The runtime's autorelease pool calls (what @autoreleasepool compiles to): the
// game thread's per-frame pool spans C++ code (renderer, D3D9 device).
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);

namespace outrun::vision {
namespace {
constexpr const char* Shaders=R"(#include <metal_stdlib>
using namespace metal;
struct In { float2 p; float2 uv; float4 c; };
struct V { float4 p [[position]]; float2 uv; float4 c; };
vertex V compose_vs(uint i [[vertex_id]],const device In* q [[buffer(0)]],constant float2& canvas [[buffer(1)]]){
    V v; v.p=float4(q[i].p.x/canvas.x*2.0-1.0,1.0-q[i].p.y/canvas.y*2.0,0.0,1.0); v.uv=q[i].uv; v.c=q[i].c; return v;
}
fragment float4 compose_fs(V v [[stage_in]],texture2d<float> t [[texture(0)]],sampler s [[sampler(0)]]){ return t.sample(s,v.uv)*v.c; }
struct C { float4 p [[position]]; float2 uv; };
vertex C copy_vs(uint i [[vertex_id]]){
    // Written bottom row first: RealityKit samples the eye textures with v up (as USD images).
    const float2 a=float2(float((i<<1)&2),float(i&2)); C c; c.p=float4(a*2.0-1.0,0.0,1.0); c.uv=a; return c;
}
// The composed eye holds the game's (sRGB-encoded) bytes: decoded for an sRGB
// target, which encodes them again, so the window shows them unchanged.
fragment float4 copy_fs(C c [[stage_in]],texture2d<float> t [[texture(0)]],constant uint& srgb [[buffer(0)]]){
    constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::nearest);
    float4 v=t.sample(s,c.uv);
    if(srgb){ const float3 x=v.rgb; v.rgb=select(pow((x+0.055)/1.055,2.4),x/12.92,x<=0.04045); }
    return v;
}
)";
enum class SlotState {Free,Composing,Ready,Presenting};
struct Slot {
    id<MTLTexture> eye[2];
    unsigned width{},height{};
    SlotState state{SlotState::Free};
    std::uint64_t serial{};
};
struct Picture {
    id<MTLTexture> once;                 // uploaded once (update false)
    std::array<id<MTLTexture>,3> slot;   // per slot (update true)
    unsigned width{},height{};
};
struct Item {Texture left,right;Vertex quad[4];bool blend;};
struct State {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> blend,opaque;
    id<MTLLibrary> library;
    id<MTLSamplerState> sampler;
    std::map<MTLPixelFormat,id<MTLRenderPipelineState>> copies;   // main thread
    std::mutex mutex;
    std::condition_variable changed;
    std::array<Slot,3> slots;
    std::uint64_t serial{};
    int composing{-1};
    unsigned canvas_width{960},canvas_height{720};
    std::vector<Item> items;
    std::map<const void*,Picture> pictures;
    std::chrono::steady_clock::time_point next_frame{};
    // The headset's refreshes (the window's RealityKit updates, or2_vision_take_frame)
    // and the last one a frame was paced to.
    std::uint64_t refreshes{},paced{};
};
std::atomic<float> g_window_width{0.f},g_aspect{4.f/3.f};
std::atomic<bool> g_active{true};
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
id<MTLRenderPipelineState> make_pipeline(State& s,NSString* vs,NSString* fs,MTLPixelFormat format,bool blend){
    auto d=[MTLRenderPipelineDescriptor new];
    d.vertexFunction=[s.library newFunctionWithName:vs];d.fragmentFunction=[s.library newFunctionWithName:fs];
    d.colorAttachments[0].pixelFormat=format;
    if(blend){   // SDL_BLENDMODE_BLEND, as the other ports' SDL renderer
        auto c=d.colorAttachments[0];c.blendingEnabled=YES;
        c.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;c.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
        c.sourceAlphaBlendFactor=MTLBlendFactorOne;c.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    }
    NSError* e=nil;auto p=[s.device newRenderPipelineStateWithDescriptor:d error:&e];
    if(!p)throw std::runtime_error(std::string("visionOS compositor pipeline: ")+(e?e.localizedDescription.UTF8String:"?"));
    return p;
}
State& state(){
    static State* s=[]{
        auto* s=new State;
        s->device=MTLCreateSystemDefaultDevice();require(s->device!=nil,"No Metal GPU available");
        // The D3D9 device's queue: the frames it leaves on the GPU are read in order with its writes.
        s->queue=(__bridge id<MTLCommandQueue>)mac::metal_shared_queue();require(s->queue!=nil,"Cannot create the compositor queue");
        NSError* e=nil;s->library=[s->device newLibraryWithSource:@(Shaders) options:nil error:&e];
        if(!s->library)throw std::runtime_error(std::string("visionOS compositor shaders: ")+(e?e.localizedDescription.UTF8String:"?"));
        s->blend=make_pipeline(*s,@"compose_vs",@"compose_fs",MTLPixelFormatRGBA8Unorm,true);
        s->opaque=make_pipeline(*s,@"compose_vs",@"compose_fs",MTLPixelFormatRGBA8Unorm,false);
        auto sd=[MTLSamplerDescriptor new];sd.minFilter=sd.magFilter=MTLSamplerMinMagFilterLinear;
        sd.sAddressMode=sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
        s->sampler=[s->device newSamplerStateWithDescriptor:sd];require(s->sampler!=nil,"Cannot create the compositor sampler");
        return s;
    }();
    return *s;
}
id<MTLTexture> new_texture(State& s,unsigned w,unsigned h,MTLTextureUsage usage,MTLStorageMode storage){
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
    td.usage=usage;td.storageMode=storage;
    auto t=[s.device newTextureWithDescriptor:td];require(t!=nil,"Cannot allocate a compositor texture");return t;
}
}

void begin(unsigned cw,unsigned ch,unsigned pixel_height){
    auto& s=state();
    if(!cw||!ch)return;
    s.canvas_width=cw;s.canvas_height=ch;g_aspect=float(cw)/float(ch);
    s.items.clear();
    std::unique_lock lock(s.mutex);
    if(s.composing>=0)return;   // the last frame was never submitted: compose into it again
    for(;;){
        for(int i=0;i<3&&s.composing<0;++i)if(s.slots[i].state==SlotState::Free)s.composing=i;
        if(s.composing>=0)break;
        s.changed.wait_for(lock,std::chrono::milliseconds(100));
    }
    auto& slot=s.slots[s.composing];slot.state=SlotState::Composing;
    lock.unlock();
    // The eyes' pixels: the scene's height (1080 .. 2160), the canvas's aspect.
    const unsigned h=std::clamp(pixel_height,1080u,2160u),w=(unsigned(std::lround(double(h)*cw/ch))+1u)&~1u;
    if(slot.width!=w||slot.height!=h)@autoreleasepool {
        for(auto& e:slot.eye)e=new_texture(s,w,h,MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead,MTLStorageModePrivate);
        slot.width=w;slot.height=h;
    }
}
Texture texture(const void* key,const std::uint8_t* rgba,unsigned w,unsigned h,bool update){
    auto& s=state();
    if(!rgba||!w||!h)throw std::runtime_error("invalid compositor picture");
    auto& p=s.pictures[key];
    if(p.width!=w||p.height!=h){p=Picture{};p.width=w;p.height=h;}
    id<MTLTexture> __strong* t=&p.once;
    if(update){require(s.composing>=0,"compositor picture outside a frame");t=&p.slot[std::size_t(s.composing)];}
    const bool fresh=*t==nil;
    if(fresh)*t=new_texture(s,w,h,MTLTextureUsageShaderRead,MTLStorageModeShared);
    // A slot's copy is only uploaded while that slot is composed: the window is done with it.
    if(fresh||update)[*t replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:rgba bytesPerRow:std::size_t(w)*4];
    return (__bridge void*)*t;
}
void draw(Texture left,Texture right,const Vertex (&quad)[4],bool blend){
    auto& s=state();if(!left||s.composing<0)return;
    Item item{left,right,{},blend};std::copy(std::begin(quad),std::end(quad),item.quad);s.items.push_back(item);
}
bool submit(std::string& error){
    auto& s=state();if(s.composing<0)return true;
    @autoreleasepool {
        const int index=s.composing;auto& slot=s.slots[std::size_t(index)];
        id<MTLCommandBuffer> command=[s.queue commandBuffer];
        if(!command){error="Cannot allocate the compositor command buffer";return false;}
        const float canvas[2]{float(s.canvas_width),float(s.canvas_height)};
        for(unsigned eye=0;eye<2;++eye){
            auto p=[MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture=slot.eye[eye];p.colorAttachments[0].loadAction=MTLLoadActionClear;
            p.colorAttachments[0].storeAction=MTLStoreActionStore;p.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
            auto e=[command renderCommandEncoderWithDescriptor:p];
            if(!e){error="Cannot encode the composed frame";return false;}
            [e setVertexBytes:canvas length:sizeof canvas atIndex:1];[e setFragmentSamplerState:s.sampler atIndex:0];
            for(const auto& item:s.items){
                [e setRenderPipelineState:item.blend?s.blend:s.opaque];
                [e setVertexBytes:item.quad length:sizeof item.quad atIndex:0];
                id<MTLTexture> t=(__bridge id<MTLTexture>)(eye&&item.right?item.right:item.left);
                [e setFragmentTexture:t atIndex:0];
                [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            }
            [e endEncoding];
        }
        [command addCompletedHandler:^(id<MTLCommandBuffer> c){
            auto& st=state();std::lock_guard lock(st.mutex);
            auto& done=st.slots[std::size_t(index)];
            if(c.status!=MTLCommandBufferStatusCompleted){done.state=SlotState::Free;st.changed.notify_all();return;}
            // Only the newest frame waits for the window: an older one not taken yet is dropped.
            for(auto& other:st.slots)if(other.state==SlotState::Ready)other.state=SlotState::Free;
            done.state=SlotState::Ready;done.serial=++st.serial;st.changed.notify_all();
        }];
        [command commit];
        s.composing=-1;s.items.clear();
    }
    // Frames at the FRAME RATE. 90: one per refresh of the headset (the game
    // still ticks at 60 Hz and interpolates between ticks, enhancements/frame_rate.hpp),
    // paced by the window's refreshes so that it never drifts against them;
    // a refresh that passed while this frame was composed is not waited for.
    const unsigned rate=std::max(60u,unsigned(enhancements::display_rate()));
    const auto period=std::chrono::nanoseconds(1000000000/rate);
    if(rate>=90u){
        std::unique_lock lock(s.mutex);
        if(s.refreshes==s.paced&&g_active)s.changed.wait_for(lock,2*period,[&]{return s.refreshes!=s.paced;});
        s.paced=s.refreshes;
        return true;
    }
    const auto now=std::chrono::steady_clock::now();
    if(s.next_frame==std::chrono::steady_clock::time_point{})s.next_frame=now;
    s.next_frame+=period;if(now>s.next_frame)s.next_frame=now;
    std::this_thread::sleep_until(s.next_frame);
    return true;
}
void show_picture(const std::uint8_t* rgba,unsigned w,unsigned h){
    if(!rgba||!w||!h)return;
    @autoreleasepool {
    auto& s=state();
    try{
        begin(s.canvas_width,s.canvas_height,std::max(h,1080u));
        static const int key=0;
        auto t=texture(&key,rgba,w,h,true);
        // Letterboxed: the picture's aspect in the window's.
        const float cw=float(s.canvas_width),ch=float(s.canvas_height),scale=std::min(cw/float(w),ch/float(h));
        const float x0=(cw-float(w)*scale)/2,y0=(ch-float(h)*scale)/2,x1=cw-x0,y1=ch-y0;
        const Vertex quad[4]{{x0,y0,0,0,1,1,1,1},{x1,y0,1,0,1,1,1,1},{x0,y1,0,1,1,1,1,1},{x1,y1,1,1,1,1,1,1}};
        draw(t,nullptr,quad,false);
        std::string error;if(!submit(error))std::fprintf(stderr,"visionOS display: %s\n",error.c_str());
    }catch(const std::exception& e){std::fprintf(stderr,"visionOS display: %s\n",e.what());}
    }
}
float window_width_meters(){return g_window_width;}
bool window_active(){return g_active;}
bool terminating(){return false;}
}

// ---- The application side (vision_bridge.h) ----
using namespace outrun::vision;
extern "C" void or2_vision_set_active(bool active){g_active=active;}
extern "C" void or2_vision_set_window_width(float meters){if(std::isfinite(meters)&&meters>0)g_window_width=meters;}
extern "C" float or2_vision_window_aspect(void){return g_aspect;}
extern "C" id<MTLDevice> or2_vision_device(void){return state().device;}
extern "C" int or2_vision_take_frame(int* width,int* height){
    auto& s=state();std::lock_guard lock(s.mutex);
    ++s.refreshes;s.changed.notify_all();   // called once per refresh: the game's pacing (submit)
    int newest=-1;
    for(int i=0;i<3;++i)if(s.slots[i].state==SlotState::Ready&&(newest<0||s.slots[i].serial>s.slots[newest].serial))newest=i;
    if(newest<0)return -1;
    auto& slot=s.slots[newest];slot.state=SlotState::Presenting;
    *width=int(slot.width);*height=int(slot.height);return newest;
}
extern "C" void or2_vision_encode_frame(int index,id<MTLCommandBuffer> command,id<MTLTexture> left,id<MTLTexture> right){
    auto& s=state();if(index<0||index>2)return;
    auto& slot=s.slots[std::size_t(index)];
    id<MTLTexture> targets[2]{left,right};
    for(unsigned eye=0;eye<2;++eye){
        const MTLPixelFormat format=targets[eye].pixelFormat;
        auto& pipeline=s.copies[format];
        if(!pipeline)pipeline=make_pipeline(s,@"copy_vs",@"copy_fs",format,false);
        const std::uint32_t srgb=format==MTLPixelFormatRGBA8Unorm_sRGB||format==MTLPixelFormatBGRA8Unorm_sRGB;
        auto p=[MTLRenderPassDescriptor renderPassDescriptor];
        p.colorAttachments[0].texture=targets[eye];p.colorAttachments[0].loadAction=MTLLoadActionDontCare;p.colorAttachments[0].storeAction=MTLStoreActionStore;
        auto e=[command renderCommandEncoderWithDescriptor:p];if(!e)return;
        [e setRenderPipelineState:pipeline];[e setFragmentTexture:slot.eye[eye] atIndex:0];[e setFragmentBytes:&srgb length:sizeof srgb atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
    }
}
extern "C" void or2_vision_release_frame(int index){
    auto& s=state();if(index<0||index>2)return;
    std::lock_guard lock(s.mutex);
    if(s.slots[std::size_t(index)].state==SlotState::Presenting)s.slots[std::size_t(index)].state=SlotState::Free;
    s.changed.notify_all();
}

// ---- mac/source/sdl_backend.hpp's SdlDisplay on the compositor ----
namespace outrun::mac_runtime {
namespace {
Vertex corner(float x,float y,float u,float v){return {x,y,u,v,1,1,1,1};}
unsigned g_scene_height=1440;   // pixel height of the eyes: the PC scene's (rgba / rgba_stereo)
}
SdlDisplay::~SdlDisplay()=default;
bool SdlDisplay::open(unsigned w,unsigned h,std::string& error){
    try{state();}catch(const std::exception& e){error=e.what();return false;}
    width_=w;height_=h;
    std::fprintf(stdout,"visionOS display: Metal D3D9 rasterisation, Metal composition, RealityKit stereo window\n");
    return true;
}
bool SdlDisplay::resize(unsigned w,unsigned h,std::string&){if(!w||!h)return false;width_=w;height_=h;return true;}
float SdlDisplay::screen_aspect()const{return 0;}   // a resizable window: no FULL SCREEN
void SdlDisplay::clear(){begin(width_,height_,g_scene_height);}
SDL_Texture* SdlDisplay::upload(const void* key,const std::uint8_t* pixels,unsigned w,unsigned h,bool update){
    return static_cast<SDL_Texture*>(texture(key,pixels,w,h,update));
}
SDL_Texture* SdlDisplay::texels(const void* key,platform::MeshPreviewTextureFormat f,unsigned w,unsigned h,const std::vector<std::uint8_t>& bytes){
    auto i=textures_.find(key);if(i!=textures_.end())return i->second.texture;
    platform::PcDdsTexture t{};t.width=w;t.height=h;t.levels=1;
    t.format=f==platform::MeshPreviewTextureFormat::bc1?platform::PcDdsTexture::Format::bc1:
        f==platform::MeshPreviewTextureFormat::bc2?platform::PcDdsTexture::Format::bc2:
        f==platform::MeshPreviewTextureFormat::bc3?platform::PcDdsTexture::Format::bc3:platform::PcDdsTexture::Format::rgba8;
    t.faces.resize(1);t.faces[0].push_back({w,h,bytes});
    auto rgba=platform::pc_dds_rgba(t,0,0);
    if(rgba.size()!=std::size_t(w)*h*4)throw std::runtime_error("DDS decode size mismatch");
    auto* texture=upload(key,rgba.data(),w,h,false);textures_[key]={texture,w,h};return texture;
}
void SdlDisplay::quad(const std::array<platform::MeshPreviewVertex,4>& v,SDL_Texture* texture,unsigned,unsigned){
    // SDL_RenderGeometry's corners (0, 1, 2), (2, 3, 0) as one strip: 0, 1, 3, 2.
    Vertex q[4];const unsigned order[4]{0,1,3,2};
    for(unsigned k=0;k<4;++k){const auto& c=v[order[k]];
        auto channel=[&](unsigned i){return std::clamp(c.color[i],0.f,1.f);};
        q[k]={(c.position[0]+1)*0.5f*float(width_),(1-c.position[1])*0.5f*float(height_),
              std::clamp(c.uv[0],0.f,1.f),std::clamp(c.uv[1],0.f,1.f),channel(0),channel(1),channel(2),channel(3)};}
    draw(texture,nullptr,q,true);
}
void SdlDisplay::rgba(const void* key,const std::uint8_t* bytes,unsigned w,unsigned h,int x,int y,int width,int height,bool opaque){
    if(opaque&&height>=int(height_))g_scene_height=h;
    auto* t=upload(key,bytes,w,h,true);
    const float x0=float(x),y0=float(y),x1=float(x+width),y1=float(y+height);
    const Vertex q[4]{corner(x0,y0,0,0),corner(x1,y0,1,0),corner(x0,y1,0,1),corner(x1,y1,1,1)};
    draw(t,nullptr,q,!opaque);
}
void SdlDisplay::rgba_stereo(const void* key,const std::uint8_t* left,const std::uint8_t* right,unsigned w,unsigned h,int x,int y,int width,int height){
    if(height>=int(height_))g_scene_height=h;
    auto* l=upload(key,left,w,h,true);
    auto* r=upload(static_cast<const char*>(key)+1,right,w,h,true);
    const float x0=float(x),y0=float(y),x1=float(x+width),y1=float(y+height);
    const Vertex q[4]{corner(x0,y0,0,0),corner(x1,y0,1,0),corner(x0,y1,0,1),corner(x1,y1,1,1)};
    draw(l,r,q,false);
}
void SdlDisplay::gpu_frame(void* left,void* right,unsigned pixel_height,int x,int y,int width,int height){
    if(height>=int(height_))g_scene_height=pixel_height;
    const float x0=float(x),y0=float(y),x1=float(x+width),y1=float(y+height);
    const Vertex q[4]{corner(x0,y0,0,0),corner(x1,y0,1,0),corner(x0,y1,0,1),corner(x1,y1,1,1)};
    draw(left,right,q,false);
}
SdlDisplay::Stereo SdlDisplay::stereo(unsigned level)const{
    if(!level)return {};
    // At level 5 the farthest scenery's disparity is the distance between the
    // eyes (63 mm) at the window's physical width: far things look truly far.
    // Lower levels flatten the depth in proportion.
    float width=window_width_meters();if(!(width>0.05f))width=1.2f;
    Stereo s{std::min(float(level),5.f)/5.f*0.063f/width,2.5f};
    if(const char* v=std::getenv("OR2_VISION_SEPARATION"))s.separation=std::strtof(v,nullptr);
    if(const char* v=std::getenv("OR2_VISION_CONVERGENCE"))s.convergence=std::strtof(v,nullptr);
    return s;
}
bool SdlDisplay::read_pixels(std::vector<std::uint8_t>&,std::string& error){error="read_pixels is not available on visionOS";return false;}
bool SdlDisplay::present(std::string& error){
    const bool ok=submit(error);
    // The game thread (vision_main.mm) has no run loop to drain autorelease
    // pools: without this every frame's autoreleased Metal objects (command
    // buffers, which keep the textures and buffers they used) live forever.
    static thread_local void* pool=nullptr;
    if(pool)objc_autoreleasePoolPop(pool);
    pool=objc_autoreleasePoolPush();
    // Diagnostics (OR2_TRACE): the application's memory footprint every 10 s.
    static const bool trace=std::getenv("OR2_TRACE")!=nullptr;static unsigned frames=0;
    if(trace&&++frames%900u==0u){
        task_vm_info_data_t info{};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
        if(task_info(mach_task_self(),TASK_VM_INFO,task_info_t(&info),&count)==KERN_SUCCESS)
            std::fprintf(stderr,"[visionos] memory footprint %.0f MB\n",double(info.phys_footprint)/1048576.0);
    }
    return ok;
}
}
