#include "platform/texture_pack.hpp"
#include "platform/button_prompts.hpp"
#include "platform/pc_dds.hpp"
#include "platform/pc_sprite_2d.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
namespace outrun::platform {
namespace {
std::atomic<unsigned> g_max_scale{0};
std::mutex g_root_mutex;
std::string g_root;
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::uint32_t rd32(const std::uint8_t* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
bool style_folder(const std::string& l){return l=="keyboard"||l=="xbox"||l=="playstation"||l=="switch";}
// The pack's files by lower-case file name, listed once per game folder.
struct PackIndex { std::size_t files{};std::map<std::string,std::vector<std::filesystem::path>> by_name; };
const PackIndex& pack_index(const std::string& root){
    static std::mutex m;static std::map<std::string,PackIndex> cache;
    std::lock_guard<std::mutex> lock(m);
    auto [it,fresh]=cache.try_emplace(root);
    if(fresh&&!root.empty()){
        std::error_code ec;
        // "textures" / "load" may be in any case on a case-sensitive file system.
        std::filesystem::path base;
        for(const auto& e:std::filesystem::directory_iterator(root,ec))
            if(lower(e.path().filename().string())=="textures"&&e.is_directory(ec))
                for(const auto& f:std::filesystem::directory_iterator(e.path(),ec))
                    if(lower(f.path().filename().string())=="load"&&f.is_directory(ec))base=f.path();
        if(!base.empty())
            for(auto r=std::filesystem::recursive_directory_iterator(base,std::filesystem::directory_options::skip_permission_denied,ec);
                !ec&&r!=std::filesystem::recursive_directory_iterator();r.increment(ec))
                if(r->is_regular_file(ec)&&lower(r->path().extension().string())==".dds"){
                    it->second.by_name[lower(r->path().filename().string())].push_back(r->path());++it->second.files;}
        if(texture_pack_trace())std::fprintf(stderr,"[tex] %zu pack textures under %s\n",it->second.files,
                                             base.empty()?"(no textures/load)":base.string().c_str());
    }
    return it->second;
}
// Alpha-weighted box filter of RGBA8 texels sw x sh down by an integer factor
// r (r x r texels per output texel); a fully transparent block keeps its plain
// average colour (no dark fringe under bilinear filtering).
std::vector<std::uint8_t> box_down(const std::uint8_t* src,std::uint32_t sw,std::uint32_t sh,std::uint32_t rx,std::uint32_t ry){
    const std::uint32_t w=sw/rx,h=sh/ry,n=rx*ry;
    std::vector<std::uint8_t> out(std::size_t(w)*h*4);
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){
        std::uint64_t r=0,g=0,b=0,a=0,pr=0,pg=0,pb=0;
        for(std::uint32_t dy=0;dy<ry;++dy){
            const auto* s=src+(std::size_t(y*ry+dy)*sw+std::size_t(x)*rx)*4;
            for(std::uint32_t dx=0;dx<rx;++dx,s+=4){
                r+=std::uint64_t(s[0])*s[3];g+=std::uint64_t(s[1])*s[3];b+=std::uint64_t(s[2])*s[3];a+=s[3];
                pr+=s[0];pg+=s[1];pb+=s[2];}
        }
        auto* d=out.data()+(std::size_t(y)*w+x)*4;
        if(a){d[0]=std::uint8_t((r+a/2)/a);d[1]=std::uint8_t((g+a/2)/a);d[2]=std::uint8_t((b+a/2)/a);}
        else{d[0]=std::uint8_t((pr+n/2)/n);d[1]=std::uint8_t((pg+n/2)/n);d[2]=std::uint8_t((pb+n/2)/n);}
        d[3]=std::uint8_t((a+n/2)/n);
    }
    return out;
}
int mask_byte(std::uint32_t mask){for(int k=0;k<4;++k)if(mask==(0xffu<<(8*k)))return k;return -1;}
}
void texture_pack_set_max_scale(unsigned s){g_max_scale.store(std::min(s,16u),std::memory_order_relaxed);}
unsigned texture_pack_max_scale(){
    static const int forced=[]{const char* v=std::getenv("OR2_HD_TEXTURES");return v?int(std::min(16ul,std::strtoul(v,nullptr,10))):-1;}();
    return forced>=0?unsigned(forced):g_max_scale.load(std::memory_order_relaxed);
}
void texture_pack_set_root(const std::string& r){std::lock_guard<std::mutex> lock(g_root_mutex);g_root=r;}
std::string texture_pack_root(){std::lock_guard<std::mutex> lock(g_root_mutex);return g_root;}
bool texture_pack_trace(){static const bool on=std::getenv("OR2_TRACE")!=nullptr;return on;}
std::size_t texture_pack_file_count(const std::string& root){return pack_index(root).files;}
std::filesystem::path texture_pack_find(const std::string& root,const std::string& stem,std::uint32_t hash,
                                        std::uint32_t width,std::uint32_t height,const std::string& style,bool allow_unstyled){
    if(root.empty())return {};
    char name[48];std::snprintf(name,sizeof name,"%X_%ux%u.dds",hash,width,height);
    const auto& index=pack_index(root);
    const auto it=index.by_name.find(lower(name));
    if(it==index.by_name.end())return {};
    const auto want_style=lower(style),lstem=lower(stem);
    const std::filesystem::path* best=nullptr;int best_rank=0;
    for(const auto& f:it->second){
        bool in_stem=false,in_want=false,in_other=false;
        for(const auto& part:f.parent_path()){
            const auto l=lower(part.string());
            in_stem|=!lstem.empty()&&l==lstem;
            if(style_folder(l)){if(l==want_style)in_want=true;else in_other=true;}
        }
        if(in_other)continue;
        if(want_style.empty()?in_want:!(in_want||allow_unstyled))continue;
        const int rank=1+(in_want?4:0)+(in_stem?2:0);
        if(rank>best_rank){best=&f;best_rank=rank;}
    }
    return best?*best:std::filesystem::path{};
}
bool texture_pack_decode(const std::filesystem::path& file,std::uint32_t width,std::uint32_t height,unsigned max_scale,
                         unsigned levels,TexturePackImage& out,std::string& note){
    out={};
    if(!width||!height){note="no original size";return false;}
    std::ifstream in(file,std::ios::binary|std::ios::ate);
    if(!in){note="cannot read";return false;}
    const auto size=std::size_t(in.tellg());
    if(size<128||size>(std::size_t(512)<<20)){note="bad size";return false;}
    std::vector<std::uint8_t> bytes(size);in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));
    if(!in){note="short read";return false;}
    if(std::memcmp(bytes.data(),"DDS ",4)!=0){note="not a DDS";return false;}
    const std::uint32_t sh=rd32(bytes.data()+12),sw=rd32(bytes.data()+16),pf=rd32(bytes.data()+80),bits=rd32(bytes.data()+88);
    if(!sw||!sh||sw%width||sh%height||sw/width!=sh/height){note="not a multiple of "+std::to_string(width)+"x"+std::to_string(height);return false;}
    const std::uint32_t k=sw/width;
    std::uint32_t s=std::max(1u,std::min(max_scale,k));
    while(k%s)--s;
    std::vector<std::uint8_t> rgba;
    if(!(pf&4u)&&bits==32u){
        // Uncompressed 32-bit, any byte order (the HD GUI pack's files are A8B8G8R8).
        const int ch[4]={mask_byte(rd32(bytes.data()+92)),mask_byte(rd32(bytes.data()+96)),mask_byte(rd32(bytes.data()+100)),
                         (pf&1u)?mask_byte(rd32(bytes.data()+104)):-1};
        if(ch[0]<0||ch[1]<0||ch[2]<0){note="unsupported channel masks";return false;}
        const std::size_t texels=std::size_t(sw)*sh;
        if(size<128+texels*4){note="shorter than its image";return false;}
        if(ch[0]==0&&ch[1]==1&&ch[2]==2&&ch[3]==3){bytes.erase(bytes.begin(),bytes.begin()+128);bytes.resize(texels*4);rgba=std::move(bytes);}
        else{
            rgba.resize(texels*4);
            const auto* p=bytes.data()+128;
            for(std::size_t t=0;t<texels;++t,p+=4){auto* d=rgba.data()+t*4;d[0]=p[ch[0]];d[1]=p[ch[1]];d[2]=p[ch[2]];d[3]=ch[3]<0?255:p[ch[3]];}
            std::vector<std::uint8_t>().swap(bytes);
        }
    }else{
        // DXT1/3/5 (or the D3DX formats the port reads): level 0 only.
        PcTextureFileRequest q{};q.data=bytes.data();q.size=std::uint32_t(size);
        q.width=q.height=0xffffffffu;q.mip_levels=1;q.pool=1;q.filter=1;q.mip_filter=1;
        PcDdsTexture t;std::string error;
        if(!pc_d3dx_texture_from_file(q,t,error)||t.faces.empty()||t.faces[0].empty()){note=error.empty()?"decode failed":error;return false;}
        std::vector<std::uint8_t>().swap(bytes);
        if(t.faces[0][0].width!=sw||t.faces[0][0].height!=sh){note="decoded size differs";return false;}
        rgba=pc_dds_rgba(t,0,0);
        if(rgba.size()!=std::size_t(sw)*sh*4){note="decode size";return false;}
    }
    const std::uint32_t r=k/s;
    if(levels>1)for(std::uint32_t m=s;m>1;m>>=1)++levels;      // the same smallest level as the original's chain
    out.width=width*s;out.height=height*s;out.scale=s;
    out.levels.push_back(r>1?box_down(rgba.data(),sw,sh,r,r):std::move(rgba));
    std::vector<std::uint8_t>().swap(rgba);
    for(unsigned l=1;l<levels;++l){
        const std::uint32_t pw=std::max(1u,out.width>>(l-1)),ph=std::max(1u,out.height>>(l-1));
        if(pw==1&&ph==1)break;
        out.levels.push_back(box_down(out.levels.back().data(),pw,ph,pw>1?2u:1u,ph>1?2u:1u));
    }
    note=file.string()+" at "+std::to_string(s)+"x";
    return true;
}
std::uint32_t texture_pack_scene_texture(PcD3D9Device& device,const PcTextureFileRequest& q){
    const unsigned max_scale=texture_pack_max_scale();
    if(!max_scale||q.cube||!q.data||q.size<128||std::memcmp(q.data,"DDS ",4)!=0)return 0;
    const auto root=texture_pack_root();
    if(root.empty()||!texture_pack_file_count(root))return 0;
    const std::uint32_t h=rd32(q.data+12),w=rd32(q.data+16);
    const auto hash=button_prompts_xxh32(q.data,q.size);
    const auto file=texture_pack_find(root,{},hash,w,h);
    if(file.empty())return 0;
    // The original's mip chain (D3DX: up to mip_levels, down to 1x1), and as many
    // more levels as the extra scale adds, so the smallest level matches.
    unsigned levels=1;
    if(q.mip_levels!=1u){
        const unsigned want=q.mip_levels==0u||q.mip_levels==0xffffffffu?32u:q.mip_levels;
        for(std::uint32_t m=std::max(w,h);m>1&&levels<want;m>>=1)++levels;
    }
    TexturePackImage image;std::string note;
    if(!texture_pack_decode(file,w,h,max_scale,levels,image,note)){
        if(texture_pack_trace())std::fprintf(stderr,"[tex] scene %X %ux%u: %s: %s\n",hash,w,h,file.string().c_str(),note.c_str());
        return 0;
    }
    const auto handle=device.create_replacement_texture(image.width,image.height,std::move(image.levels),w,h);
    if(texture_pack_trace())std::fprintf(stderr,"[tex] scene %X %ux%u: %s%s\n",hash,w,h,note.c_str(),handle?"":" (device refused)");
    return handle;
}
// ------------------------------------------------------------------ 2D banks
TexturePackUi::~TexturePackUi(){
    {std::lock_guard<std::mutex> lock(m_);stop_=true;jobs_.clear();}
    cv_.notify_all();
    if(worker_.joinable())worker_.join();
}
void TexturePackUi::register_texture(const std::string& root,const std::string& xst,std::uint32_t bank,std::uint32_t index,
                                     std::uint32_t handle,const std::uint8_t* dds,std::size_t size){
    if(!handle||!dds||size<128||!texture_pack_max_scale()||root.empty()||!texture_pack_file_count(root))return;
    const auto hash=button_prompts_xxh32(dds,size);
    if(button_prompts_is_sheet(hash))return;                    // ButtonPrompts swaps these per controller
    Job j;j.bank=bank;j.index=index;j.handle=handle;j.height=rd32(dds+12);j.width=rd32(dds+16);
    j.file=texture_pack_find(root,std::filesystem::path(xst).stem().string(),hash,j.width,j.height);
    if(j.file.empty())return;
    std::lock_guard<std::mutex> lock(m_);
    if(!worker_.joinable())worker_=std::thread([this]{run();});
    jobs_.push_back(std::move(j));
    cv_.notify_one();
}
void TexturePackUi::run(){
    for(;;){
        Job j;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock,[&]{return stop_||!jobs_.empty();});
            if(stop_)return;
            j=std::move(jobs_.front());jobs_.pop_front();
        }
        Done d;d.job=std::move(j);
        d.ok=texture_pack_decode(d.job.file,d.job.width,d.job.height,texture_pack_max_scale(),1,d.image,d.note);
        std::lock_guard<std::mutex> lock(m_);
        if(stop_)return;
        done_.push_back(std::move(d));
    }
}
void TexturePackUi::update(PcSpriteBank* banks,std::size_t count,PcD3D9Device& device){
    // A few per frame: each becomes a GPU upload at its first draw.
    constexpr std::size_t BudgetBytes=24u<<20;
    std::size_t spent=0;
    while(spent<BudgetBytes){
        Done d;
        {
            std::lock_guard<std::mutex> lock(m_);
            if(done_.empty())return;
            d=std::move(done_.front());done_.pop_front();
        }
        const auto& j=d.job;
        if(!d.ok){++failed_;if(texture_pack_trace())std::fprintf(stderr,"[tex] bank %X texture %u: %s: %s\n",j.bank,j.index,j.file.string().c_str(),d.note.c_str());continue;}
        // The bank must still hold the texture the job was made for.
        if(j.bank>=count||banks[j.bank].state!=2u||j.index>=banks[j.bank].textures.size()||banks[j.bank].textures[j.index]!=j.handle)continue;
        spent+=d.image.levels.empty()?0:d.image.levels[0].size();
        const auto w=d.image.width,h=d.image.height;
        const auto tex=device.create_replacement_texture(w,h,std::move(d.image.levels),j.width,j.height);
        if(!tex){++failed_;if(texture_pack_trace())std::fprintf(stderr,"[tex] bank %X texture %u: device refused %ux%u\n",j.bank,j.index,w,h);continue;}
        banks[j.bank].textures[j.index]=tex;
        device.release(j.handle);
        ++replaced_;
        if(texture_pack_trace())std::fprintf(stderr,"[tex] bank %X texture %u (%ux%u): %s\n",j.bank,j.index,j.width,j.height,d.note.c_str());
    }
}
}
