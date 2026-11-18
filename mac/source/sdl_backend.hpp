// macOS adaptation of ps5/source/sdl_backend.hpp; reference baseline remains unchanged.
#pragma once
#include "platform/mesh_preview_pack.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <chrono>
struct SDL_Window;struct SDL_Renderer;struct SDL_Texture;
namespace outrun::mac_runtime {
class SdlDisplay {
public:
    ~SdlDisplay();
    bool open(unsigned width,unsigned height,std::string& error);
    // A new canvas (logical) size; SDL keeps its aspect on the window.
    bool resize(unsigned width,unsigned height,std::string& error);
    // iOS: the screen's width / height (landscape); 0 on the Mac (resizable window).
    float screen_aspect()const;
    void clear();
    SDL_Texture* texels(const void* key,platform::MeshPreviewTextureFormat,unsigned w,unsigned h,const std::vector<std::uint8_t>&);
    void quad(const std::array<platform::MeshPreviewVertex,4>&,SDL_Texture*,unsigned,unsigned);
    void rgba(const void* key,const std::uint8_t* bytes,unsigned w,unsigned h,int x,int y,int width,int height,bool opaque=false);
    bool present(std::string& error);
    // Presents at most this many frames per second (FRAME RATE: 60 or 120); vsync still applies.
    void set_frames_per_second(unsigned fps){frames_per_second_=fps<60u?60u:fps;}
    bool read_pixels(std::vector<std::uint8_t>& pixels,std::string& error);
#ifdef OR2_VISIONOS
    // visionOS (visionos/source/vision_display.mm, which implements this class
    // with Metal for the RealityKit window): the PC frame as two eyes (both
    // opaque), and the stereo shift of a 3D DEPTH level for the window's size.
    void rgba_stereo(const void* key,const std::uint8_t* left,const std::uint8_t* right,unsigned w,unsigned h,int x,int y,int width,int height);
    // The PC frame left on the GPU (MetalD3D9Device::gpu_frame: id<MTLTexture> per eye), opaque.
    void gpu_frame(void* left,void* right,unsigned pixel_height,int x,int y,int width,int height);
    struct Stereo {float separation{},convergence{};};
    Stereo stereo(unsigned depth_level)const;
#endif
private:
    SDL_Texture* upload(const void* key,const std::uint8_t*,unsigned,unsigned,bool update);
    struct Image {SDL_Texture* texture{};unsigned width{},height{};};
    SDL_Window* window_{};SDL_Renderer* renderer_{};
    std::map<const void*,Image> textures_;
    unsigned width_{},height_{};
    std::chrono::steady_clock::time_point next_frame_{};
    unsigned frames_per_second_{60};
};
}
