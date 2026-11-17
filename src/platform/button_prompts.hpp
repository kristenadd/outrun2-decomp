#pragma once
// Port feature (not original code): controller button prompts.
//
// The PC game draws keyboard keys in its button prompts ("ENTER Select",
// "ESC Back", the title's PRESS ENTER...). They live in six sprite sheets of
// the English XST banks. OutRun2006Tweaks (and the Multi Input mod built on
// it) swaps those sheets for controller versions when a pad is in use; the
// art comes from the OR2006Sprites "HD GUI" pack (Release/Xbox, PlayStation,
// Switch), which tools/button_prompts/fetch.sh downloads into the game folder:
//
//   <game>/textures/load/<Xbox|PlayStation|Switch>/<xst stem>/<HASH>_<w>x<h>.dds
//
// The sheets are found by the XXH32 of the original DDS (the names the pack
// uses). Any folder layout under textures/load works as long as one folder on
// the path is named after the pad style (case-insensitive), as the Tweaks
// loader accepts. The pack's sheets are 4x the original size; they are box-
// filtered down to the original size on load, so every sprite keeps its
// texel coordinates (the renderer divides by the real texture size).
//
// The style follows the connected controller (set by the platform's pad
// reader) and switches at runtime; with no controller (keyboard) the game's
// own sheets are used. OR2_BUTTON_PROMPTS=keyboard|xbox|playstation|switch
// forces one (testing). Missing files keep the game's sheet.
#include "platform/pc_d3d9.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace outrun::platform {
enum class ButtonPromptStyle : std::uint8_t { Keyboard, Xbox, PlayStation, Switch };
// Set by the platform layer every input sample (any thread).
void button_prompts_set_style(ButtonPromptStyle);
// The style in effect (OR2_BUTTON_PROMPTS overrides the controller).
ButtonPromptStyle button_prompts_style();
const char* button_prompts_style_name(ButtonPromptStyle);
// Maps an SDL_GameControllerType value (SDL2) to a style: Xbox 360 / One,
// PS3 / PS4 / PS5, Switch Pro / Joy-Con; any other controller shows Xbox
// (A/B/X/Y). A negative value means no controller (keyboard).
ButtonPromptStyle button_prompts_style_for_sdl_type(int sdl_controller_type);
std::uint32_t button_prompts_xxh32(const std::uint8_t* data,std::size_t size,std::uint32_t seed=0);

struct PcSpriteBank;
class ButtonPrompts {
public:
    // A texture of a bank just loaded: kept when it is one of the prompt
    // sheets (a copy of its DDS for switching back), and replaced at once if
    // the current style has a file. `root` is the game folder, `xst` the
    // bank's XST path ("sprite/spr_..._Exst.sz").
    void register_texture(PcSpriteBank&,PcD3D9Device&,const std::string& root,const std::string& xst,
                          std::uint32_t bank,std::uint32_t index,const std::uint8_t* dds,std::size_t size);
    // Applies a style change to the sheets already loaded. Call only while
    // no queued 2D node refers to them (after a full flush).
    void update(PcSpriteBank* banks,std::size_t bank_count,PcD3D9Device&);
    std::uint32_t replaced()const{return replaced_;}
private:
    struct Slot {
        std::uint32_t bank{},index{},hash{},width{},height{};
        std::string root,stem;
        std::vector<std::uint8_t> original;          // the game's DDS
        ButtonPromptStyle shown{ButtonPromptStyle::Keyboard};   // whose art the texture has
        ButtonPromptStyle tried{ButtonPromptStyle::Keyboard};   // the style last applied
    };
    bool apply(Slot&,PcSpriteBank&,PcD3D9Device&,ButtonPromptStyle);
    std::vector<Slot> slots_;
    ButtonPromptStyle current_{ButtonPromptStyle::Keyboard};
    std::uint32_t replaced_{};
};
// The replacement DDS of a sheet for a style, already scaled to width x
// height as a 32-bit A8R8G8B8 DDS; empty when there is no usable file.
std::vector<std::uint8_t> button_prompts_load_replacement(const std::string& root,const std::string& stem,
    ButtonPromptStyle,std::uint32_t hash,std::uint32_t width,std::uint32_t height,std::string& note);
}
