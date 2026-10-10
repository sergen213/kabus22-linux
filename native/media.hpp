#pragma once

#include "assets.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace k22 {

// Decoded pixels are straight-alpha RGBA8, rows top to bottom.
// Native alpha-format metadata follows source bitmap loaders/encoded-texture
// upload selection, not decoded RGBA channels; Assets gates its use by profile.
Image decode_image(std::span<const uint8_t> bytes, std::string_view name);

// Indexed fonts retain their byte plane and use the caller's active 768-byte RGB
// palette, not the embedded bitmap palette. Expanded pixels have opaque alpha.
struct BitmapFontImage { Image image; std::vector<uint8_t> indices; };
BitmapFontImage decode_bitmap_font(std::span<const uint8_t> bytes, std::string_view name,
                                  std::span<const uint8_t> palette);

class Media {
public:
    using Handle = uint32_t;
    explicit Media(Assets& assets, bool open_audio = true);
    ~Media();
    Media(const Media&) = delete;
    Media& operator=(const Media&) = delete;

    Handle sound_play(std::string_view name, double volume = 100, double pan = 0,
                      double pitch = 100, bool loop = false);
    Handle sound_play_at(std::string_view name, Vec3 position, double volume = 100,
                         double range = 1000, bool loop = false);
    bool sound_playing(Handle handle) const;
    void sound_stop(Handle handle);
    void sound_stop_all();
    // A zero parameter preserves its current value, as in A6 snd_tune.
    void sound_tune(Handle handle, double volume, double pitch, double pan);
    void sound_position(Handle handle, Vec3 position);
    void listener(Vec3 position, double pan_degrees);
    // Independent live SOUND_VOL/MIDI_VOL percentages; zero genuinely mutes.
    void master_volume(double sound_volume, double midi_volume);

    Handle movie_play(std::string_view name, double volume = 100, bool loop = false);
    bool movie_playing(Handle handle) const;
    // One-based decoded frame number; zero after completion/stop.
    double movie_status(Handle handle) const;
    // Zero stops all movies; a bitmap target may retain the final frame.
    void movie_stop(Handle handle);
    // Image object remains stable for Media's lifetime, including after stop.
    const Image* movie_frame(Handle handle) const;
    // Called on the game/render thread before using movie_frame().
    void update();
    // Scenario mode: advance_clock switches movie/silent-voice time to a manual clock stepped by the
    // engine frame (deterministic, wall-clock independent); real_clock resumes wall time from there.
    // A device-backed mixer still consumes voices in real time. Unused outside scenarios.
    void advance_clock(double seconds);
    void real_clock();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace k22
