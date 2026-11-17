#include "platform/button_prompts.hpp"
#include "platform/pc_dds.hpp"
#include "platform/pc_sprite_2d.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
namespace outrun::platform {
namespace {
std::atomic<std::uint8_t> g_style{std::uint8_t(ButtonPromptStyle::Keyboard)};
bool trace(){static const bool on=std::getenv("OR2_TRACE")!=nullptr;return on;}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
// OR2_BUTTON_PROMPTS: forced style, or -1.
int forced_style(){
    static const int forced=[]{
        const char* v=std::getenv("OR2_BUTTON_PROMPTS");if(!v)return -1;
        const auto s=lower(v);
        if(s=="keyboard"||s=="pc")return int(ButtonPromptStyle::Keyboard);
        if(s=="xbox")return int(ButtonPromptStyle::Xbox);
        if(s=="playstation"||s=="ps")return int(ButtonPromptStyle::PlayStation);
        if(s=="switch"||s=="nintendo")return int(ButtonPromptStyle::Switch);
        return -1;
    }();
    return forced;
}
// The six sheets OutRun2006Tweaks swaps (hooks_textures.cpp, HandleTexture):
// XXH32 of the PC's DDS. 455717B2 congrats, 1F77CB88 etc (the small key
// icons), FA7BBB13 fruity, 39229D64 game, ACF61D7C / 31C58963 sumo_fe.
constexpr std::uint32_t PromptSheets[]={0x455717B2u,0x1F77CB88u,0xFA7BBB13u,0x39229D64u,0xACF61D7Cu,0x31C58963u};
std::uint32_t rd32(const std::uint8_t* p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
void wr32(std::uint8_t* p,std::uint32_t v){std::memcpy(p,&v,4);}
std::uint32_t rotl(std::uint32_t x,int r){return (x<<r)|(x>>(32-r));}
// Files under <root>/textures/load, listed once per game folder.
const std::vector<std::filesystem::path>& load_files(const std::string& root){
    static std::mutex m;static std::map<std::string,std::vector<std::filesystem::path>> cache;
    std::lock_guard<std::mutex> lock(m);
    auto [it,fresh]=cache.try_emplace(root);
    if(fresh){
        std::error_code ec;
        // "textures" may be in any case on a case-sensitive file system.
        std::filesystem::path base;
        for(const auto& e:std::filesystem::directory_iterator(root,ec))
            if(lower(e.path().filename().string())=="textures"&&e.is_directory(ec))
                for(const auto& f:std::filesystem::directory_iterator(e.path(),ec))
                    if(lower(f.path().filename().string())=="load"&&f.is_directory(ec))base=f.path();
        if(!base.empty())
            for(auto r=std::filesystem::recursive_directory_iterator(base,std::filesystem::directory_options::skip_permission_denied,ec);
                !ec&&r!=std::filesystem::recursive_directory_iterator();r.increment(ec))
                if(r->is_regular_file(ec)&&lower(r->path().extension().string())==".dds")it->second.push_back(r->path());
        if(trace())std::fprintf(stderr,"[btn] %zu replacement textures under %s\n",it->second.size(),base.empty()?"(no textures/load)":base.string().c_str());
    }
    return it->second;
}
// Box filter (alpha-weighted colour) of a 32-bit image sw x sh (channel byte
// offsets r, g, b, a in each texel; a < 0: opaque) down to w x h, written as
// A8R8G8B8 (B, G, R, A bytes) after a 128-byte DDS header.
std::vector<std::uint8_t> shrink_to_dds(const std::uint8_t* src,std::uint32_t sw,std::uint32_t sh,const int ch[4],
                                        std::uint32_t w,std::uint32_t h){
    std::vector<std::uint8_t> out(128+std::size_t(w)*h*4,0);
    auto* hd=out.data();
    std::memcpy(hd,"DDS ",4);wr32(hd+4,124);wr32(hd+8,0x100fu);wr32(hd+12,h);wr32(hd+16,w);wr32(hd+20,w*4);wr32(hd+28,1);
    wr32(hd+76,32);wr32(hd+80,0x41u);wr32(hd+88,32);wr32(hd+92,0xff0000u);wr32(hd+96,0xff00u);wr32(hd+100,0xffu);wr32(hd+104,0xff000000u);wr32(hd+108,0x1000u);
    const std::uint32_t rx=sw/w,ry=sh/h,n=rx*ry;
    auto* px=out.data()+128;
    for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){
        std::uint64_t r=0,g=0,b=0,a=0;
        for(std::uint32_t dy=0;dy<ry;++dy){
            const auto* s=src+(std::size_t(y*ry+dy)*sw+std::size_t(x)*rx)*4;
            for(std::uint32_t dx=0;dx<rx;++dx,s+=4){
                const unsigned al=ch[3]<0?255u:s[ch[3]];
                r+=std::uint64_t(s[ch[0]])*al;g+=std::uint64_t(s[ch[1]])*al;b+=std::uint64_t(s[ch[2]])*al;a+=al;}
        }
        auto* d=px+(std::size_t(y)*w+x)*4;
        if(a){d[2]=std::uint8_t((r+a/2)/a);d[1]=std::uint8_t((g+a/2)/a);d[0]=std::uint8_t((b+a/2)/a);}
        d[3]=std::uint8_t((a+n/2)/n);
    }
    return out;
}
// Byte offset of an 8-bit channel mask in a little-endian texel, or -1.
int mask_byte(std::uint32_t mask){for(int k=0;k<4;++k)if(mask==(0xffu<<(8*k)))return k;return -1;}
}
void button_prompts_set_style(ButtonPromptStyle s){g_style.store(std::uint8_t(s),std::memory_order_relaxed);}
ButtonPromptStyle button_prompts_style(){
    const int f=forced_style();
    return f>=0?ButtonPromptStyle(f):ButtonPromptStyle(g_style.load(std::memory_order_relaxed));
}
const char* button_prompts_style_name(ButtonPromptStyle s){
    switch(s){case ButtonPromptStyle::Xbox:return "Xbox";case ButtonPromptStyle::PlayStation:return "PlayStation";
              case ButtonPromptStyle::Switch:return "Switch";default:return "Keyboard";}
}
ButtonPromptStyle button_prompts_style_for_sdl_type(int t){
    // SDL_GameControllerType (SDL 2.24+): 1/2 Xbox 360/One, 3/4/7 PS3/PS4/PS5,
    // 5 Switch Pro, 11..13 Joy-Con left/right/pair.
    if(t<0)return ButtonPromptStyle::Keyboard;
    if(t==3||t==4||t==7)return ButtonPromptStyle::PlayStation;
    if(t==5||(t>=11&&t<=13))return ButtonPromptStyle::Switch;
    return ButtonPromptStyle::Xbox;
}
std::uint32_t button_prompts_xxh32(const std::uint8_t* p,std::size_t len,std::uint32_t seed){
    constexpr std::uint32_t P1=2654435761u,P2=2246822519u,P3=3266489917u,P4=668265263u,P5=374761393u;
    const auto* end=p+len;std::uint32_t h;
    if(len>=16){
        std::uint32_t v1=seed+P1+P2,v2=seed+P2,v3=seed,v4=seed-P1;
        const auto* limit=end-16;
        do{
            v1=rotl(v1+rd32(p)*P2,13)*P1;p+=4;v2=rotl(v2+rd32(p)*P2,13)*P1;p+=4;
            v3=rotl(v3+rd32(p)*P2,13)*P1;p+=4;v4=rotl(v4+rd32(p)*P2,13)*P1;p+=4;
        }while(p<=limit);
        h=rotl(v1,1)+rotl(v2,7)+rotl(v3,12)+rotl(v4,18);
    }else h=seed+P5;
    h+=std::uint32_t(len);
    while(p+4<=end){h=rotl(h+rd32(p)*P3,17)*P4;p+=4;}
    while(p<end){h=rotl(h+(*p)*P5,11)*P1;++p;}
    h^=h>>15;h*=P2;h^=h>>13;h*=P3;h^=h>>16;
    return h;
}
std::vector<std::uint8_t> button_prompts_load_replacement(const std::string& root,const std::string& stem,
    ButtonPromptStyle style,std::uint32_t hash,std::uint32_t width,std::uint32_t height,std::string& note){
    if(style==ButtonPromptStyle::Keyboard||root.empty())return {};
    char name[48];std::snprintf(name,sizeof name,"%X_%ux%u.dds",hash,width,height);
    const auto want=lower(name),folder=lower(button_prompts_style_name(style)),lstem=lower(stem);
    // A file of that name with the style's folder on its path; one in the bank's own folder first.
    const std::filesystem::path* best=nullptr;int best_rank=0;
    for(const auto& f:load_files(root)){
        if(lower(f.filename().string())!=want)continue;
        bool in_style=false,in_stem=false;
        for(const auto& part:f.parent_path()){const auto l=lower(part.string());in_style|=l==folder;in_stem|=l==lstem;}
        if(!in_style)continue;
        const int rank=in_stem?2:1;
        if(rank>best_rank){best=&f;best_rank=rank;}
    }
    if(!best){note=std::string("no ")+button_prompts_style_name(style)+" file "+name;return {};}
    std::ifstream in(*best,std::ios::binary|std::ios::ate);
    if(!in){note="cannot read "+best->string();return {};}
    const auto size=std::size_t(in.tellg());
    if(size<128||size>(std::size_t(256)<<20)){note="bad size "+best->string();return {};}
    std::vector<std::uint8_t> file(size);in.seekg(0);in.read(reinterpret_cast<char*>(file.data()),std::streamsize(size));
    if(!in){note="short read "+best->string();return {};}
    const std::uint32_t sh=rd32(file.data()+12),sw=rd32(file.data()+16),pf=rd32(file.data()+80),bits=rd32(file.data()+88);
    if(!sw||!sh||sw<width||sh<height||sw%width||sh%height){note=best->string()+": not a multiple of "+std::to_string(width)+"x"+std::to_string(height);return {};}
    note=best->string();
    if(!(pf&4u)&&bits==32u){
        // Uncompressed 32-bit, any byte order (the pack's files are A8B8G8R8).
        const int ch[4]={mask_byte(rd32(file.data()+92)),mask_byte(rd32(file.data()+96)),mask_byte(rd32(file.data()+100)),
                         (pf&1u)?mask_byte(rd32(file.data()+104)):-1};
        if(ch[0]<0||ch[1]<0||ch[2]<0){note+=": unsupported channel masks";return {};}
        if(size<128+std::size_t(sw)*sh*4){note+=": shorter than its image";return {};}
        return shrink_to_dds(file.data()+128,sw,sh,ch,width,height);
    }
    // DXT1/3/5 (or the D3DX formats the port reads).
    PcTextureFileRequest q{};q.data=file.data();q.size=std::uint32_t(size);q.mip_levels=1;
    PcDdsTexture t;std::string error;
    if(!pc_d3dx_texture_from_file(q,t,error)||t.faces.empty()||t.faces[0].empty()){note+=": "+error;return {};}
    file.clear();file.shrink_to_fit();
    const auto rgba=pc_dds_rgba(t,0,0);
    t.faces.clear();
    const int ch[4]={0,1,2,3};
    return shrink_to_dds(rgba.data(),sw,sh,ch,width,height);
}
void ButtonPrompts::register_texture(PcSpriteBank& b,PcD3D9Device& d,const std::string& root,const std::string& xst,
                                     std::uint32_t bank,std::uint32_t index,const std::uint8_t* dds,std::size_t size){
    if(!dds||size<128||index>=b.textures.size())return;
    const auto hash=button_prompts_xxh32(dds,size);
    if(std::find(std::begin(PromptSheets),std::end(PromptSheets),hash)==std::end(PromptSheets))return;
    Slot s;s.bank=bank;s.index=index;s.hash=hash;s.height=rd32(dds+12);s.width=rd32(dds+16);s.root=root;
    s.stem=std::filesystem::path(xst).stem().string();
    s.original.assign(dds,dds+size);
    if(trace())std::fprintf(stderr,"[btn] bank %u texture %u is prompt sheet %X (%ux%u, %s)\n",bank,index,hash,s.width,s.height,s.stem.c_str());
    slots_.push_back(std::move(s));
    apply(slots_.back(),b,d,button_prompts_style());
}
bool ButtonPrompts::apply(Slot& s,PcSpriteBank& b,PcD3D9Device& d,ButtonPromptStyle style){
    if(s.tried==style||s.index>=b.textures.size())return false;
    s.tried=style;                                    // once per style change, found or not
    std::vector<std::uint8_t> replacement;
    if(style!=ButtonPromptStyle::Keyboard){
        std::string note;
        replacement=button_prompts_load_replacement(s.root,s.stem,style,s.hash,s.width,s.height,note);
        if(trace())std::fprintf(stderr,"[btn] %X %s: %s%s\n",s.hash,button_prompts_style_name(style),replacement.empty()?"":"from ",note.c_str());
    }
    // No file for this style: the game's own sheet (put back if another style's art is in place).
    const auto show=replacement.empty()?ButtonPromptStyle::Keyboard:style;
    if(show==s.shown)return false;
    const auto& data=replacement.empty()?s.original:replacement;
    PcTextureFileRequest q{};
    q.data=data.data();q.size=std::uint32_t(data.size());
    q.width=q.height=0xffffffffu;q.mip_levels=1;q.usage=0;q.format=0;q.pool=1;q.filter=1;q.mip_filter=1;q.colour_key=0;
    const auto tex=d.create_texture_from_file(q);
    if(!tex){if(trace())std::fprintf(stderr,"[btn] %X: texture create failed\n",s.hash);return false;}
    const auto old=b.textures[s.index];
    b.textures[s.index]=tex;
    if(old)d.release(old);
    s.shown=show;
    if(show!=ButtonPromptStyle::Keyboard)++replaced_;
    return true;
}
void ButtonPrompts::update(PcSpriteBank* banks,std::size_t count,PcD3D9Device& d){
    const auto style=button_prompts_style();
    if(style==current_)return;
    current_=style;
    if(trace())std::fprintf(stderr,"[btn] style %s\n",button_prompts_style_name(style));
    for(auto& s:slots_)if(s.bank<count&&banks[s.bank].state==2u)apply(s,banks[s.bank],d,style);
}
}
