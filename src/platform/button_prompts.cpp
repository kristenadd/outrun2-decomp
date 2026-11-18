#include "platform/button_prompts.hpp"
#include "platform/texture_pack.hpp"
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
std::uint32_t rotl(std::uint32_t x,int r){return (x<<r)|(x>>(32-r));}
}
bool button_prompts_is_sheet(std::uint32_t hash){return std::find(std::begin(PromptSheets),std::end(PromptSheets),hash)!=std::end(PromptSheets);}
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
void ButtonPrompts::register_texture(PcSpriteBank& b,PcD3D9Device& d,const std::string& root,const std::string& xst,
                                     std::uint32_t bank,std::uint32_t index,const std::uint8_t* dds,std::size_t size){
    if(!dds||size<128||index>=b.textures.size())return;
    const auto hash=button_prompts_xxh32(dds,size);
    if(!button_prompts_is_sheet(hash))return;
    Slot s;s.bank=bank;s.index=index;s.hash=hash;s.height=rd32(dds+12);s.width=rd32(dds+16);s.root=root;
    s.stem=std::filesystem::path(xst).stem().string();
    s.original.assign(dds,dds+size);
    if(trace())std::fprintf(stderr,"[btn] bank %u texture %u is prompt sheet %X (%ux%u, %s)\n",bank,index,hash,s.width,s.height,s.stem.c_str());
    slots_.push_back(std::move(s));
    apply(slots_.back(),b,d,button_prompts_style());
}
bool ButtonPrompts::apply(Slot& s,PcSpriteBank& b,PcD3D9Device& d,ButtonPromptStyle style){
    const int want=int(style);
    if(s.tried==want||s.index>=b.textures.size())return false;
    s.tried=want;                                     // once per style change, found or not
    // Controller styles: their folder's art. Keyboard: the pack's own (HD) sheet
    // when texture packs are on (hd_textures), else the game's.
    const unsigned hd=texture_pack_max_scale();
    const bool keyboard=style==ButtonPromptStyle::Keyboard;
    TexturePackImage image;bool have=false;
    if(!keyboard||hd){
        std::string note;
        const auto file=texture_pack_find(s.root,s.stem,s.hash,s.width,s.height,button_prompts_style_name(style),keyboard);
        if(file.empty())note=std::string("no ")+button_prompts_style_name(style)+" file";
        else have=texture_pack_decode(file,s.width,s.height,std::max(1u,hd),1,image,note);
        if(trace())std::fprintf(stderr,"[btn] %X %s: %s%s%s\n",s.hash,button_prompts_style_name(style),have?"from ":"",
                                have||file.empty()?"":(file.string()+": ").c_str(),note.c_str());
    }
    // No file for this style: the game's own sheet (put back if other art is in place).
    const int show=have?want:-1;
    if(show==s.shown)return false;
    std::uint32_t tex=0;
    if(have)tex=d.create_replacement_texture(image.width,image.height,std::move(image.levels),s.width,s.height);
    else{
        PcTextureFileRequest q{};
        q.data=s.original.data();q.size=std::uint32_t(s.original.size());
        q.width=q.height=0xffffffffu;q.mip_levels=1;q.usage=0;q.format=0;q.pool=1;q.filter=1;q.mip_filter=1;q.colour_key=0;
        tex=d.create_texture_from_file(q);
    }
    if(!tex){if(trace())std::fprintf(stderr,"[btn] %X: texture create failed\n",s.hash);return false;}
    const auto old=b.textures[s.index];
    b.textures[s.index]=tex;
    if(old)d.release(old);
    s.shown=show;
    if(have)++replaced_;
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
