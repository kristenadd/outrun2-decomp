#pragma once
// The glare (bloom) of the frame renderer 449050: after layer 6, when the
// byte 95AF09 is set, 414E50 copies the back buffer into a 640x480 target and
// 414F00 runs the glare: a bright pass (4152E0, pixel shader 622348 with the
// stage's glow record 8A8C18..8A8C5C), [8A8C40]-1 blur passes ping-ponging
// two targets (415630, 622428: four taps spreading by [8A8C44]/640, then
// [8A8C48]/640 more per pass, the previous target added back at [8A8C4C]),
// and the composite over the screen (415ED0, 6224B4) with the stage's blend
// factors (6237F0[[8A8C54]], 6237F0[[8A8C58]]).
//
// On the PC 95AF09 is the second byte of the DX/WINDOWED value (40E700), so
// the retail game never ran it. The bright pass keys on the back buffer's
// alpha, which the PC never fills with the materials' exposure (the empty
// alpha-op fragment); with the glare on, the port restores that as
// OutRun2006Tweaks' RestoreSkyGlow does (pc_render_flush.cpp: 40B200 linker,
// 40AF80 alpha reference, 408F56; pc_frame_render.cpp: the clear's alpha).
// The port enables it with options.ini glare=1 (g_pc_glare, default on) and
// OR2_GLARE=0/1. The targets and shaders (414C00 / 414CE0, created with the
// device on the PC) are created on first use.
#include <cstdint>
#include <string>
namespace outrun::platform {
struct PcFlushContext;
inline bool g_pc_glare=true;   // options.ini glare= (port option, default on), OR2_GLARE overrides
// Is the glare on (g_pc_glare, OR2_GLARE)?
bool pc_glare_enabled();
struct PcGlareState {
    const void* device{};                    // the device the objects below belong to
    std::uint32_t copy_8a8c14{},bright_8a8c10{},blur_8a8c68[2]{};   // 640x480 A8R8G8B8 render targets
    std::uint32_t ps_8a8c70{},ps_8a8c64{},ps_8a8c0c{},ps_8a8c60{};   // 622348 / 622428 / 6224B4 / 6224D0
    std::uint32_t current_8a8c08{};          // the texture the composite shows
    bool unavailable{};                      // the device cannot create them: the glare stays off
    bool captured{};                         // 414E50 copied the scene this frame
    std::uint32_t runs{},copies{},skipped{};
    std::string error;
};
PcGlareState& pc_glare_state();
// 414E50 / 414F00 (frame leaves, layer 6). flags_79fcd5: event 397's flags
// (PART_EFC; the glare runs while it is live); layer_7d25f0 for 408880.
// False when the leaf is not one of them.
bool pc_glare_leaf(PcFlushContext&,std::uint32_t pc,std::uint8_t flags_79fcd5,std::uint32_t layer_7d25f0);
}
