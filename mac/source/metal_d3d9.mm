#include "metal_d3d9.hpp"
#include "pc_metal_shaders.hpp"
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sstream>
#include <stdexcept>
namespace outrun::mac {
using namespace platform::d3d9;
namespace {
struct alignas(16) Float4 {float v[4]{};};
struct alignas(16) Int4 {std::int32_t v[4]{};};
struct alignas(16) Uniforms {
    Float4 pc[8];Int4 program[4],info,kinds[2];Float4 bump[8],lum[2];
    Int4 op[4],arg[4],misc[4];Float4 konst[4],tfactor;Int4 flags;
    Float4 fog_colour,fog_params,alpha_test;float lod_bias[6]{};
};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void metal_error(NSError* e,const char* operation){throw std::runtime_error(std::string(operation)+": "+(e?e.localizedDescription.UTF8String:"Metal returned nil"));}
Float4 colour(std::uint32_t c){return {{float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,float(c>>24)/255}};}
MTLCompareFunction compare(unsigned v){require(v>=1&&v<=8,"Invalid D3D9 comparison");return MTLCompareFunction(v-1);}
MTLStencilOperation stencil(unsigned v){
    const MTLStencilOperation ops[]{MTLStencilOperationKeep,MTLStencilOperationKeep,MTLStencilOperationZero,MTLStencilOperationReplace,MTLStencilOperationIncrementClamp,MTLStencilOperationDecrementClamp,MTLStencilOperationInvert,MTLStencilOperationIncrementWrap,MTLStencilOperationDecrementWrap};
    require(v<=8,"Invalid D3D9 stencil operation");return ops[v];
}
MTLBlendFactor blend(unsigned v){
    const MTLBlendFactor factors[]{MTLBlendFactorOne,MTLBlendFactorZero,MTLBlendFactorOne,MTLBlendFactorSourceColor,MTLBlendFactorOneMinusSourceColor,MTLBlendFactorSourceAlpha,MTLBlendFactorOneMinusSourceAlpha,MTLBlendFactorDestinationAlpha,MTLBlendFactorOneMinusDestinationAlpha,MTLBlendFactorDestinationColor,MTLBlendFactorOneMinusDestinationColor,MTLBlendFactorSourceAlphaSaturated};
    if(v==14)return MTLBlendFactorBlendColor;                 // D3DBLEND_BLENDFACTOR (RS 0xC1; the glare's blur 415630)
    if(v==15)return MTLBlendFactorOneMinusBlendColor;         // D3DBLEND_INVBLENDFACTOR
    require(v>=1&&v<=11,"Unsupported D3D9 blend factor");return factors[v];
}
MTLBlendOperation blend_op(unsigned v){require(v>=1&&v<=5,"Invalid D3D9 blend operation");return MTLBlendOperation(v-1);}
MTLSamplerAddressMode address(unsigned v){
    switch(v){case 1:return MTLSamplerAddressModeRepeat;case 2:return MTLSamplerAddressModeMirrorRepeat;case 3:return MTLSamplerAddressModeClampToEdge;case 4:return MTLSamplerAddressModeClampToBorderColor;case 5:return MTLSamplerAddressModeMirrorClampToEdge;default:throw std::runtime_error("Unknown D3D9 sampler address mode");}
}
}
constexpr unsigned FfpGeneric=0,FfpAsync=1,FfpSync=2;
constexpr std::size_t FfpLimit=4096;   // distinct fixed-function states specialised (then the generic pipeline)
// The pc_ffp function constants: ffp_fixed and the 13 state vectors (generic: false, zeros).
static MTLFunctionConstantValues* ffp_constants(bool fixed,const std::array<std::int32_t,52>& state){
    auto c=[MTLFunctionConstantValues new];[c setConstantValue:&fixed type:MTLDataTypeBool atIndex:32];
    for(unsigned i=0;i<13;++i)[c setConstantValue:state.data()+4*i type:MTLDataTypeInt4 atIndex:33+i];
    return c;
}
struct MetalD3D9Device::Impl {
    MetalD3D9Device& owner;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> library;
    id<MTLCommandBuffer> command;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLTexture> back,back_depth,dummy2d,dummycube;
    id<MTLBuffer> readback_staging;   // the back buffer (GPU-private) copied here for the CPU
    // A D3D9 Clear becomes the clear load action of the next pass on the same
    // targets (on tile GPUs a pass of its own stores and reloads every target).
    unsigned clear_flags{};std::uint32_t clear_colour{};float clear_z{1};unsigned clear_stencil{};
    id<MTLTexture> scene_copy;               // FXAA source (port enhancement)
    id<MTLTexture> msaa_colour,msaa_depth;   // multisampled scene targets (port enhancement)
    id<MTLRenderPipelineState> fxaa;
    id<MTLSamplerState> fxaa_sampler;
    id<MTLRenderPipelineState> stretch;   // StretchRect (the glare's scene copies, 414E50 / 4152E0)
    id<MTLSamplerState> stretch_point;
    id<MTLTexture> scene_resolve;         // the multisampled scene resolved for a StretchRect from it
    id<MTLBuffer> visibility;
    // Stereo (set_stereo): the back buffer, its depth, the MSAA targets and the
    // FXAA source have `layers` layers (2: left, right eye); a pass on them
    // renders both at once (vertex amplification, `layered`).
    unsigned layers{1};
    bool amplification{};             // the GPU renders 2 views per draw
    bool pass_layered{},layered{};    // the last pass descriptor / the open encoder covers both layers
    std::array<id<MTLTexture>,2> fxaa_views;   // scene_copy's layers as 2D textures
    // Pipelined frames (set_pipelined): the frame the GPU still renders keeps
    // its own vertex arena, visibility results and readback copy (the spare
    // set) while the CPU records the next one in the current set.
    id<MTLBuffer> spare_visibility,spare_staging;
    std::vector<id<MTLBuffer>> spare_vertex_buffers;
    id<MTLCommandBuffer> inflight;unsigned inflight_samples{};bool inflight_done{};
    // Occlusion queries (query_issue): a query's samples are the visibility
    // counts of the draws between its BEGIN and END (slots first..last of the
    // command buffer's results), delivered when that buffer completes.
    struct QueryRange { std::uint32_t handle; unsigned first,last; double scale; };
    std::vector<QueryRange> query_ranges,spare_query_ranges;
    std::map<std::uint32_t,std::pair<std::uint64_t,unsigned>> query_open;   // BEGIN: command generation, first slot
    std::map<std::uint32_t,unsigned> query_pending;                          // ENDs not completed yet
    std::uint64_t command_generation{};
    // gpu_frames_: the back buffer's copies (3, as the queue may still read the
    // shown one while the next two are drawn) and their eyes as 2D textures.
    std::array<id<MTLTexture>,3> presents;
    std::array<std::array<id<MTLTexture>,2>,3> present_eyes;
    unsigned present_next{};int inflight_present{-1},shown_present{-1};
    unsigned copy_present(id<MTLBlitCommandEncoder> blit){
        const unsigned k=present_next;present_next=(k+1)%3;auto& t=presents[k];
        if(!t||t.width!=back.width||t.height!=back.height||t.arrayLength!=back.arrayLength||t.textureType!=back.textureType){
            auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:back.width height:back.height mipmapped:NO];
            td.textureType=back.textureType;td.arrayLength=back.arrayLength;td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageShaderRead;
            t=[device newTextureWithDescriptor:td];require(t!=nil,"Cannot allocate a frame copy");
            for(unsigned eye=0;eye<2;++eye)present_eyes[k][eye]=back.textureType==MTLTextureType2DArray
                ?[t newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1) slices:NSMakeRange(std::min<NSUInteger>(eye,back.arrayLength-1),1)]:t;
        }
        [blit copyFromTexture:back toTexture:t];return k;
    }
    id<MTLBuffer> default_vertex;
    std::map<std::uint32_t,id<MTLBuffer>> buffers;
    std::map<std::uint32_t,id<MTLTexture>> textures,depths;
    std::map<std::string,id<MTLRenderPipelineState>> pipelines;
    // Specialised fixed-function pipelines (flush): requested once per state,
    // compiled by Metal in the background (FfpAsync, the default), at once
    // (FfpSync: OR2_METAL_FFP=sync, for tests) or never (OR2_METAL_FFP=generic).
    struct FfpResults {
        std::mutex lock;std::vector<std::pair<std::string,id<MTLRenderPipelineState>>> ready;std::atomic<unsigned> waiting{};unsigned failed{};
        void done(const std::string& key,id<MTLRenderPipelineState> state){
            std::lock_guard<std::mutex> g(lock);if(state)ready.emplace_back(key,state);else ++failed;++waiting;}
    };
    unsigned ffp_mode{FfpAsync},ffp_built{};
    std::set<std::string> ffp_requested;
    std::shared_ptr<FfpResults> ffp_async=std::make_shared<FfpResults>();
    void collect_ffp(){
        if(!ffp_async->waiting.load(std::memory_order_acquire))return;
        std::lock_guard<std::mutex> g(ffp_async->lock);
        for(auto& r:ffp_async->ready){
            pipelines.emplace(r.first,r.second);++ffp_built;
            static const bool trace=std::getenv("OR2_TRACE")!=nullptr;
            if(trace&&!(ffp_built&(ffp_built-1)))std::fprintf(stderr,"[metal] %u fixed-function pipelines specialised (%u failed)\n",ffp_built,ffp_async->failed);
        }
        ffp_async->ready.clear();ffp_async->waiting=0;
    }
    std::map<std::string,id<MTLFunction>> vertex_functions;
    std::map<std::string,id<MTLDepthStencilState>> depth_states;
    std::map<std::string,id<MTLSamplerState>> samplers;
    std::vector<id<MTLBuffer>> vertex_buffers;
    NSUInteger vertex_offset{};
    unsigned vertex_buffer_index{};
    unsigned samples{};
    std::uint64_t gpu_ns{};
    explicit Impl(MetalD3D9Device& d):owner(d){}
    id<MTLBuffer> buffer(unsigned h){
        auto* o=owner.object(h);require(o&&o->kind==1&&!o->bytes.empty(),"Invalid Metal geometry buffer");
        auto& b=buffers[h];
        // A buffer changed during command recording needs a new snapshot:
        // earlier GPU draws must retain the bytes they originally referenced.
        if(!b||b.length!=o->bytes.size()||(h<owner.buffer_dirty_.size()&&owner.buffer_dirty_[h])){
            b=[device newBufferWithBytes:o->bytes.data() length:o->bytes.size() options:MTLResourceStorageModeShared];
            require(b!=nil,"Cannot allocate Metal geometry buffer");
        }
        if(h<owner.buffer_dirty_.size())owner.buffer_dirty_[h]=false;return b;
    }
    void end_encoder(){if(encoder){[encoder endEncoding];encoder=nil;}layered=false;}
    // Ends the pass; a clear still pending (no draw followed it) runs as a pass of its own.
    void finish_encoder(){
        end_encoder();if(!clear_flags)return;
        start();const unsigned flags=clear_flags;clear_flags=0;
        auto e=[command renderCommandEncoderWithDescriptor:pass(flags,clear_colour,clear_z,clear_stencil)];require(e!=nil,"Cannot encode Metal clear");[e endEncoding];
    }
    void start(){if(!command){command=[queue commandBuffer];require(command!=nil,"Cannot allocate Metal command buffer");samples=0;++command_generation;std::memset(visibility.contents,0,visibility.length);vertex_offset=0;vertex_buffer_index=0;}}
    id<MTLBuffer> upload_vertices(const void* source,NSUInteger bytes,NSUInteger& offset){
        const NSUInteger padded=(bytes+255)&~NSUInteger(255);
        if(vertex_buffer_index<vertex_buffers.size()&&vertex_offset+padded>vertex_buffers[vertex_buffer_index].length){++vertex_buffer_index;vertex_offset=0;}
        if(vertex_buffer_index>=vertex_buffers.size()){
            auto b=[device newBufferWithLength:std::max(NSUInteger(4*1024*1024),padded) options:MTLResourceStorageModeShared];
            require(b!=nil,"Cannot allocate Metal vertex arena");vertex_buffers.push_back(b);
        }else if(padded>vertex_buffers[vertex_buffer_index].length){
            auto b=[device newBufferWithLength:padded options:MTLResourceStorageModeShared];require(b!=nil,"Cannot grow Metal vertex arena");vertex_buffers[vertex_buffer_index]=b;
        }
        auto b=vertex_buffers[vertex_buffer_index];offset=vertex_offset;
        std::memcpy(static_cast<char*>(b.contents)+offset,source,bytes);vertex_offset+=padded;return b;
    }
    void finish(){
        finish_encoder();if(!command)return;
        [command commit];[command waitUntilCompleted];
        completed(command,visibility,samples,query_ranges);
        command=nil;samples=0;
    }
    // A finished command buffer: its status, GPU time and visibility counts.
    void completed(id<MTLCommandBuffer> c,id<MTLBuffer> results,unsigned count,std::vector<QueryRange>& ranges){
        if(c.status!=MTLCommandBufferStatusCompleted)metal_error(c.error,"D3D9 Metal command");
        const auto* values=static_cast<const std::uint64_t*>(results.contents);
        for(unsigned i=0;i<count;++i)owner.pixels_shaded+=values[i];
        for(const auto& r:ranges){
            std::uint64_t n=0;for(unsigned i=r.first;i<r.last&&i<count;++i)n+=values[i];
            const double scaled=std::floor(double(n)*r.scale+0.5);
            if(r.handle-owner.QueryBase<owner.queries_.size())owner.queries_[r.handle-owner.QueryBase].result=std::uint32_t(std::min(scaled,4294967295.0));
            if(auto it=query_pending.find(r.handle);it!=query_pending.end()&&!--it->second)query_pending.erase(it);
        }
        ranges.clear();
        const double elapsed=c.GPUEndTime-c.GPUStartTime;
        if(elapsed>0)gpu_ns+=std::uint64_t(elapsed*1e9);
    }
    // Copies the readback buffer of a finished frame to the CPU back buffer.
    void copy_out(id<MTLBuffer> from){
        auto& destination=owner.colour_target_==owner.BackBuffer?owner.colour_:owner.back_colour_;
        const std::size_t bytes=std::size_t(back.width)*back.height*4;
        destination.resize(bytes);
        if(from&&from.length>=bytes)std::memcpy(destination.data(),from.contents,bytes);
        if(layers<2){owner.right_colour_.clear();return;}
        owner.right_colour_.resize(bytes);
        if(from&&from.length>=2*bytes)std::memcpy(owner.right_colour_.data(),static_cast<const char*>(from.contents)+bytes,bytes);
    }
    // The back buffer's layers into `to` (one after the other), for copy_out.
    void copy_back(id<MTLBlitCommandEncoder> blit,id<MTLBuffer> to,NSUInteger w,NSUInteger h){
        const std::size_t row=std::size_t(w)*4;
        for(unsigned layer=0;layer<layers;++layer)
            [blit copyFromTexture:back sourceSlice:layer sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1)
                 toBuffer:to destinationOffset:row*h*layer destinationBytesPerRow:row destinationBytesPerImage:row*h];
    }
    // Waits for the frame in flight (pipelined) and shows it: before the CPU
    // changes what that frame reads (texture uploads, a resize) or reads back.
    void drain(){
        if(!inflight)return;
        [inflight waitUntilCompleted];
        if(!inflight_done){completed(inflight,spare_visibility,inflight_samples,spare_query_ranges);if(inflight_present>=0)shown_present=inflight_present;else copy_out(spare_staging);}
        inflight=nil;inflight_done=false;
    }
    id<MTLTexture> texture(std::uint32_t handle){
        auto* o=owner.object(handle);if(!o||o->kind!=5)return nil;
        if(auto it=textures.find(handle);it!=textures.end())return it->second;
        const auto& d=o->texture;require(!d.faces.empty()&&!d.faces[0].empty(),"Empty D3D9 texture");
        auto td=[MTLTextureDescriptor new];td.textureType=d.cube?MTLTextureTypeCube:MTLTextureType2D;
        td.width=d.faces[0][0].width;td.height=d.faces[0][0].height;td.mipmapLevelCount=d.levels;
        td.pixelFormat=MTLPixelFormatRGBA8Unorm;td.storageMode=MTLStorageModeShared;
        td.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
        id<MTLTexture> t=[device newTextureWithDescriptor:td];require(t!=nil,"Cannot allocate Metal texture");
        for(unsigned face=0;face<o->rgba.size();++face)for(unsigned level=0;level<o->rgba[face].size();++level){
            const auto& size=d.faces[face][level];const auto& data=o->rgba[face][level];
            // Binding a CPU render target moves its storage into the active
            // framebuffer. The GPU attachment owns that level independently.
            if(data.empty())continue;
            require(data.size()==std::size_t(size.width)*size.height*4,"Invalid RGBA texture level");
            [t replaceRegion:MTLRegionMake2D(0,0,size.width,size.height) mipmapLevel:level slice:face withBytes:data.data() bytesPerRow:std::size_t(size.width)*4 bytesPerImage:data.size()];
        }
        // A texture-pack replacement is never read back or locked: the GPU copy is enough.
        if(o->gpu_only)std::vector<std::vector<std::vector<std::uint8_t>>>().swap(o->rgba);
        textures.emplace(handle,t);return t;
    }
    id<MTLTexture> depth(std::uint32_t handle){
        if(handle==owner.DepthBuffer)return back_depth;if(!handle)return nil;
        if(auto it=depths.find(handle);it!=depths.end())return it->second;
        auto* o=owner.object(handle);require(o&&o->kind==6&&!o->parent,"Invalid Metal depth surface");
        auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:o->width height:o->height mipmapped:NO];
        td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;
        auto t=[device newTextureWithDescriptor:td];require(t!=nil,"Cannot allocate depth/stencil texture");depths.emplace(handle,t);return t;
    }
    MTLRenderPassDescriptor* pass(unsigned flags=0,std::uint32_t c=0,float z=1,unsigned s=0){
        auto p=[MTLRenderPassDescriptor renderPassDescriptor];
        const bool msaa=owner.colour_target_==owner.BackBuffer&&owner.msaa_live_;
        id<MTLTexture> target=msaa?msaa_colour:back;unsigned level=0,slice=0;
        if(owner.colour_target_!=owner.BackBuffer){auto* o=owner.object(owner.colour_target_);require(o&&o->parent,"Invalid Metal colour surface");target=texture(o->parent);level=o->level;slice=o->face;}
        const bool one_eye=layers>1&&owner.colour_target_==owner.BackBuffer&&owner.stereo_eye_>=0;   // set_stereo_eye
        if(one_eye)slice=unsigned(owner.stereo_eye_);
        require(target!=nil,"Metal colour attachment missing");
        p.colorAttachments[0].texture=target;p.colorAttachments[0].level=level;p.colorAttachments[0].slice=slice;
        p.colorAttachments[0].loadAction=flags&1?MTLLoadActionClear:MTLLoadActionLoad;
        p.colorAttachments[0].storeAction=MTLStoreActionStore;
        const auto col=colour(c);p.colorAttachments[0].clearColor=MTLClearColorMake(col.v[0],col.v[1],col.v[2],col.v[3]);
        auto d=depth(owner.depth_target_);
        if(msaa){require(owner.depth_target_==owner.DepthBuffer||!d,"MSAA scene with a separate depth surface");if(d)d=msaa_depth;}
        // Both eyes when the target is the layered back buffer (with its own
        // depth or none); a separate depth surface keeps the pass mono (left eye).
        pass_layered=!one_eye&&layers>1&&owner.colour_target_==owner.BackBuffer&&(!d||d==back_depth||d==msaa_depth);
        if(pass_layered)p.renderTargetArrayLength=layers;
        if(d&&one_eye&&d!=back_depth&&d!=msaa_depth)d=nil;   // a mono depth surface: the eye's pass goes without it
        if(d){
            if(one_eye){p.depthAttachment.slice=slice;p.stencilAttachment.slice=slice;}
            require(d.width>=std::max(NSUInteger(1),target.width>>level)&&d.height>=std::max(NSUInteger(1),target.height>>level),"Metal depth attachment smaller than target");
            p.depthAttachment.texture=d;p.depthAttachment.loadAction=flags&2?MTLLoadActionClear:MTLLoadActionLoad;p.depthAttachment.storeAction=MTLStoreActionStore;p.depthAttachment.clearDepth=z;
            p.stencilAttachment.texture=d;p.stencilAttachment.loadAction=flags&4?MTLLoadActionClear:MTLLoadActionLoad;p.stencilAttachment.storeAction=MTLStoreActionStore;p.stencilAttachment.clearStencil=s&255;
        }else require(!(flags&6),"Clear requests missing Metal depth/stencil surface");
        p.visibilityResultBuffer=visibility;return p;
    }
    id<MTLRenderCommandEncoder> render_encoder(){
        start();
        if(!encoder){const unsigned flags=clear_flags;clear_flags=0;encoder=[command renderCommandEncoderWithDescriptor:pass(flags,clear_colour,clear_z,clear_stencil)];require(encoder!=nil,"Cannot create Metal render encoder");
            layered=pass_layered;
            if(layered){const MTLVertexAmplificationViewMapping views[2]{{0,0},{0,1}};[encoder setVertexAmplificationCount:2 viewMappings:views];}}
        return encoder;
    }
    void allocate_back(unsigned w,unsigned h){
        require(w>0&&h>0&&w<=16384&&h<=16384,"Invalid Metal back buffer size");
        auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
        if(layers>1){td.textureType=MTLTextureType2DArray;td.arrayLength=layers;}
        // GPU-private (losslessly compressed on Apple GPUs); readback() copies it out.
        td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;back=[device newTextureWithDescriptor:td];
        td.pixelFormat=MTLPixelFormatDepth32Float_Stencil8;td.storageMode=MTLStorageModePrivate;back_depth=[device newTextureWithDescriptor:td];
        require(back&&back_depth,"Cannot allocate Metal back buffers");
        scene_copy=nil;fxaa_views={};msaa_colour=msaa_depth=nil;readback_staging=spare_staging=nil;
    }
    void ensure_msaa(unsigned samples){
        if(msaa_colour&&msaa_colour.sampleCount==samples&&msaa_colour.width==back.width&&msaa_colour.height==back.height&&msaa_colour.arrayLength==layers)return;
        require([device supportsTextureSampleCount:samples],"Metal GPU lacks this MSAA sample count");
        auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:back.width height:back.height mipmapped:NO];
        td.textureType=layers>1?MTLTextureType2DMultisampleArray:MTLTextureType2DMultisample;td.arrayLength=layers;
        td.sampleCount=samples;td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;
        msaa_colour=[device newTextureWithDescriptor:td];
        td.pixelFormat=MTLPixelFormatDepth32Float_Stencil8;msaa_depth=[device newTextureWithDescriptor:td];
        require(msaa_colour&&msaa_depth,"Cannot allocate the MSAA scene targets");
    }
    // The multisampled scene into the back buffer, whose depth/stencil is
    // cleared for the 2D layer drawn there next.
    void resolve_msaa(){
        finish_encoder();start();
        MTLRenderPassDescriptor* p;id<MTLRenderCommandEncoder> e;
        for(unsigned layer=0;layer<layers;++layer){   // one eye per resolve
            p=[MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture=msaa_colour;p.colorAttachments[0].slice=layer;p.colorAttachments[0].loadAction=MTLLoadActionLoad;
            p.colorAttachments[0].storeAction=MTLStoreActionMultisampleResolve;p.colorAttachments[0].resolveTexture=back;p.colorAttachments[0].resolveSlice=layer;
            e=[command renderCommandEncoderWithDescriptor:p];require(e!=nil,"Cannot encode the MSAA resolve");[e endEncoding];
        }
        p=[MTLRenderPassDescriptor renderPassDescriptor];if(layers>1)p.renderTargetArrayLength=layers;
        p.colorAttachments[0].texture=back;p.colorAttachments[0].loadAction=MTLLoadActionLoad;p.colorAttachments[0].storeAction=MTLStoreActionStore;
        p.depthAttachment.texture=back_depth;p.depthAttachment.loadAction=MTLLoadActionClear;p.depthAttachment.storeAction=MTLStoreActionStore;p.depthAttachment.clearDepth=1;
        p.stencilAttachment.texture=back_depth;p.stencilAttachment.loadAction=MTLLoadActionClear;p.stencilAttachment.storeAction=MTLStoreActionStore;p.stencilAttachment.clearStencil=0;
        e=[command renderCommandEncoderWithDescriptor:p];require(e!=nil,"Cannot encode the depth clear");[e endEncoding];
    }
};
// Port post-process (not original code): switch/shaders/fxaa_fsh.glsl in MSL,
// over the whole back buffer before the 2D layer.
constexpr const char* FxaaSource=R"(#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; };
vertex V fxaa_vs(uint i [[vertex_id]]){ float2 p=float2(float((i<<1)&2),float(i&2)); V v; v.p=float4(p*2.0-1.0,0.0,1.0); return v; }
static float4 at(texture2d<float> t,sampler s,float2 q,float2 size){ q=clamp(q,float2(0.5),size-0.5); return t.sample(s,q/size,level(0.0)); }
static float luma(float3 c){ return dot(c,float3(0.299,0.587,0.114)); }
fragment float4 fxaa_fs(V v [[stage_in]],texture2d<float> scene [[texture(0)]],sampler s [[sampler(0)]]){
    const float2 size=float2(scene.get_width(),scene.get_height());
    const float2 q=v.p.xy;
    const float4 cM=at(scene,s,q,size);
    const float lNW=luma(at(scene,s,q+float2(-1.0,-1.0),size).rgb),lNE=luma(at(scene,s,q+float2(1.0,-1.0),size).rgb);
    const float lSW=luma(at(scene,s,q+float2(-1.0,1.0),size).rgb),lSE=luma(at(scene,s,q+float2(1.0,1.0),size).rgb);
    const float lM=luma(cM.rgb);
    const float lMin=min(lM,min(min(lNW,lNE),min(lSW,lSE)));
    const float lMax=max(lM,max(max(lNW,lNE),max(lSW,lSE)));
    float2 dir=float2(-((lNW+lNE)-(lSW+lSE)),(lNW+lSW)-(lNE+lSE));
    const float reduce=max((lNW+lNE+lSW+lSE)*(0.25/8.0),1.0/128.0);
    const float rcpMin=1.0/(min(abs(dir.x),abs(dir.y))+reduce);
    dir=clamp(dir*rcpMin,float2(-8.0),float2(8.0));
    const float3 a=0.5*(at(scene,s,q+dir*(1.0/3.0-0.5),size).rgb+at(scene,s,q+dir*(2.0/3.0-0.5),size).rgb);
    const float3 b=a*0.5+0.25*(at(scene,s,q+dir*-0.5,size).rgb+at(scene,s,q+dir*0.5,size).rgb);
    const float lB=luma(b);
    const float3 aa=(lB<lMin||lB>lMax)?a:b;
    return float4((lMax-lMin<max(0.0312,lMax*0.125))?cM.rgb:aa,cM.a);
}
// StretchRect (stretch_rect): the whole source over the whole destination.
fragment float4 stretch_fs(V v [[stage_in]],texture2d<float> source [[texture(0)]],sampler s [[sampler(0)]],constant float2& size [[buffer(0)]]){
    return source.sample(s,v.p.xy/size,level(0.0));
}
)";
MetalD3D9Device::MetalD3D9Device(unsigned w,unsigned h,unsigned scale):PcSoftD3D9Device(w,h),metal_(std::make_unique<Impl>(*this)){
    @autoreleasepool {
        require(scale>=1&&scale<=4,"Metal render scale must be between 1 and 4");
        static_assert(sizeof(Vertex)==180,"MSL packed vertex layout");
        auto& m=*metal_;m.device=MTLCreateSystemDefaultDevice();require(m.device!=nil,"No Metal GPU available");
        m.queue=(__bridge id<MTLCommandQueue>)metal_shared_queue();require(m.queue!=nil&&m.queue.device==m.device,"Cannot create Metal queue");
        NSError* error=nil;auto options=[MTLCompileOptions new];options.fastMathEnabled=NO;
        m.library=[m.device newLibraryWithSource:[NSString stringWithUTF8String:PcMetalSource] options:options error:&error];
        if(!m.library)metal_error(error,"Recovered MSL shader compilation");
        m.amplification=[m.device supportsVertexAmplificationCount:2];
        if(const char* v=std::getenv("OR2_METAL_FFP"))m.ffp_mode=std::strcmp(v,"generic")==0?FfpGeneric:std::strcmp(v,"sync")==0?FfpSync:FfpAsync;
        m.allocate_back(w*scale,h*scale);
        auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:1 height:1 mipmapped:NO];
        td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageShaderRead;
        m.dummy2d=[m.device newTextureWithDescriptor:td];td.textureType=MTLTextureTypeCube;m.dummycube=[m.device newTextureWithDescriptor:td];
        require(m.dummy2d&&m.dummycube,"Cannot allocate Metal placeholder textures");
        const unsigned char black[]{0,0,0,255};
        [m.dummy2d replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:black bytesPerRow:4];
        for(unsigned face=0;face<6;++face)[m.dummycube replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 slice:face withBytes:black bytesPerRow:4 bytesPerImage:4];
        m.visibility=[m.device newBufferWithLength:65536*8 options:MTLResourceStorageModeShared];
        m.spare_visibility=[m.device newBufferWithLength:65536*8 options:MTLResourceStorageModeShared];
        require(m.visibility&&m.spare_visibility,"Cannot allocate Metal visibility results");
        const float defaults[]{0,0,0,1};m.default_vertex=[m.device newBufferWithBytes:defaults length:sizeof defaults options:MTLResourceStorageModeShared];require(m.default_vertex!=nil,"Cannot allocate default vertex attributes");
        id<MTLLibrary> fxaa=[m.device newLibraryWithSource:[NSString stringWithUTF8String:FxaaSource] options:options error:&error];
        if(!fxaa)metal_error(error,"FXAA shader compilation");
        auto pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=[fxaa newFunctionWithName:@"fxaa_vs"];pd.fragmentFunction=[fxaa newFunctionWithName:@"fxaa_fs"];
        pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        m.fxaa=[m.device newRenderPipelineStateWithDescriptor:pd error:&error];if(!m.fxaa)metal_error(error,"FXAA pipeline");
        auto sd=[MTLSamplerDescriptor new];sd.minFilter=sd.magFilter=MTLSamplerMinMagFilterLinear;sd.sAddressMode=sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
        m.fxaa_sampler=[m.device newSamplerStateWithDescriptor:sd];require(m.fxaa_sampler!=nil,"Cannot create FXAA sampler");
        pd.fragmentFunction=[fxaa newFunctionWithName:@"stretch_fs"];
        m.stretch=[m.device newRenderPipelineStateWithDescriptor:pd error:&error];if(!m.stretch)metal_error(error,"StretchRect pipeline");
        sd.minFilter=sd.magFilter=MTLSamplerMinMagFilterNearest;
        m.stretch_point=[m.device newSamplerStateWithDescriptor:sd];require(m.stretch_point!=nil,"Cannot create StretchRect sampler");
    }
}
MetalD3D9Device::~MetalD3D9Device(){try{metal_->drain();metal_->finish();}catch(const std::exception& e){std::fprintf(stderr,"Metal shutdown: %s\n",e.what());}}
void MetalD3D9Device::begin_frame(){
    metal_->finish();metal_->gpu_ns=0;scene_finished_=false;
    // Stereo on or off: the back buffer's layers change (between frames, as a resize).
    {auto& m=*metal_;const unsigned layers=stereo_separation_>0&&m.amplification?2u:1u;
     if(layers!=m.layers)@autoreleasepool {m.drain();m.finish();m.layers=layers;m.allocate_back(unsigned(m.back.width),unsigned(m.back.height));if(layers<2)right_colour_.clear();}
     stereo_live_=m.layers>1;}
    msaa_live_=msaa_>1&&!count_only;
    if(msaa_live_)@autoreleasepool {metal_->ensure_msaa(msaa_);}
    if(!count_only)metal_->start();
}
void MetalD3D9Device::resize_back_buffer(unsigned w,unsigned h){
    if(w==presentation_width()&&h==presentation_height())return;
    @autoreleasepool {metal_->drain();metal_->finish();metal_->allocate_back(w,h);}
}
void MetalD3D9Device::finish_scene(){
    if(scene_finished_||count_only||colour_target_!=BackBuffer)return;
    if(msaa_live_){@autoreleasepool {metal_->resolve_msaa();}msaa_live_=false;++msaa_resolves;scene_finished_=true;return;}
    if(!fxaa_)return;
    scene_finished_=true;
    @autoreleasepool {
        auto& m=*metal_;m.finish_encoder();m.start();
        if(!m.scene_copy){
            auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:m.back.width height:m.back.height mipmapped:NO];
            if(m.layers>1){td.textureType=MTLTextureType2DArray;td.arrayLength=m.layers;}
            td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageShaderRead;
            m.scene_copy=[m.device newTextureWithDescriptor:td];require(m.scene_copy!=nil,"Cannot allocate the FXAA source");
            for(unsigned layer=0;layer<m.layers&&m.layers>1;++layer){
                m.fxaa_views[layer]=[m.scene_copy newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1) slices:NSMakeRange(layer,1)];
                require(m.fxaa_views[layer]!=nil,"Cannot view the FXAA source");
            }
        }
        auto blit=[m.command blitCommandEncoder];require(blit!=nil,"Cannot encode the FXAA copy");
        [blit copyFromTexture:m.back toTexture:m.scene_copy];[blit endEncoding];
        for(unsigned layer=0;layer<m.layers;++layer){   // one eye per pass
            auto p=[MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture=m.back;p.colorAttachments[0].slice=layer;p.colorAttachments[0].loadAction=MTLLoadActionDontCare;p.colorAttachments[0].storeAction=MTLStoreActionStore;
            auto e=[m.command renderCommandEncoderWithDescriptor:p];require(e!=nil,"Cannot encode FXAA");
            [e setRenderPipelineState:m.fxaa];[e setFragmentTexture:m.layers>1?m.fxaa_views[layer]:m.scene_copy atIndex:0];[e setFragmentSamplerState:m.fxaa_sampler atIndex:0];
            [e setViewport:MTLViewport{0,0,double(m.back.width),double(m.back.height),0,1}];
            [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
        }
        ++fxaa_passes;
    }
}
void MetalD3D9Device::end_frame(){
    if(msaa_live_){const auto target=colour_target_;colour_target_=BackBuffer;finish_scene();colour_target_=target;}   // no 2D layer this frame
    if(!pipelined_){readback();return;}   // the scene and its copy-out in one command buffer, one wait
    // Pipelined: this frame goes to the GPU with its copy-out, and the CPU
    // shows the previous one (normally finished while this one was recorded)
    // instead of waiting: one frame of latency, the CPU and GPU in parallel.
    auto& m=*metal_;m.finish_encoder();
    const auto w=presentation_width(),h=presentation_height();const std::size_t row=std::size_t(w)*4;
    if(!m.readback_staging||m.readback_staging.length<row*h*m.layers){
        m.readback_staging=[m.device newBufferWithLength:row*h*m.layers options:MTLResourceStorageModeShared];require(m.readback_staging!=nil,"Cannot allocate the readback buffer");}
    m.start();auto blit=[m.command blitCommandEncoder];require(blit!=nil,"Cannot encode the readback");
    const int present=gpu_frames_?int(m.copy_present(blit)):-1;   // GPU frames: no CPU readback
    if(present<0)m.copy_back(blit,m.readback_staging,w,h);
    [blit endEncoding];
    id<MTLCommandBuffer> frame=m.command;const unsigned samples=m.samples;m.command=nil;m.samples=0;
    [frame commit];
    bool done=false;
    if(m.inflight)m.drain();
    else{   // the first frame: shown at once (and again next frame, while the pipeline fills)
        [frame waitUntilCompleted];m.completed(frame,m.visibility,samples,m.query_ranges);
        if(present>=0)m.shown_present=present;else m.copy_out(m.readback_staging);done=true;
    }
    m.inflight=frame;m.inflight_samples=samples;m.inflight_done=done;m.inflight_present=present;
    std::swap(m.visibility,m.spare_visibility);std::swap(m.query_ranges,m.spare_query_ranges);std::swap(m.readback_staging,m.spare_staging);std::swap(m.vertex_buffers,m.spare_vertex_buffers);
}
void MetalD3D9Device::set_pipelined(bool on){
    if(!on&&pipelined_)@autoreleasepool {metal_->drain();}
    pipelined_=on;
}
void MetalD3D9Device::readback(){
    metal_->drain();metal_->finish_encoder();
    auto& destination=colour_target_==BackBuffer?colour_:back_colour_;
    const auto w=presentation_width(),h=presentation_height();
    destination.resize(std::size_t(w)*h*4);
    auto& m=*metal_;const std::size_t row=std::size_t(w)*4;
    if(!m.readback_staging||m.readback_staging.length<row*h*m.layers){
        m.readback_staging=[m.device newBufferWithLength:row*h*m.layers options:MTLResourceStorageModeShared];require(m.readback_staging!=nil,"Cannot allocate the readback buffer");}
    m.start();auto blit=[m.command blitCommandEncoder];require(blit!=nil,"Cannot encode the readback");
    m.copy_back(blit,m.readback_staging,w,h);
    [blit endEncoding];m.finish();
    std::memcpy(destination.data(),m.readback_staging.contents,row*h);
    if(m.layers>1){right_colour_.resize(row*h);std::memcpy(right_colour_.data(),static_cast<const char*>(m.readback_staging.contents)+row*h,row*h);}
}
void* metal_shared_queue(){
    static id<MTLCommandQueue> queue=[]{id<MTLDevice> d=MTLCreateSystemDefaultDevice();return d?[d newCommandQueue]:nil;}();
    return (__bridge void*)queue;
}
void* MetalD3D9Device::gpu_frame(unsigned eye)const{
    const auto& m=*metal_;if(m.shown_present<0)return nullptr;
    return (__bridge void*)m.present_eyes[std::size_t(m.shown_present)][eye?1:0];
}
unsigned MetalD3D9Device::presentation_width()const{return unsigned(metal_->back.width);}
unsigned MetalD3D9Device::presentation_height()const{return unsigned(metal_->back.height);}
std::uint64_t MetalD3D9Device::gpu_nanoseconds()const{return metal_->gpu_ns;}
std::string MetalD3D9Device::device_name()const{return metal_->device.name.UTF8String;}
// The pass ends only when a target really changes (the PC sets the same one again and again).
void MetalD3D9Device::set_render_target(std::uint32_t i,std::uint32_t h){if(h!=colour_target_)metal_->finish_encoder();PcSoftD3D9Device::set_render_target(i,h);}
void MetalD3D9Device::set_depth_stencil_surface(std::uint32_t h){if(h!=depth_target_)metal_->finish_encoder();PcSoftD3D9Device::set_depth_stencil_surface(h);}
void MetalD3D9Device::set_stereo_eye(int eye){
    eye=eye<0?-1:eye>0?1:0;
    if(eye!=stereo_eye_){metal_->finish_encoder();stereo_eye_=eye;}
}
// StretchRect on the GPU: a pass drawing the source over the destination
// (linear or nearest). The back buffer as source is the scene: resolved first
// while it is multisampled, its left eye when it is layered (stereo), or the
// eye set_stereo_eye picked.
bool MetalD3D9Device::stretch_rect(std::uint32_t source,std::uint32_t dest,std::uint32_t filter){
    if(source==dest||dest==BackBuffer)return false;
    auto& m=*metal_;
    auto surface=[&](std::uint32_t h,__strong id<MTLTexture>& t,unsigned& level,unsigned& slice)->bool{
        auto* o=object(h);if(!o||o->kind!=6||!o->parent)return false;
        t=m.texture(o->parent);level=o->level;slice=o->face;return t!=nil;};
    id<MTLTexture> dst=nil;unsigned dst_level=0,dst_slice=0;
    if(!surface(dest,dst,dst_level,dst_slice))return false;
    m.finish_encoder();m.start();
    id<MTLTexture> src=nil;unsigned src_level=0,src_slice=0;
    const unsigned eye=m.layers>1&&stereo_eye_>0?1u:0u;   // set_stereo_eye: the right eye's layer
    if(source==BackBuffer){
        if(msaa_live_&&m.msaa_colour){
            if(!m.scene_resolve||m.scene_resolve.width!=m.back.width||m.scene_resolve.height!=m.back.height){
                auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:m.back.width height:m.back.height mipmapped:NO];
                td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
                m.scene_resolve=[m.device newTextureWithDescriptor:td];require(m.scene_resolve!=nil,"Cannot allocate the scene resolve");
            }
            auto p=[MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture=m.msaa_colour;p.colorAttachments[0].slice=eye;p.colorAttachments[0].loadAction=MTLLoadActionLoad;
            p.colorAttachments[0].storeAction=MTLStoreActionStoreAndMultisampleResolve;p.colorAttachments[0].resolveTexture=m.scene_resolve;
            auto e=[m.command renderCommandEncoderWithDescriptor:p];require(e!=nil,"Cannot encode the scene resolve");[e endEncoding];
            src=m.scene_resolve;
        }else{src=m.back;src_slice=eye;}
    }else if(!surface(source,src,src_level,src_slice))return false;
    if(src.textureType!=MTLTextureType2D||src_level||src_slice)
        src=[src newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(src_level,1) slices:NSMakeRange(src_slice,1)];
    require(src!=nil,"StretchRect source view");
    auto p=[MTLRenderPassDescriptor renderPassDescriptor];
    p.colorAttachments[0].texture=dst;p.colorAttachments[0].level=dst_level;p.colorAttachments[0].slice=dst_slice;
    p.colorAttachments[0].loadAction=MTLLoadActionDontCare;p.colorAttachments[0].storeAction=MTLStoreActionStore;
    auto e=[m.command renderCommandEncoderWithDescriptor:p];require(e!=nil,"Cannot encode StretchRect");
    const float size[2]{float(std::max<NSUInteger>(1,dst.width>>dst_level)),float(std::max<NSUInteger>(1,dst.height>>dst_level))};
    [e setRenderPipelineState:m.stretch];[e setFragmentTexture:src atIndex:0];
    [e setFragmentSamplerState:filter==2u?m.fxaa_sampler:m.stretch_point atIndex:0];[e setFragmentBytes:size length:sizeof size atIndex:0];
    [e setViewport:MTLViewport{0,0,size[0],size[1],0,1}];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
    return true;
}
// Occlusion queries over the per-draw visibility counts (Impl::QueryRange):
// GetData answers S_FALSE until the command buffer holding the END is done
// (one frame later with pipelined frames, as on a PC GPU).
void MetalD3D9Device::query_issue(std::uint32_t h,std::uint32_t flags){
    if(h<QueryBase||h-QueryBase>=queries_.size())return;
    auto& m=*metal_;auto& q=queries_[h-QueryBase];
    if(flags&2u){m.start();m.query_open[h]={m.command_generation,m.samples};q.active=true;}
    if((flags&1u)&&q.active){
        q.active=false;
        const auto it=m.query_open.find(h);
        // A BEGIN in an earlier command buffer (a mid-frame finish): this buffer's draws only.
        const unsigned first=it!=m.query_open.end()&&it->second.first==m.command_generation?it->second.second:0u;
        if(it!=m.query_open.end())m.query_open.erase(it);
        if(!m.command){q.result=0;return;}   // nothing recorded since: no samples
        // Port: the samples as a 2560x1440 target with 4x MSAA would count them (the
        // owner's reference, the modded Windows game). The sun pass (40CBC0) divides
        // them by a fixed calibration (73E2AC 11500, raised only above 0.22), so on the
        // PC the flare's strength depends on resolution and anti-aliasing; this makes
        // it the reference's on every device and setting. Both eyes count when layered.
        const double per_pixel=colour_target_==BackBuffer&&msaa_live_?double(msaa_):1.0;
        const double pixels=double(std::max(1u,presentation_width()))*double(std::max(1u,presentation_height()));
        const double scale=4.0/per_pixel*(2560.0*1440.0)/pixels/(m.layered?double(m.layers):1.0);
        m.query_ranges.push_back({h,first,m.samples,scale});++m.query_pending[h];
    }
}
std::uint32_t MetalD3D9Device::query_get_data(std::uint32_t h,std::uint32_t& samples){
    samples=0;
    if(h<QueryBase||h-QueryBase>=queries_.size())return 1u;
    const auto& q=queries_[h-QueryBase];
    if(q.active||metal_->query_pending.count(h))return 1u;
    samples=q.result;return 0u;
}
void MetalD3D9Device::release(std::uint32_t h){
    PcSoftD3D9Device::release(h);
    // Most releases only drop a reference (set_stream_source, set_texture): nothing was destroyed.
    if(object(h))return;
    // Releasing a surface can recursively release its parent texture.
    for(auto it=metal_->textures.begin();it!=metal_->textures.end();)if(!object(it->first))it=metal_->textures.erase(it);else ++it;
    for(auto it=metal_->depths.begin();it!=metal_->depths.end();)if(!object(it->first))it=metal_->depths.erase(it);else ++it;
    for(auto it=metal_->buffers.begin();it!=metal_->buffers.end();)if(!object(it->first))it=metal_->buffers.erase(it);else ++it;
}
std::uint8_t* MetalD3D9Device::lock(std::uint32_t h,std::uint32_t offset,std::uint32_t size,std::uint32_t flags){
    auto* p=PcSoftD3D9Device::lock(h,offset,size,flags);
    if(p){if(buffer_dirty_.size()<=h)buffer_dirty_.resize(h+1);buffer_dirty_[h]=true;}return p;
}
void MetalD3D9Device::unlock_rect(std::uint32_t h){
    metal_->drain();metal_->finish();PcSoftD3D9Device::unlock_rect(h);auto* s=object(h);if(!s||!s->parent)return;
    auto found=metal_->textures.find(s->parent);if(found==metal_->textures.end())return;
    const auto* parent=object(s->parent);const auto& data=parent->rgba[s->face][s->level];
    [found->second replaceRegion:MTLRegionMake2D(0,0,s->width,s->height) mipmapLevel:s->level slice:s->face withBytes:data.data() bytesPerRow:std::size_t(s->width)*4 bytesPerImage:data.size()];
}
void MetalD3D9Device::clear(std::uint32_t flags,std::uint32_t c,float z,std::uint32_t s){
    if(count_only)return;auto& m=*metal_;
    // Ends the pass; the clear opens the next one (merged with a clear already waiting).
    m.end_encoder();m.clear_flags|=flags&7u;
    if(flags&1u)m.clear_colour=c;if(flags&2u)m.clear_z=z;if(flags&4u)m.clear_stencil=s;
}
void MetalD3D9Device::raster(const Vertex* v,unsigned n){
    if(n<3)return;
    batch_rhw_=pretransformed_;
    auto push=[&](const Vertex& input){auto o=input;
        if(pretransformed_){const float w=1.f/o.pos[3];o.pos[0]=(2*(o.pos[0]+.5f-soft_viewport_.x)/soft_viewport_.w-1)*w;o.pos[1]=(1-2*(o.pos[1]+.5f-soft_viewport_.y)/soft_viewport_.h)*w;o.pos[2]*=w;o.pos[3]=w;}
        else{o.pos[0]+=o.pos[3]/soft_viewport_.w;o.pos[1]-=o.pos[3]/soft_viewport_.h;}
        batch_.push_back(o);};
    for(unsigned i=1;i+1<n;++i){push(v[0]);push(v[i]);push(v[i+1]);++triangles_drawn;}
}
void MetalD3D9Device::draw_primitive_up(std::uint32_t t,std::uint32_t n,const void* d,std::uint32_t s){
    native_vertices_=false;batch_.clear();
    if(t!=1){PcSoftD3D9Device::draw_primitive_up(t,n,d,s);flush();return;}
    if(!d||!n||count_only)return;
    // Retail 41A6F0 / 41AE40: XYZ + D3DCOLOR point sprites (FVF 0x42).
    // Expand in clip space so the existing Metal pixel path preserves all
    // material, alpha-test, blending and depth state. No effect is discarded.
    require(fvf_==0x42&&s>=16&&vertex_shader==0,"Unsupported point sprite vertex layout");
    const auto matrix=transform_wvp();
    auto transform=[](const float* v,const std::array<float,16>& m){
        std::array<float,4> out{};
        for(unsigned k=0;k<4;++k)out[k]=v[0]*m[k]+v[1]*m[4+k]+v[2]*m[8+k]+v[3]*m[12+k];
        return out;
    };
    const auto* bytes=static_cast<const std::uint8_t*>(d);
    const float minimum=render[155]?f(render[155]):1.f;
    const float maximum=render[166]?f(render[166]):64.f;
    require(std::isfinite(minimum)&&std::isfinite(maximum)&&minimum>=0&&maximum>=minimum,"Invalid point sprite size limits");
    pretransformed_=false;
    for(unsigned i=0;i<n;++i){
        float xyz[4]{0,0,0,1};std::uint32_t c;
        std::memcpy(xyz,bytes+std::size_t(i)*s,12);std::memcpy(&c,bytes+std::size_t(i)*s+12,4);
        const auto clip=transform(xyz,matrix);
        // D3D points clip their centre before expanding, including z and w.
        if(!(clip[3]>0)||clip[2]<0||clip[2]>clip[3]||std::abs(clip[0])>clip[3]||std::abs(clip[1])>clip[3])continue;
        float size=render[154]?f(render[154]):1.f;
        if(render[157]){
            const auto world=transform(xyz,transform_world);
            const auto eye=transform(world.data(),transform_view);
            const float distance=std::sqrt(eye[0]*eye[0]+eye[1]*eye[1]+eye[2]*eye[2]);
            const float denominator=f(render[158])+f(render[159])*distance+f(render[160])*distance*distance;
            size=denominator>0?soft_viewport_.h*size/std::sqrt(denominator):maximum;
        }
        require(std::isfinite(size),"Invalid point sprite size");size=std::clamp(size,minimum,maximum);
        const float dx=size*clip[3]/soft_viewport_.w,dy=size*clip[3]/soft_viewport_.h;
        Vertex q[4]{};
        for(unsigned k=0;k<4;++k){
            auto& v=q[k];std::copy(clip.begin(),clip.end(),v.pos);
            v.pos[0]+=(k&1)?dx:-dx;v.pos[1]+=(k&2)?-dy:dy;
            v.d[0]={float((c>>16)&255)/255.f,float((c>>8)&255)/255.f,float(c&255)/255.f,float(c>>24)/255.f};v.fog=1;
            for(auto& uv:v.t)uv=render[156]?platform::pc_shader::V4{float(k&1),float((k>>1)&1),0,1}:platform::pc_shader::V4{0,0,0,1};
        }
        const Vertex a[]{q[0],q[1],q[2]},b[]{q[2],q[1],q[3]};raster(a,3);raster(b,3);
    }
    const auto cull=render[RS_CULLMODE];render[RS_CULLMODE]=CULL_NONE;
    flush();render[RS_CULLMODE]=cull;
}
bool MetalD3D9Device::screen_fade(){
    // Clip space after raster(): the batch spans the viewport edge to edge.
    if(batch_.empty()||soft_viewport_.x>0||soft_viewport_.y>0||soft_viewport_.w<float(back_width_)||soft_viewport_.h<float(back_height_))return false;
    float x0=2,x1=-2,y0=2,y1=-2,u0=2,u1=-1,v0=2,v1=-1;
    for(const auto& v:batch_){
        const float x=v.pos[0]/v.pos[3],y=v.pos[1]/v.pos[3];
        x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
        u0=std::min(u0,v.t[0].x);u1=std::max(u1,v.t[0].x);v0=std::min(v0,v.t[0].y);v1=std::max(v1,v.t[0].y);
    }
    constexpr float edge=0.98f;   // the PC's half-texel offset keeps the corners just inside +-1
    if(!(x0<=-edge&&x1>=edge&&y0<=-edge&&y1>=edge))return false;
    if(!textures[0])return true;
    // A textured fade samples one flat patch of a sprite sheet (the ending's 512x256
    // sheet); 4:3 artwork drawn full screen samples a detailed picture.
    const auto* o=object(textures[0]);
    if(!o||o->rgba.empty()||o->rgba[0].empty()||o->texture.faces.empty()||o->texture.faces[0].empty())return false;
    const std::uint32_t w=o->texture.faces[0][0].width,h=o->texture.faces[0][0].height;   // the texels' size (not a replacement's logical one)
    if(!w||!h)return false;
    const auto& px=o->rgba[0][0];if(px.size()<std::size_t(w)*h*4)return false;
    auto texel=[](float t,std::uint32_t n){return std::uint32_t(std::clamp(t*float(n),0.f,float(n-1)));};
    const std::uint32_t tx0=texel(u0,w),tx1=texel(u1,w),ty0=texel(v0,h),ty1=texel(v1,h);
    if(std::size_t(tx1-tx0+1)*(ty1-ty0+1)>65536u)return false;   // a large region is a picture, not a fade
    const std::uint8_t* first=&px[(std::size_t(ty0)*w+tx0)*4];
    for(std::uint32_t y=ty0;y<=ty1;++y)for(std::uint32_t x=tx0;x<=tx1;++x)
        if(std::memcmp(&px[(std::size_t(y)*w+x)*4],first,4)!=0)return false;
    return true;
}
void MetalD3D9Device::flush(){
    if((batch_.empty()&&!native_vertices_)||count_only)return;
    @autoreleasepool {
        auto& m=*metal_;const auto* ps=object(pixel_shader);
        unsigned kind=0;
        if(ps&&ps->kind==4){
            require(ps->program.version==0xffff0101u||ps->program.version==0xffff0104u,"Unsupported Metal pixel shader version");
            require(ps->program.blobs.size()<=16,"Metal pixel program exceeds retained 16-blob layout");kind=ps->program.version==0xffff0101u?1:2;
        }
        const bool has_depth=depth_target_!=0;
        const bool separate=render[RS_SEPARATEALPHABLENDENABLE]!=0;
        auto e=m.render_encoder();const bool layered=m.layered;   // both eyes (stereo back buffer)
        std::string key;auto word=[&](unsigned v){key.append(reinterpret_cast<const char*>(&v),sizeof v);};
        const unsigned samples=colour_target_==BackBuffer&&msaa_live_?msaa_:1;
        word(kind);word(has_depth);word(samples);word(layered);
        word(native_vertices_);
        const auto* vs=object(vertex_shader);const auto* decl=object(declaration);
        if(native_vertices_){
            word(unsigned(vs->program.blobs.size()));for(auto blob:vs->program.blobs)word(blob);
            key.append(reinterpret_cast<const char*>(decl->elements.data()),decl->elements.size()*sizeof(platform::PcVertexElement));
            for(const auto& stream:streams)word(stream.stride);
        }
        // Specialise the retained instruction list so Metal can eliminate
        // unreachable interpreter branches, while material state stays dynamic.
        if(kind){word(unsigned(ps->program.blobs.size()));for(auto blob:ps->program.blobs)word(blob);}
        for(auto state:{RS_COLORWRITEENABLE,RS_ALPHABLENDENABLE,RS_SRCBLEND,RS_DESTBLEND,RS_BLENDOP,RS_SEPARATEALPHABLENDENABLE,RS_SRCBLENDALPHA,RS_DESTBLENDALPHA,RS_BLENDOPALPHA})word(render[state]);
        Uniforms u{};std::memcpy(u.pc,ps_constants.data(),sizeof u.pc);
        for(auto& row:u.program)for(auto& value:row.v)value=-1;
        if(kind)for(unsigned i=0;i<ps->program.blobs.size();++i)u.program[i/4].v[i%4]=int(ps->program.blobs[i]);
        if(kind)u.info.v[0]=int(ps->program.blobs.size());
        for(unsigned s=0;s<8;++s){const auto* t=object(textures[s]);u.kinds[s/4].v[s%4]=t&&t->kind==5&&t->texture.cube;
            for(unsigned k=0;k<4;++k)u.bump[s].v[k]=f(stage[s][7+k]);}
        for(unsigned s=0;s<4;++s){const auto& t=stage[s];
            u.lum[s/2].v[(s%2)*2]=f(t[22]);u.lum[s/2].v[(s%2)*2+1]=f(t[23]);
            u.op[s]={{int(t[1]),int(t[2]),int(t[3]),int(t[4])}};
            u.arg[s]={{int(t[5]),int(t[6]),int(t[26]),int(t[27])}};
            const auto* tx=object(textures[s]);u.misc[s]={{int(t[28]),int(t[11]&3),tx&&tx->kind==5&&!tx->texture.cube,tx&&tx->kind==5&&tx->texture.cube}};
            u.konst[s]=colour(t[32]);
        }
        u.tfactor=colour(render[RS_TEXTUREFACTOR]);u.flags.v[0]=render[RS_SPECULARENABLE]!=0;
        u.fog_colour=colour(render[RS_FOGCOLOR]);u.fog_colour.v[3]=f(render[RS_FOGEND]);
        u.fog_params={{render[RS_FOGENABLE]?float(render[RS_FOGTABLEMODE]?render[RS_FOGTABLEMODE]:4):0,f(render[RS_FOGSTART]),f(render[RS_FOGDENSITY]),1}};
        u.alpha_test={{render[RS_ALPHATESTENABLE]?1.f:0.f,float(render[RS_ALPHAREF]&255),float(render[RS_ALPHAFUNC]),0}};
        // Fixed-function draws: the cascade state as function constants (the
        // values the generic pipeline reads from u), compiled in the background
        // and used once ready; until then, the generic pipeline (port optimisation:
        // the generic one interprets every stage per fragment, which made the
        // full-screen road spray of wet courses GPU-bound at high resolutions).
        std::array<std::int32_t,52> ffp_state{};
        if(!kind){
            for(unsigned s=0;s<4;++s)for(unsigned k=0;k<4;++k){ffp_state[s*4+k]=u.op[s].v[k];ffp_state[16+s*4+k]=u.arg[s].v[k];ffp_state[32+s*4+k]=u.misc[s].v[k];}
            ffp_state[48]=u.flags.v[0];ffp_state[49]=u.alpha_test.v[0]!=0;ffp_state[50]=int(u.alpha_test.v[2]);ffp_state[51]=int(u.fog_params.v[0]);
        }
        // The pipeline's descriptor; fixed: without its fragment program (the caller sets the specialised one).
        auto describe=[&](bool fixed)->MTLRenderPipelineDescriptor*{
            auto d=[MTLRenderPipelineDescriptor new];
            if(!native_vertices_)d.vertexFunction=[m.library newFunctionWithName:@"pc_vertex"];
            else{
                std::int32_t instructions[32];std::fill(std::begin(instructions),std::end(instructions),-1);
                for(unsigned i=0;i<vs->program.blobs.size();++i)instructions[i]=int(vs->program.blobs[i]);
                // One specialisation per program (the generic and the specialised fixed-function pipelines share it).
                auto& function=m.vertex_functions[std::string(reinterpret_cast<const char*>(instructions),sizeof instructions)];
                NSError* error=nil;
                if(!function){
                    auto constants=[MTLFunctionConstantValues new];
                    for(unsigned i=0;i<8;++i)[constants setConstantValue:instructions+4*i type:MTLDataTypeInt4 atIndex:16+i];
                    function=[m.library newFunctionWithName:@"pc_native_vertex" constantValues:constants error:&error];
                }
                d.vertexFunction=function;
                if(!d.vertexFunction)metal_error(error,"Metal vertex program specialization");
                auto layout=[MTLVertexDescriptor vertexDescriptor];
                for(unsigned reg=0;reg<16;++reg){layout.attributes[reg].format=MTLVertexFormatFloat4;layout.attributes[reg].bufferIndex=4;}
                layout.layouts[4].stride=16;layout.layouts[4].stepFunction=MTLVertexStepFunctionConstant;layout.layouts[4].stepRate=0;
                const MTLVertexFormat formats[]{MTLVertexFormatFloat,MTLVertexFormatFloat2,MTLVertexFormatFloat3,MTLVertexFormatFloat4,MTLVertexFormatUChar4Normalized_BGRA,MTLVertexFormatUChar4};
                for(const auto& field:decl->elements){
                    if(field.stream==platform::PcDeclEndStream)break;
                    for(const auto& input:vs->program.inputs)if(field.usage==input.usage&&field.usage_index==input.usage_index){
                        require(input.reg<16&&field.type<6&&field.stream<4,"Unsupported Metal vertex declaration");
                        auto a=layout.attributes[input.reg];a.format=formats[field.type];a.offset=field.offset;a.bufferIndex=field.stream;
                        layout.layouts[field.stream].stride=streams[field.stream].stride;layout.layouts[field.stream].stepFunction=MTLVertexStepFunctionPerVertex;
                    }
                }d.vertexDescriptor=layout;
            }
            if(fixed){}
            else if(!kind){
                NSError* error=nil;d.fragmentFunction=[m.library newFunctionWithName:@"pc_ffp" constantValues:ffp_constants(false,{}) error:&error];
                if(!d.fragmentFunction)metal_error(error,"Metal fixed-function program");
            }else{
                auto constants=[MTLFunctionConstantValues new];
                std::int32_t instructions[16];std::fill(std::begin(instructions),std::end(instructions),-1);
                for(unsigned i=0;i<ps->program.blobs.size();++i)instructions[i]=int(ps->program.blobs[i]);
                for(unsigned i=0;i<4;++i)[constants setConstantValue:instructions+4*i type:MTLDataTypeInt4 atIndex:i];
                NSError* error=nil;
                d.fragmentFunction=[m.library newFunctionWithName:kind==1?@"pc_ps11":@"pc_ps14" constantValues:constants error:&error];
                if(!d.fragmentFunction)metal_error(error,"Metal pixel program specialization");
            }
            auto c=d.colorAttachments[0];c.pixelFormat=MTLPixelFormatRGBA8Unorm;
            const auto mask=render[RS_COLORWRITEENABLE];c.writeMask=MTLColorWriteMaskNone;
            if(mask&1)c.writeMask|=MTLColorWriteMaskRed;if(mask&2)c.writeMask|=MTLColorWriteMaskGreen;if(mask&4)c.writeMask|=MTLColorWriteMaskBlue;if(mask&8)c.writeMask|=MTLColorWriteMaskAlpha;
            c.blendingEnabled=render[RS_ALPHABLENDENABLE]!=0;
            if(c.blendingEnabled){
                c.sourceRGBBlendFactor=blend(render[RS_SRCBLEND]);c.destinationRGBBlendFactor=blend(render[RS_DESTBLEND]);c.rgbBlendOperation=blend_op(render[RS_BLENDOP]);
                c.sourceAlphaBlendFactor=blend(render[separate?RS_SRCBLENDALPHA:RS_SRCBLEND]);c.destinationAlphaBlendFactor=blend(render[separate?RS_DESTBLENDALPHA:RS_DESTBLEND]);c.alphaBlendOperation=blend_op(render[separate?RS_BLENDOPALPHA:RS_BLENDOP]);
            }
            if(has_depth){d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;}
            d.rasterSampleCount=samples;
            if(layered){d.maxVertexAmplificationCount=2;d.inputPrimitiveTopology=MTLPrimitiveTopologyClassTriangle;}
            return d;
        };
        auto pipeline=m.pipelines.end();
        if(!kind&&m.ffp_mode!=FfpGeneric){
            m.collect_ffp();
            std::string fixed=key;fixed.push_back('F');fixed.append(reinterpret_cast<const char*>(ffp_state.data()),sizeof ffp_state);
            pipeline=m.pipelines.find(fixed);
            if(pipeline==m.pipelines.end()&&m.ffp_requested.size()<FfpLimit&&m.ffp_requested.insert(fixed).second){
                auto d=describe(true);
                auto constants=ffp_constants(true,ffp_state);
                if(m.ffp_mode==FfpSync){
                    NSError* error=nil;d.fragmentFunction=[m.library newFunctionWithName:@"pc_ffp" constantValues:constants error:&error];
                    if(!d.fragmentFunction)metal_error(error,"Metal fixed-function specialisation");
                    auto p=[m.device newRenderPipelineStateWithDescriptor:d error:&error];if(!p)metal_error(error,"Metal D3D9 fixed-function pipeline");
                    pipeline=m.pipelines.emplace(fixed,p).first;++m.ffp_built;
                }else{
                    auto shared=m.ffp_async;id<MTLDevice> device=m.device;
                    [m.library newFunctionWithName:@"pc_ffp" constantValues:constants completionHandler:^(id<MTLFunction> function,NSError*){
                        if(!function){shared->done(fixed,nil);return;}
                        d.fragmentFunction=function;
                        [device newRenderPipelineStateWithDescriptor:d completionHandler:^(id<MTLRenderPipelineState> state,NSError*){shared->done(fixed,state);}];
                    }];
                }
            }
        }
        if(pipeline==m.pipelines.end())pipeline=m.pipelines.find(key);
        if(pipeline==m.pipelines.end()){
            auto d=describe(false);
            NSError* error=nil;auto p=[m.device newRenderPipelineStateWithDescriptor:d error:&error];if(!p)metal_error(error,"Metal D3D9 pipeline");pipeline=m.pipelines.emplace(key,p).first;
        }
        key.clear();word(has_depth);
        for(unsigned state:{7u,14u,23u,52u,53u,54u,55u,56u,58u,59u})word(render[state]);
        auto depth_state=m.depth_states.find(key);
        if(depth_state==m.depth_states.end()){
            auto d=[MTLDepthStencilDescriptor new];d.depthCompareFunction=render[RS_ZENABLE]&&has_depth?compare(render[RS_ZFUNC]):MTLCompareFunctionAlways;
            d.depthWriteEnabled=render[RS_ZENABLE]&&render[RS_ZWRITEENABLE]&&has_depth;
            if(render[52]){
                require(has_depth,"Stencil enabled without a depth/stencil attachment");
                auto s=[MTLStencilDescriptor new];s.stencilCompareFunction=compare(render[56]);s.stencilFailureOperation=stencil(render[53]);s.depthFailureOperation=stencil(render[54]);s.depthStencilPassOperation=stencil(render[55]);s.readMask=render[58]&255;s.writeMask=render[59]&255;d.frontFaceStencil=s;d.backFaceStencil=s;
            }
            auto ds=[m.device newDepthStencilStateWithDescriptor:d];require(ds!=nil,"Cannot create Metal depth/stencil state");depth_state=m.depth_states.emplace(key,ds).first;
        }
        [e setRenderPipelineState:pipeline->second];[e setDepthStencilState:depth_state->second];[e setStencilReferenceValue:render[57]&255];
        {const auto b=colour(render[0xc1]);[e setBlendColorRed:b.v[0] green:b.v[1] blue:b.v[2] alpha:b.v[3]];}   // D3DRS_BLENDFACTOR
        [e setFrontFacingWinding:MTLWindingClockwise];[e setCullMode:render[RS_CULLMODE]==CULL_NONE?MTLCullModeNone:render[RS_CULLMODE]==CULL_CCW?MTLCullModeBack:MTLCullModeFront];
        [e setTriangleFillMode:render[RS_FILLMODE]==2?MTLTriangleFillModeLines:MTLTriangleFillModeFill];
        require(render[RS_FILLMODE]==2||render[RS_FILLMODE]==3,"Metal point fill mode is not implemented");
        // The logical PC target stretched to the back buffer's pixels; with the
        // UI rectangle, pretransformed (2D) draws keep a centred 4:3 area.
        const bool back=colour_target_==BackBuffer;
        double sx=back?double(presentation_width())/back_width_:1,sy=back?double(presentation_height())/back_height_:1,ox=0;
        if(back&&ui_rect_&&(scene_in_ui_rect_||(batch_rhw_&&!native_vertices_&&!rhw_full_canvas_&&!screen_fade()))){const double w=double(presentation_height())*4/3;ox=std::floor((presentation_width()-w)/2);sx=w/back_width_;}
        MTLViewport viewport{ox+soft_viewport_.x*sx,soft_viewport_.y*sy,soft_viewport_.w*sx,soft_viewport_.h*sy,soft_viewport_.min_z,soft_viewport_.max_z};[e setViewport:viewport];
        const unsigned left=unsigned(std::max(0.f,soft_viewport_.x)),top=unsigned(std::max(0.f,soft_viewport_.y));
        require(left<width_&&top<height_&&soft_viewport_.w>0&&soft_viewport_.h>0,"Metal viewport outside target");
        const unsigned right=left+std::min(width_-left,unsigned(soft_viewport_.w)),bottom=top+std::min(height_-top,unsigned(soft_viewport_.h));
        const NSUInteger tw=back?presentation_width():width_,th=back?presentation_height():height_;
        const auto px=[&](unsigned v){return std::min(tw,NSUInteger(std::lround(ox+v*sx)));};const auto py=[&](unsigned v){return std::min(th,NSUInteger(std::lround(v*sy)));};
        [e setScissorRect:MTLScissorRect{px(left),py(top),px(right)-px(left),py(bottom)-py(top)}];
        // Constant D3D depth bias is an absolute offset, unlike Metal's
        // format-dependent depthBias. Fold it into clip z; slope is native.
        // Only while the depth test is on: D3D9 biases the depth after clipping,
        // so it never removes a draw, but folded into clip z it moves geometry
        // at z 0 or 1 out of the clip volume. A bias left set by an earlier draw
        // (-1.06e-5 from road / shadow draws) clipped the glare's full-screen
        // composite (z 0, depth off) for whole stretches of frames: the bloom
        // switched on and off.
        const float depth_bias=render[RS_ZENABLE]&&has_depth?f(render[RS_DEPTHBIAS]):0.f;
        for(auto& v:batch_)v.pos[2]+=depth_bias*v.pos[3];
        [e setDepthBias:0 slopeScale:f(render[RS_SLOPESCALEDEPTHBIAS]) clamp:0];
        for(unsigned s=0;s<6;++s){
            auto* o=object(textures[s]);auto texture=m.texture(textures[s]);const bool cube=o&&o->kind==5&&o->texture.cube;
            [e setFragmentTexture:texture&&!cube?texture:m.dummy2d atIndex:s];[e setFragmentTexture:texture&&cube?texture:m.dummycube atIndex:6+s];
            const auto& state=sampler[s];std::string sk(reinterpret_cast<const char*>(state.data()),state.size()*sizeof(state[0]));
            const unsigned levels=o&&o->kind==5?o->texture.levels:1;sk.append(reinterpret_cast<const char*>(&levels),sizeof levels);
            auto sample=m.samplers.find(sk);
            if(sample==m.samplers.end()){
                auto d=[MTLSamplerDescriptor new];d.sAddressMode=address(state[SAMP_ADDRESSU]);d.tAddressMode=address(state[SAMP_ADDRESSV]);d.rAddressMode=address(state[SAMP_ADDRESSW]);
                d.minFilter=state[SAMP_MINFILTER]==1?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
                d.magFilter=state[SAMP_MAGFILTER]==1?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
                d.mipFilter=state[SAMP_MIPFILTER]==0?MTLSamplerMipFilterNotMipmapped:state[SAMP_MIPFILTER]==1?MTLSamplerMipFilterNearest:MTLSamplerMipFilterLinear;
                d.lodMinClamp=float(std::min(state[SAMP_MAXMIPLEVEL],levels-1));
                d.maxAnisotropy=std::clamp(state[SAMP_MAXANISOTROPY],1u,16u);
                if(state[SAMP_ADDRESSU]==4||state[SAMP_ADDRESSV]==4||state[SAMP_ADDRESSW]==4){
                    switch(state[SAMP_BORDERCOLOR]){case 0:d.borderColor=MTLSamplerBorderColorTransparentBlack;break;case 0xff000000u:d.borderColor=MTLSamplerBorderColorOpaqueBlack;break;case 0xffffffffu:d.borderColor=MTLSamplerBorderColorOpaqueWhite;break;default:throw std::runtime_error("Custom D3D border colour requires shader emulation");}
                }
                auto value=[m.device newSamplerStateWithDescriptor:d];require(value!=nil,"Cannot create Metal sampler");sample=m.samplers.emplace(sk,value).first;
            }
            [e setFragmentSamplerState:sample->second atIndex:s];u.lod_bias[s]=f(state[SAMP_MIPMAPLODBIAS]);
        }
        // Per-eye shift (pc_stereo in the vertex programs): left, right, convergence,
        // and the screen-space draws' shift (set_stereo_screen_depth): a 3D point at
        // clip w D moves max(1 - C / D, -1) NDC of the full canvas per unit of
        // separation; a 2D draw in the narrower 4:3 rectangle needs it scaled up.
        float screen_shift=0;
        if(layered&&stereo_screen_depth_>0&&viewport.width>0)
            screen_shift=std::max(1.f-stereo_convergence_/stereo_screen_depth_,-1.f)*float(double(presentation_width())/viewport.width);
        const float eyes[4]{layered?-stereo_separation_:0.f,layered?stereo_separation_:0.f,stereo_convergence_,screen_shift};
        [e setVertexBytes:eyes length:sizeof eyes atIndex:7];
        // Metal copies set*Bytes immediately; large vertex batches need a buffer.
        if(native_vertices_){
            for(unsigned i=0;i<4;++i)if(streams[i].buffer)[e setVertexBuffer:m.buffer(streams[i].buffer) offset:streams[i].offset atIndex:i];
            [e setVertexBuffer:m.default_vertex offset:0 atIndex:4];
            [e setVertexBytes:vs_constants.data() length:sizeof vs_constants atIndex:5];
            const float viewport[]{soft_viewport_.w,soft_viewport_.h,depth_bias,0};
            [e setVertexBytes:viewport length:sizeof viewport atIndex:6];
        }else{
            NSUInteger vertex_offset=0;
            id<MTLBuffer> vertex_buffer=m.upload_vertices(batch_.data(),batch_.size()*sizeof(Vertex),vertex_offset);
            [e setVertexBuffer:vertex_buffer offset:vertex_offset atIndex:0];
        }
        [e setFragmentBytes:&u length:sizeof u atIndex:0];
        require(m.samples<65536,"Metal visibility query capacity exceeded");
        [e setVisibilityResultMode:MTLVisibilityResultModeCounting offset:std::size_t(m.samples++)*8];
        if(native_vertices_)[e drawIndexedPrimitives:native_type_==5?MTLPrimitiveTypeTriangleStrip:MTLPrimitiveTypeTriangle indexCount:native_count_ indexType:MTLIndexTypeUInt16 indexBuffer:m.buffer(indices) indexBufferOffset:std::size_t(native_start_)*2 instanceCount:1 baseVertex:native_base_ baseInstance:0];
        else [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:batch_.size()];batch_.clear();batch_rhw_=false;
    }
}
}
