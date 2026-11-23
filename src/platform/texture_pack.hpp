#pragma once
// Port feature (not original code): OutRun2006Tweaks texture packs.
//
// OutRun2006Tweaks (UITextureReplacement / SceneTextureReplacement) replaces a
// texture the game creates with D3DXCreateTextureFromFileInMemoryEx when a file
//
//   <game>/textures/load/[any folders/]<HASH>_<w>x<h>.dds
//
// exists, HASH being the XXH32 (seed 0, upper-case hex) of the original DDS
// and w x h its size. Packs (the OR2006Sprites "HD GUI" pack, ...) keep the
// files in a folder per XST / PMT (spr_sprani_sumo_fe_cvt_Exst, cs_BK_..._pmt);
// a file in the texture's own folder wins over one elsewhere. Folders named
// Keyboard / Xbox / PlayStation / Switch hold controller button prompt art,
// which only ButtonPrompts (button_prompts.hpp) uses.
//
// The replacement art is an integer multiple of the original size (4x in the
// HD GUI pack, up to 16x). It is kept at up to `max scale` times the original
// (box-filtered down when larger) and created with
// PcD3D9Device::create_replacement_texture, which reports the original size
// to GetLevelDesc: the 2D renderer divides texel coordinates by that size, so
// every sprite keeps its place and only gains detail.
//
// Settings: options.ini hd_textures=0 (off) / 1..16 (the largest scale kept);
// OR2_HD_TEXTURES=<n> overrides it. Memory is the limit: the HD GUI pack's
// frontend bank alone is about 1.3 GB of RGBA at 4x, 320 MB at 2x.
//
// 2D sprite banks (TexturePackUi) are decoded on a worker thread and swapped
// in after a full 2D flush, so the game's own art shows until then. Scene
// (PMT) textures are replaced synchronously when the PMT loads
// (texture_pack_scene_texture). OR2_TRACE logs `[tex]` lines.
#include "platform/pc_d3d9.hpp"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace outrun::platform {
// 0 = off. Default set by the platform (options.ini); OR2_HD_TEXTURES wins.
void texture_pack_set_max_scale(unsigned);
unsigned texture_pack_max_scale();
// The game folder (set once the retail store is open).
void texture_pack_set_root(const std::string&);
std::string texture_pack_root();
bool texture_pack_trace();

// <HASH>_<w>x<h>.dds under <root>/textures/load. `style` empty: a file outside
// the controller style folders. `style` set: a file inside that style's folder
// (or, with `allow_unstyled`, outside every style folder). A file with `stem`
// (the bank's XST / PMT name, case-insensitive) on its path wins. Empty: none.
std::filesystem::path texture_pack_find(const std::string& root,const std::string& stem,std::uint32_t hash,
                                        std::uint32_t width,std::uint32_t height,const std::string& style={},
                                        bool allow_unstyled=false);
// The number of .dds files under <root>/textures/load (0: no pack).
std::size_t texture_pack_file_count(const std::string& root);
// The same without the controller prompt folders (Keyboard / Xbox / PlayStation /
// Switch): 0 when only button prompts are installed (Settings > HD TEXTURES).
std::size_t texture_pack_art_count(const std::string& root);

// A decoded replacement: RGBA8 levels (R,G,B,A), level k = width>>k x height>>k.
struct TexturePackImage {
    std::uint32_t width{},height{},scale{};
    std::vector<std::vector<std::uint8_t>> levels;
};
// Reads `file` (DXT1/3/5 or 32-bit RGB(A), an integer multiple k of
// width x height) and keeps it at s x the original, s the largest divisor of
// k not above max_scale (s >= 1), with `levels` mip levels (box filter; when
// levels > 1, log2(s) more, so the chain ends at the original's smallest level).
bool texture_pack_decode(const std::filesystem::path& file,std::uint32_t width,std::uint32_t height,unsigned max_scale,
                         unsigned levels,TexturePackImage& out,std::string& note);

// Scene textures: the PMT loader's D3DXCreateTextureFromFileInMemoryEx
// request, replaced when the pack has the file (0: create the game's own).
std::uint32_t texture_pack_scene_texture(PcD3D9Device&,const PcTextureFileRequest&);

struct PcSpriteBank;
// 2D sprite banks: replacements decoded on a worker thread.
class TexturePackUi {
public:
    TexturePackUi()=default;
    TexturePackUi(const TexturePackUi&)=delete;
    TexturePackUi& operator=(const TexturePackUi&)=delete;
    ~TexturePackUi();
    // A bank texture just created (`handle` = banks[bank].textures[index]) from
    // the DDS `dds`: queued when the pack has a file for it. Button prompt
    // sheets are left to ButtonPrompts.
    void register_texture(const std::string& root,const std::string& xst,std::uint32_t bank,std::uint32_t index,
                          std::uint32_t handle,const std::uint8_t* dds,std::size_t size);
    // Installs finished replacements (a few per call). Call only while no
    // queued 2D node refers to the bank textures (after a full flush).
    void update(PcSpriteBank* banks,std::size_t bank_count,PcD3D9Device&);
    std::uint32_t replaced()const{return replaced_;}
private:
    struct Job { std::uint32_t bank{},index{},handle{},width{},height{};std::filesystem::path file; };
    struct Done { Job job;TexturePackImage image;std::string note;bool ok{}; };
    void run();
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::deque<Done> done_;
    std::thread worker_;
    bool stop_{};
    std::uint32_t replaced_{},failed_{};
};
}
