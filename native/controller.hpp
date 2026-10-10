#pragma once
#include "render.hpp"
#include <array>
#include <bitset>
#include <cstdint>
#include <iosfwd>
#include <string_view>
namespace k22 {
// Contract F: ${XDG_CONFIG_HOME:-~/.config}/kabus22/controller.ini. Absent file or mode=original leaves Input untouched.
struct ControllerConfig {
 static constexpr int LeftTrigger=SDL_CONTROLLER_BUTTON_MAX,RightTrigger=SDL_CONTROLLER_BUTTON_MAX+1,Sources=SDL_CONTROLLER_BUTTON_MAX+2;
 bool modern=false,vibration=true;float deadzone=0.15f,sensitivity=1;
 std::array<std::uint8_t,Sources> buttons=default_buttons(); // original joystick button 1-12; 0 unmapped
 static std::array<std::uint8_t,Sources> default_buttons();
};
ControllerConfig parse_controller_config(std::string_view text,std::ostream& warnings);
ControllerConfig load_controller_config(std::ostream& warnings);
// Radial: magnitudes inside the deadzone become 0, the rest rescales to 0-1 keeping direction.
std::array<float,2> radial_deadzone(float x,float y,float deadzone);
// Modern mode: remaps [Buttons] into legacy joystick slot 0 (with edges against `held`) and returns joy_force.
// Original GenelAyarlar.wdl:75-78 halves joy_force into JOY_Ileri/Geri/Sag/Sol, and oyuncu2.wdl moves only on
// Key_Ileri==1, so forward/back is digital (force 2) while turn is analog 0-2, scaled by sensitivity.
Vec3 apply_modern_controller(const ControllerConfig&,Input&,std::bitset<32>& held);
// Enhancement (no original force feedback exists): rumble strength when the same player entity's health
// (oyuncu2.wdl DEFINE Enerji,skill1) drops; a new player entity (level/save load) only re-baselines.
struct DamageRumble {std::uint32_t player=0;double health=0;float update(std::uint32_t id,double current);};
}
