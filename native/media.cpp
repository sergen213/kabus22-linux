#include "media.hpp"

#include <SDL.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace k22 {
namespace {
constexpr int sample_rate = 48000;
using Clock = std::chrono::steady_clock;

uint32_t allocate_media_handle(uint32_t& next) {
    // WDL marshals handles through a signed 22.10 numeric value.
    if (!next || next >= (uint32_t{1} << 21))
        throw std::runtime_error("Native media signed 22.10 handle space exhausted");
    return next++;
}

void check(int result, std::string_view operation) {
    if (result >= 0) return;
    std::array<char, AV_ERROR_MAX_STRING_SIZE> error{};
    av_strerror(result, error.data(), error.size());
    throw std::runtime_error(std::string(operation) + ": " + error.data());
}
std::string cache_key(std::string_view name) {
    std::string key(name);
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '\\') c = '/';
    }
    return key;
}
struct CodecFree { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameFree { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PacketFree { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwrFree { void operator()(SwrContext* p) const { swr_free(&p); } };
struct SwsFree { void operator()(SwsContext* p) const { sws_freeContext(p); } };
using Codec = std::unique_ptr<AVCodecContext, CodecFree>;
using Frame = std::unique_ptr<AVFrame, FrameFree>;
using Packet = std::unique_ptr<AVPacket, PacketFree>;

std::span<const uint8_t> media_payload(std::span<const uint8_t> bytes) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return bytes;
    auto little = [&](size_t offset, size_t width) {
        uint32_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= uint32_t(bytes[offset + i]) << (8 * i);
        return value;
    };
    const size_t declared = little(4, 4);
    if (declared < 4) return bytes;
    const size_t end = 8 + std::min(declared, bytes.size() - 8);
    bool malformed = declared > bytes.size() - 8;
    bool seen_format = false, ogg_format = false, seen_data = false;
    std::span<const uint8_t> data;
    for (size_t at = 12; at < end;) {
        if (end - at < 8) { malformed = true; break; }
        const size_t size = little(at + 4, 4);
        const size_t start = at + 8;
        if (std::memcmp(bytes.data() + at, "fmt ", 4) == 0) {
            if (seen_format || size < 16) malformed = true;
            seen_format = true;
            // Even a malformed chunk must not read its tag outside the extent. Vorbis ACM tags
            // 0x674f-0x6751/0x676f-0x6771 (modes 1-3, 1+-3+): Kaynak.k22 uses 0x674f and 0x676f.
            if (size >= 2 && end - start >= 2) {
                const auto tag = little(start, 2);
                ogg_format |= (tag >= 0x674f && tag <= 0x6751) || (tag >= 0x676f && tag <= 0x6771);
            }
        }
        if (size > end - start) { malformed = true; break; }
        if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
            if (seen_data) malformed = true;
            seen_data = true;
            data = bytes.subspan(start, size);
        }
        at = start + size;
        if (size & 1) {
            if (at == end) { malformed = true; break; }
            ++at;
        }
    }
    if (!ogg_format) return bytes;
    if (malformed || !seen_data) throw std::runtime_error("Malformed OggDS RIFF media");
    // Windows OggDS wraps a complete Ogg stream, not raw Vorbis WAV packets.
    // Borrow only the declared data chunk; callbacks retain the owner's lifetime.
    if (data.size() >= 4 && std::memcmp(data.data(), "OggS", 4) == 0) return data;
    return bytes;
}

class Input {
    std::span<const uint8_t> bytes_;
    size_t cursor_{};
    AVIOContext* io_{};
    AVFormatContext* format_{};
    static int read_bytes(void* opaque, uint8_t* out, int count) {
        auto& self = *static_cast<Input*>(opaque);
        size_t remaining = self.bytes_.size() - self.cursor_;
        if (!remaining) return AVERROR_EOF;
        size_t amount = std::min(remaining, static_cast<size_t>(count));
        std::memcpy(out, self.bytes_.data() + self.cursor_, amount);
        self.cursor_ += amount;
        return static_cast<int>(amount);
    }
    static int64_t seek_bytes(void* opaque, int64_t offset, int whence) {
        auto& self = *static_cast<Input*>(opaque);
        if (whence & AVSEEK_SIZE) return static_cast<int64_t>(self.bytes_.size());
        whence &= ~AVSEEK_FORCE;
        int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<int64_t>(self.cursor_)
                      : whence == SEEK_END ? static_cast<int64_t>(self.bytes_.size()) : -1;
        if (base < 0 || offset < -base || offset > static_cast<int64_t>(self.bytes_.size()) - base)
            return AVERROR(EINVAL);
        self.cursor_ = static_cast<size_t>(base + offset);
        return static_cast<int64_t>(self.cursor_);
    }
    void close() noexcept {
        if (format_) avformat_close_input(&format_);
        if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
    }
public:
    Input(std::span<const uint8_t> bytes, std::string_view name) : bytes_(bytes) {
        if (bytes.empty() || bytes.size() > static_cast<size_t>(INT64_MAX))
            throw std::runtime_error("Empty or oversized media: " + std::string(name));
        bytes_ = media_payload(bytes);
        auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
        if (!buffer) throw std::bad_alloc();
        io_ = avio_alloc_context(buffer, 32768, 0, this, read_bytes, nullptr, seek_bytes);
        if (!io_) { av_free(buffer); throw std::bad_alloc(); }
        try {
            format_ = avformat_alloc_context();
            if (!format_) throw std::bad_alloc();
            format_->pb = io_;
            format_->flags |= AVFMT_FLAG_CUSTOM_IO;
            std::string filename(name);
            check(avformat_open_input(&format_, filename.c_str(), nullptr, nullptr), name);
            check(avformat_find_stream_info(format_, nullptr), "Media stream information");
        } catch (...) { close(); throw; }
    }
    ~Input() { close(); }
    Input(const Input&) = delete;
    AVFormatContext* get() const { return format_; }
    int stream(AVMediaType type) const {
        int index = av_find_best_stream(format_, type, -1, -1, nullptr, 0);
        if (index == AVERROR_STREAM_NOT_FOUND) return -1;
        check(index, "Selecting media stream");
        return index;
    }
};

class Decoder {
    Input input_;
    Codec codec_;
    Frame frame_{av_frame_alloc()};
    Packet packet_{av_packet_alloc()};
    int stream_;
    bool flushed_{};
    bool done_{};
public:
    Decoder(std::span<const uint8_t> bytes, std::string_view name, AVMediaType type)
        : input_(bytes, name), stream_(input_.stream(type)) {
        if (!frame_ || !packet_) throw std::bad_alloc();
        if (stream_ < 0) { done_ = true; return; }
        auto* parameters = input_.get()->streams[stream_]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) throw std::runtime_error("Native decoder unavailable: " + std::string(avcodec_get_name(parameters->codec_id)));
        codec_.reset(avcodec_alloc_context3(codec));
        if (!codec_) throw std::bad_alloc();
        check(avcodec_parameters_to_context(codec_.get(), parameters), "Codec parameters");
        codec_->err_recognition = AV_EF_CRCCHECK | AV_EF_CAREFUL;
        check(avcodec_open2(codec_.get(), codec, nullptr), "Opening native codec");
    }
    bool has_stream() const { return stream_ >= 0; }
    bool next() {
        if (done_) return false;
        for (;;) {
            int status = avcodec_receive_frame(codec_.get(), frame_.get());
            if (status == 0) return true;
            if (status == AVERROR_EOF) { done_ = true; return false; }
            if (status != AVERROR(EAGAIN)) check(status, "Decoding media frame");
            if (flushed_) throw std::runtime_error("Codec requested input after end of stream");
            for (;;) {
                av_packet_unref(packet_.get());
                status = av_read_frame(input_.get(), packet_.get());
                if (status == AVERROR_EOF) {
                    check(avcodec_send_packet(codec_.get(), nullptr), "Flushing decoder");
                    flushed_ = true;
                    break;
                }
                check(status, "Reading media packet");
                if (packet_->stream_index != stream_) continue;
                if (packet_->flags & AV_PKT_FLAG_CORRUPT)
                    throw std::runtime_error("Corrupt encoded media packet");
                check(avcodec_send_packet(codec_.get(), packet_.get()), "Submitting media packet");
                break;
            }
        }
    }
    AVFrame* frame() const { return frame_.get(); }
    AVCodecContext* codec() const { return codec_.get(); }
    double timestamp() const {
        int64_t pts = frame_->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) return -1;
        auto* stream = input_.get()->streams[stream_];
        double start = input_.get()->start_time == AV_NOPTS_VALUE ? 0
                     : input_.get()->start_time / static_cast<double>(AV_TIME_BASE);
        return pts * av_q2d(stream->time_base) - start;
    }
    double frame_seconds() const {
        auto* stream = input_.get()->streams[stream_];
        if (frame_->duration > 0) return frame_->duration * av_q2d(stream->time_base);
        AVRational rate = av_guess_frame_rate(input_.get(), stream, frame_.get());
        return rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30;
    }
    double duration() const {
        if (input_.get()->duration != AV_NOPTS_VALUE)
            return input_.get()->duration / static_cast<double>(AV_TIME_BASE);
        return 0;
    }
};

bool pixels(AVFrame* frame, Image& image, std::unique_ptr<SwsContext, SwsFree>& scale,
            std::vector<uint8_t>* indices = nullptr, std::span<const uint8_t> palette = {}) {
    // Same bound as the renderer's check_image; wide "+N" sprite strips (s_alev+61.tga, 18300 px) are re-laid out at load.
    if (frame->width <= 0 || frame->height <= 0 || frame->width > 32768 || frame->height > 32768)
        throw std::runtime_error("Invalid decoded image dimensions");
    image.width = static_cast<uint32_t>(frame->width);
    image.height = static_cast<uint32_t>(frame->height);
    image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
    if (indices && frame->format == AV_PIX_FMT_PAL8) {
        indices->resize(static_cast<size_t>(image.width) * image.height);
        for (uint32_t y = 0; y < image.height; ++y) {
            const uint8_t* row = frame->data[0] + static_cast<std::ptrdiff_t>(y) * frame->linesize[0];
            size_t offset = static_cast<size_t>(y) * image.width;
            std::memcpy(indices->data() + offset, row, image.width);
            uint8_t* rgba = image.rgba.data() + offset * 4;
            for (uint32_t x = 0; x < image.width; ++x) {
                size_t color = static_cast<size_t>(row[x]) * 3;
                rgba[x * 4] = palette[color];
                rgba[x * 4 + 1] = palette[color + 1];
                rgba[x * 4 + 2] = palette[color + 2];
                rgba[x * 4 + 3] = 255;
            }
        }
        image.sourceDepth = 8;
        image.nativeStorageBytes = 1;
        image.nativeTextureAlpha = false;
        return true;
    }
    scale.reset(sws_getCachedContext(scale.release(), frame->width, frame->height,
                                    static_cast<AVPixelFormat>(frame->format), frame->width,
                                    frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!scale) throw std::runtime_error("Unable to convert image pixels");
    uint8_t* destinations[4] = {image.rgba.data(), nullptr, nullptr, nullptr};
    int strides[4] = {frame->width * 4, 0, 0, 0};
    check(sws_scale(scale.get(), frame->data, frame->linesize, 0, frame->height, destinations, strides),
          "Converting decoded pixels");
    return false;
}
uint32_t image_depth(std::span<const uint8_t> bytes, AVCodecID codec, const AVFrame* frame,
                     bool* nativeTextureAlpha = nullptr, AVCodecID nativeLoader = AV_CODEC_ID_NONE) {
    // Native CPU-decoded encoded textures select A8R8G8B8 in 1002c7b0's
    // noncompressed branch, independent of source opacity/channels. The three
    // ordinary bitmap loaders instead preserve their source CPU packing.
    if (nativeTextureAlpha)
        *nativeTextureAlpha = nativeLoader != AV_CODEC_ID_PCX && nativeLoader != AV_CODEC_ID_BMP &&
                              nativeLoader != AV_CODEC_ID_TARGA;
    auto little = [&](size_t offset, size_t width) {
        uint32_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= uint32_t(bytes[offset + i]) << (i * 8);
        return value;
    };
    if (bytes.size() >= 66 && bytes[0] == 10 && codec == AV_CODEC_ID_PCX)
        return uint32_t(bytes[3]) * bytes[65];
    if (bytes.size() >= 26 && bytes[0] == 'B' && bytes[1] == 'M') {
        uint32_t header = little(14, 4);
        uint32_t depth = header == 12 ? little(24, 2)
                       : header >= 16 && bytes.size() >= 30 ? little(28, 2) : 0;
        if (depth) return depth;
    }
    if (bytes.size() >= 18 && codec == AV_CODEC_ID_TARGA && bytes[16]) {
        // A7 100120f0 sets BMAP100 for every source32 TGA, including fully opaque
        // pixels/attributeDepth0; ALPHADEPTH only selects CPU4 or CPU2 ARGB4444.
        if (nativeTextureAlpha && nativeLoader == AV_CODEC_ID_TARGA) *nativeTextureAlpha = bytes[16] == 32;
        return bytes[16];
    }
    constexpr std::array<uint8_t, 8> png_signature{137, 'P', 'N', 'G', 13, 10, 26, 10};
    if (bytes.size() >= 26 && std::equal(png_signature.begin(), png_signature.end(), bytes.begin()) &&
        std::memcmp(bytes.data() + 12, "IHDR", 4) == 0) {
        unsigned channels = bytes[25] == 0 || bytes[25] == 3 ? 1 : bytes[25] == 2 ? 3
                          : bytes[25] == 4 ? 2 : bytes[25] == 6 ? 4 : 0;
        if (channels) return uint32_t(bytes[24]) * channels;
    }
    if (bytes.size() >= 128 && std::memcmp(bytes.data(), "DDS ", 4) == 0) {
        // Preserve the DXT1 exception before FFmpeg converts to RGBA8. Native
        // decoded-texture policy follows 1002ca3c: all other stored DDS formats
        // select21, including XRGB22. This is not a claim about Windows GPU caps.
        // Ordinary10012470/MDL1005ef60 births do not produce runtime80000, which
        // would skip the uploader's automatic flagging rather than clear100.
        if (nativeTextureAlpha)
            *nativeTextureAlpha = !(little(80, 4) & 4) ||
                std::memcmp(bytes.data() + 84, "DXT1", 4) != 0;
        // FourCC compression has no packed RGB pixel depth; preserve that distinction.
        // Neither DXT1's 565 endpoints nor decoded RGBA32 describe its source bitmap.
        if (little(80, 4) & 4) return 0;
        if (little(88, 4)) return little(88, 4);
    }
    if (bytes.size() >= 6 && (std::memcmp(bytes.data(), "GIF87a", 6) == 0 ||
                              std::memcmp(bytes.data(), "GIF89a", 6) == 0)) return 8;
    if (codec == AV_CODEC_ID_MJPEG) return 24;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    if (!descriptor) throw std::runtime_error("Unknown decoded image pixel format");
    if (descriptor->flags & AV_PIX_FMT_FLAG_PAL) return 8;
    if (descriptor->flags & AV_PIX_FMT_FLAG_RGB)
        return static_cast<uint32_t>(av_get_padded_bits_per_pixel(descriptor));
    // Subsampling is compression, not a 16-bit RGB bitmap (e.g. JPEG YUV422).
    uint32_t depth = 0;
    for (unsigned i = 0; i < descriptor->nb_components; ++i) depth += descriptor->comp[i].depth;
    if (!depth) throw std::runtime_error("Invalid decoded image pixel depth");
    return depth;
}
struct Audio {
    std::vector<float> samples;
    size_t frames() const { return samples.size() / 2; }
};
Audio audio(std::span<const uint8_t> bytes, std::string_view name, bool required) {
    Decoder decoder(bytes, name, AVMEDIA_TYPE_AUDIO);
    if (!decoder.has_stream()) {
        if (required) throw std::runtime_error("Media has no audio stream: " + std::string(name));
        return {};
    }
    Audio result;
    std::unique_ptr<SwrContext, SwrFree> resampler;
    auto append = [&](AVFrame* frame) {
        int input_rate = decoder.codec()->sample_rate;
        int64_t delay = swr_get_delay(resampler.get(), input_rate);
        int64_t count = av_rescale_rnd(delay + (frame ? frame->nb_samples : 0), sample_rate,
                                     input_rate, AV_ROUND_UP);
        if (count < 0 || count > (int64_t{1} << 27) ||
            result.samples.size() > (size_t{1} << 28) - static_cast<size_t>(count) * 2)
            throw std::runtime_error("Decoded audio exceeds safe buffer size");
        size_t before = result.samples.size();
        result.samples.resize(before + static_cast<size_t>(count) * 2);
        uint8_t* target[] = {reinterpret_cast<uint8_t*>(result.samples.data() + before)};
        int converted = swr_convert(resampler.get(), target, static_cast<int>(count),
                                    frame ? const_cast<const uint8_t**>(frame->extended_data) : nullptr,
                                    frame ? frame->nb_samples : 0);
        check(converted, "Resampling decoded audio");
        result.samples.resize(before + static_cast<size_t>(converted) * 2);
        return converted;
    };
    while (decoder.next()) {
        AVFrame* frame = decoder.frame();
        if (!resampler) {
            if (decoder.codec()->sample_rate <= 0 || frame->ch_layout.nb_channels <= 0)
                throw std::runtime_error("Invalid audio sample rate/channel layout");
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            SwrContext* context = nullptr;
            check(swr_alloc_set_opts2(&context, &stereo, AV_SAMPLE_FMT_FLT, sample_rate,
                                     &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
                                     decoder.codec()->sample_rate, 0, nullptr), "Creating audio resampler");
            resampler.reset(context);
            check(swr_init(context), "Initializing audio resampler");
            double offset = std::max(0.0, decoder.timestamp());
            if (!std::isfinite(offset) || offset * sample_rate > (double{1} * (size_t{1} << 27)))
                throw std::runtime_error("Invalid audio start timestamp");
            result.samples.resize(static_cast<size_t>(offset * sample_rate) * 2, 0.0f);
        }
        append(frame);
    }
    if (resampler) while (append(nullptr) > 0) {}
    if (result.samples.empty() && required) throw std::runtime_error("Empty decoded audio: " + std::string(name));
    return result;
}

struct Voice {
    uint32_t handle{};
    const Audio* buffer{};
    double cursor{}, played_frames{}, volume{1}, pitch{1}, pan{};
    bool loop{}, active{}, positional{}, movie_source{};
    Vec3 position;
    double range{1000};
    float left{1}, right{1};
};
void advance(Voice& voice, double frames) {
    if (!voice.active) return;
    voice.cursor += frames * voice.pitch;
    voice.played_frames += frames * voice.pitch;
    if (voice.cursor >= voice.buffer->frames()) {
        if (voice.loop) voice.cursor = std::fmod(voice.cursor, static_cast<double>(voice.buffer->frames()));
        else voice.active = false;
    }
}

float master_gain(double percent) {
    if (!std::isfinite(percent)) throw std::runtime_error("Invalid native master volume");
    return static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100);
}
void mix_voices(std::span<Voice> voices, std::span<float> out,
                float sound_master, float movie_master) noexcept {
    std::fill(out.begin(), out.end(), 0.0f);
    for (Voice& voice : voices) {
        if (!voice.active) continue;
        const auto& samples = voice.buffer->samples;
        size_t total = voice.buffer->frames();
        float master = voice.movie_source ? movie_master : sound_master;
        float left = voice.left * master, right = voice.right * master;
        for (size_t i = 0; i + 1 < out.size() && voice.active; i += 2) {
            size_t at = static_cast<size_t>(voice.cursor);
            size_t next = at + 1 < total ? at + 1 : voice.loop ? 0 : at;
            float fraction = static_cast<float>(voice.cursor - at);
            float l = samples[at * 2] + (samples[next * 2] - samples[at * 2]) * fraction;
            float r = samples[at * 2 + 1] + (samples[next * 2 + 1] - samples[at * 2 + 1]) * fraction;
            out[i] += l * left;
            out[i + 1] += r * right;
            advance(voice, 1);
        }
    }
    for (float& sample : out) sample = std::clamp(sample, -1.0f, 1.0f);
}


Image decode_bitmap(std::span<const uint8_t> bytes, std::string_view name,
                    std::vector<uint8_t>* indices = nullptr, std::span<const uint8_t> palette = {}) {
    auto key = cache_key(name);
    auto dot = key.find_last_of('.');
    std::string_view extension = dot == std::string::npos ? std::string_view{} : std::string_view(key).substr(dot);
    AVCodecID id = extension == ".pcx" ? AV_CODEC_ID_PCX : extension == ".bmp" ? AV_CODEC_ID_BMP
                 : extension == ".tga" ? AV_CODEC_ID_TARGA : extension == ".dds" ? AV_CODEC_ID_DDS
                 : extension == ".jpg" || extension == ".jpeg" ? AV_CODEC_ID_MJPEG
                 : extension == ".png" ? AV_CODEC_ID_PNG : extension == ".gif" ? AV_CODEC_ID_GIF
                 : extension == ".tif" || extension == ".tiff" ? AV_CODEC_ID_TIFF
                 : extension == ".webp" ? AV_CODEC_ID_WEBP : AV_CODEC_ID_NONE;
    Image image;
    std::unique_ptr<SwsContext, SwsFree> scale;
    if (id == AV_CODEC_ID_NONE) {
        Decoder decoder(bytes, name, AVMEDIA_TYPE_VIDEO);
        if (!decoder.has_stream() || !decoder.next()) throw std::runtime_error("Image contains no frame: " + std::string(name));
        if (!pixels(decoder.frame(), image, scale, indices, palette))
            image.sourceDepth = image_depth(bytes, decoder.codec()->codec_id, decoder.frame(),
                                            &image.nativeTextureAlpha);
        return image;
    }
    if (bytes.empty() || bytes.size() > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        throw std::runtime_error("Empty or oversized image: " + std::string(name));
    const AVCodec* decoder = avcodec_find_decoder(id);
    if (!decoder) throw std::runtime_error("Image decoder unavailable: " + std::string(name));
    Codec codec(avcodec_alloc_context3(decoder));
    Frame frame(av_frame_alloc());
    Packet packet(av_packet_alloc());
    if (!codec || !frame || !packet) throw std::bad_alloc();
    codec->err_recognition = AV_EF_CRCCHECK | AV_EF_CAREFUL | AV_EF_EXPLODE;
    check(avcodec_open2(codec.get(), decoder, nullptr), name);
    check(av_new_packet(packet.get(), static_cast<int>(bytes.size())), "Image packet allocation");
    std::memcpy(packet->data, bytes.data(), bytes.size());
    check(avcodec_send_packet(codec.get(), packet.get()), "Submitting image");
    check(avcodec_receive_frame(codec.get(), frame.get()), "Decoding image");
    if (!pixels(frame.get(), image, scale, indices, palette))
        image.sourceDepth = image_depth(bytes, id, frame.get(), &image.nativeTextureAlpha, id);
    return image;
}
} // namespace

Image decode_image(std::span<const uint8_t> bytes, std::string_view name) {
    return decode_bitmap(bytes, name);
}

BitmapFontImage decode_bitmap_font(std::span<const uint8_t> bytes, std::string_view name,
                                  std::span<const uint8_t> palette) {
    if (palette.size() != 768) throw std::runtime_error("Bitmap font palette must contain 768 RGB bytes");
    BitmapFontImage font;
    font.image = decode_bitmap(bytes, name, &font.indices, palette);
    return font;
}

struct Media::Impl {
    struct Movie {
        std::vector<uint8_t> bytes;
        std::string name;
        std::unique_ptr<Decoder> decoder;
        Frame presented{av_frame_alloc()};
        Audio audio;
        Handle audio_handle{};
        Image image;
        std::unique_ptr<SwsContext, SwsFree> scale;
        Clock::time_point start;
        double duration{}, pending_time{}, last_end{};
        uint64_t frame_number{}, cycle{};
        bool active{}, loop{}, pending{}, video_eof{};
    };
    Assets& assets;
    SDL_AudioDeviceID device{};
    bool owns_audio{};
    Handle next_handle{1};
    std::unordered_map<std::string, std::unique_ptr<Audio>> sounds;
    std::vector<Voice> voices;
    std::unordered_map<Handle, std::unique_ptr<Movie>> movies;
    Vec3 listener_position;
    double listener_pan{};
    float sound_master{1}, movie_master{1};
    Clock::time_point silent_clock{Clock::now()};
    // Scenario mode (advance_clock): media time steps with the engine frame, not the wall clock. The offset
    // keeps time continuous across manual<->real switches; it stays zero outside scenarios.
    std::optional<Clock::time_point> manual;
    Clock::duration offset{};
    Clock::time_point now() const { return manual ? *manual : Clock::now() + offset; }

    struct Lock {
        Impl& self;
        explicit Lock(Impl& value) : self(value) {
            if (self.device) SDL_LockAudioDevice(self.device);
            else self.silent_advance();
        }
        ~Lock() { if (self.device) SDL_UnlockAudioDevice(self.device); }
    };
    explicit Impl(Assets& store, bool enabled) : assets(store) {
        if (!enabled) return;
        owns_audio = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0;
        if (owns_audio && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            throw std::runtime_error("SDL audio initialization: " + std::string(SDL_GetError()));
        SDL_AudioSpec desired{};
        desired.freq = sample_rate;
        desired.format = AUDIO_F32SYS;
        desired.channels = 2;
        desired.samples = 1024;
        desired.callback = callback;
        desired.userdata = this;
        device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (!device) {
            if (owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw std::runtime_error("Opening native audio: " + std::string(SDL_GetError()));
        }
        SDL_PauseAudioDevice(device, 0);
    }
    ~Impl() {
        if (device) SDL_CloseAudioDevice(device);
        if (owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    static void callback(void* userdata, Uint8* stream, int size) noexcept {
        auto& self = *static_cast<Impl*>(userdata);
        mix_voices(self.voices,
                   {reinterpret_cast<float*>(stream), static_cast<size_t>(size) / sizeof(float)},
                   self.sound_master, self.movie_master);
    }
    void silent_advance() {
        auto now = this->now();
        double elapsed = std::chrono::duration<double>(now - silent_clock).count();
        silent_clock = now;
        for (auto& voice : voices) advance(voice, elapsed * sample_rate);
    }
    Handle allocate_handle() {
        return allocate_media_handle(next_handle);
    }
    Voice* find(Handle handle) {
        for (auto& voice : voices) if (voice.handle == handle) return &voice;
        return nullptr;
    }
    void gains(Voice& voice) {
        double pan = voice.pan;
        double gain = voice.volume;
        if (voice.positional) {
            double x = voice.position.x - listener_position.x;
            double y = voice.position.y - listener_position.y;
            double z = voice.position.z - listener_position.z;
            double distance = std::sqrt(x*x + y*y + z*z);
            gain *= std::max(0.0, 1.0 - distance / voice.range);
            if (distance > 0) {
                double angle = listener_pan * (3.14159265358979323846 / 180);
                pan = (x * std::sin(angle) - y * std::cos(angle)) / distance;
            }
        }
        pan = std::clamp(pan, -1.0, 1.0);
        voice.left = static_cast<float>(gain * (pan > 0 ? 1 - pan : 1));
        voice.right = static_cast<float>(gain * (pan < 0 ? 1 + pan : 1));
    }
    Handle start(const Audio& buffer, double volume, double pan, double pitch, bool loop,
                 bool positional = false, Vec3 position = {}, double range = 1000, bool movie_source = false) {
        if (!buffer.frames()) throw std::runtime_error("Cannot play empty audio");
        if (!std::isfinite(volume) || !std::isfinite(pan) || !std::isfinite(pitch) || pitch <= 0 ||
            !std::isfinite(range) || range <= 0)
            throw std::runtime_error("Invalid native sound parameters");
        Lock lock(*this);
        Handle handle = allocate_handle();
        Voice voice;
        voice.handle = handle; voice.buffer = &buffer; voice.volume = std::clamp(volume, 0.0, 100.0) / 100;
        voice.pan = std::clamp(pan, -100.0, 100.0) / 100; voice.pitch = pitch / 100;
        voice.loop = loop; voice.active = true; voice.positional = positional;
        voice.movie_source = movie_source;
        voice.position = position; voice.range = range;
        gains(voice);
        for (auto& existing : voices) if (!existing.active) { existing = voice; return handle; }
        voices.push_back(voice);
        return handle;
    }
    const Audio& sound(std::string_view name) {
        auto key = cache_key(name);
        auto found = sounds.find(key);
        if (found != sounds.end()) return *found->second;
        auto bytes = assets.read(name);
        auto decoded = std::make_unique<Audio>(audio(bytes, name, true));
        auto* result = decoded.get();
        sounds.emplace(std::move(key), std::move(decoded));
        return *result;
    }
};

Media::Media(Assets& assets, bool open_audio) : impl_(std::make_unique<Impl>(assets, open_audio)) {}
Media::~Media() = default;
void Media::advance_clock(double seconds) {
    if (!impl_->manual) {
        if (!impl_->device) impl_->silent_advance();
        impl_->manual = impl_->now();
    }
    *impl_->manual += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
}
void Media::real_clock() {
    if (!impl_->manual) return;
    if (!impl_->device) impl_->silent_advance();
    impl_->offset = *impl_->manual - Clock::now();
    impl_->manual.reset();
}
Media::Handle Media::sound_play(std::string_view name, double volume, double pan, double pitch, bool loop) {
    return impl_->start(impl_->sound(name), volume, pan, pitch, loop);
}
Media::Handle Media::sound_play_at(std::string_view name, Vec3 position, double volume, double range, bool loop) {
    return impl_->start(impl_->sound(name), volume, 0, 100, loop, true, position, range);
}
bool Media::sound_playing(Handle handle) const {
    Impl::Lock lock(*impl_);
    const auto* voice = impl_->find(handle);
    return voice && voice->active;
}
void Media::sound_stop(Handle handle) {
    Impl::Lock lock(*impl_);
    if (auto* voice = impl_->find(handle)) { voice->active = false; voice->buffer = nullptr; }
}
void Media::sound_stop_all() {
    Impl::Lock lock(*impl_);
    for (auto& voice : impl_->voices) if (!voice.movie_source) { voice.active = false; voice.buffer = nullptr; }
}
void Media::sound_tune(Handle handle, double volume, double pitch, double pan) {
    if (!std::isfinite(volume) || !std::isfinite(pitch) || !std::isfinite(pan) || pitch < 0)
        throw std::runtime_error("Invalid snd_tune parameters");
    Impl::Lock lock(*impl_);
    if (auto* voice = impl_->find(handle); voice && voice->active) {
        if (volume != 0) voice->volume = std::clamp(volume, 0.0, 100.0) / 100;
        if (pitch != 0) voice->pitch = pitch / 100;
        if (pan != 0) voice->pan = std::clamp(pan, -100.0, 100.0) / 100;
        impl_->gains(*voice);
    }
}
void Media::sound_position(Handle handle, Vec3 position) {
    Impl::Lock lock(*impl_);
    if (auto* voice = impl_->find(handle); voice && voice->active) {
        voice->position = position;
        impl_->gains(*voice);
    }
}
void Media::listener(Vec3 position, double pan_degrees) {
    Impl::Lock lock(*impl_);
    impl_->listener_position = position;
    impl_->listener_pan = pan_degrees;
    for (auto& voice : impl_->voices) if (voice.active && voice.positional) impl_->gains(voice);
}
void Media::master_volume(double sound_volume, double midi_volume) {
    float sound = master_gain(sound_volume), movie = master_gain(midi_volume);
    Impl::Lock lock(*impl_);
    impl_->sound_master = sound;
    impl_->movie_master = movie;
}
Media::Handle Media::movie_play(std::string_view name, double volume, bool loop) {
    auto movie = std::make_unique<Impl::Movie>();
    if (!movie->presented) throw std::bad_alloc();
    movie->name = name;
    movie->bytes = impl_->assets.read(name);
    movie->audio = audio(movie->bytes, name, false);
    movie->decoder = std::make_unique<Decoder>(movie->bytes, name, AVMEDIA_TYPE_VIDEO);
    if (!movie->decoder->has_stream() && !movie->audio.frames())
        throw std::runtime_error("Media contains neither video nor audio: " + std::string(name));
    movie->duration = std::max(movie->decoder->duration(), movie->audio.frames() / static_cast<double>(sample_rate));
    if (!std::isfinite(movie->duration) || movie->duration < 0)
        throw std::runtime_error("Invalid movie duration");
    if (movie->audio.frames() && movie->duration > movie->audio.frames() / static_cast<double>(sample_rate)) {
        double frames = std::ceil(movie->duration * sample_rate);
        if (frames > (double{1} * (size_t{1} << 27)))
            throw std::runtime_error("Movie soundtrack exceeds safe buffer size");
        movie->audio.samples.resize(static_cast<size_t>(frames) * 2, 0.0f);
        movie->duration = frames / sample_rate;
    }
    movie->loop = loop;
    movie->video_eof = !movie->decoder->has_stream();
    if (!movie->video_eof) {
        if (!movie->decoder->next()) throw std::runtime_error("Movie contains no decodable frame: " + std::string(name));
        movie->pending_time = std::max(0.0, movie->decoder->timestamp());
        movie->pending = movie->pending_time > 0;
        if (!movie->pending) {
            movie->frame_number = 1;
            pixels(movie->decoder->frame(), movie->image, movie->scale);
            movie->last_end = movie->decoder->frame_seconds();
        }
    }
    Handle handle = impl_->allocate_handle();
    auto* stored = movie.get();
    impl_->movies.emplace(handle, std::move(movie));
    try {
        stored->start = impl_->now();
        if (stored->audio.frames())
            stored->audio_handle = impl_->start(stored->audio, volume, 0, 100, loop, false, {}, 1000, true);
        stored->active = true;
    } catch (...) { impl_->movies.erase(handle); throw; }
    return handle;
}
bool Media::movie_playing(Handle handle) const {
    auto found = impl_->movies.find(handle);
    if (found == impl_->movies.end() || !found->second->active) return false;
    const auto& movie = *found->second;
    if (movie.loop) return true;
    double time = std::chrono::duration<double>(impl_->now() - movie.start).count();
    double end = std::max(movie.duration, movie.last_end);
    return !((movie.duration > 0 || movie.video_eof) && time >= end &&
             (!movie.audio_handle || !sound_playing(movie.audio_handle)));
}
double Media::movie_status(Handle handle) const {
    if (!movie_playing(handle)) return 0;
    return static_cast<double>(std::max(uint64_t{1}, impl_->movies.at(handle)->frame_number));
}
void Media::movie_stop(Handle handle) {
    if (!handle) {
        for (const auto& [id, movie] : impl_->movies) if (movie->active) movie_stop(id);
        return;
    }
    auto found = impl_->movies.find(handle);
    if (found == impl_->movies.end()) return;
    auto& movie = *found->second;
    if (movie.audio_handle) sound_stop(movie.audio_handle);
    movie.active = false;
    movie.decoder.reset();
    movie.presented.reset();
    movie.scale.reset();
    movie.audio = {};
    movie.bytes = {};
}
const Image* Media::movie_frame(Handle handle) const {
    auto found = impl_->movies.find(handle);
    if (found == impl_->movies.end() || !found->second->image.width) return nullptr;
    return &found->second->image;
}
void Media::update() {
    for (auto& [handle, owned] : impl_->movies) {
        auto& movie = *owned;
        if (!movie.active) continue;
        double time = std::chrono::duration<double>(impl_->now() - movie.start).count();
        // For a soundtrack, use the mixer's consumed samples as the video master clock.
        if (movie.audio_handle) {
            Impl::Lock lock(*impl_);
            if (auto* voice = impl_->find(movie.audio_handle); voice && voice->active)
                time = voice->played_frames / sample_rate;
        }
        if (movie.loop && movie.duration > 0) {
            uint64_t cycle = static_cast<uint64_t>(time / movie.duration);
            time = std::fmod(time, movie.duration);
            if (cycle != movie.cycle && movie.decoder->has_stream()) {
                movie.decoder = std::make_unique<Decoder>(movie.bytes, movie.name, AVMEDIA_TYPE_VIDEO);
                movie.pending = false; movie.video_eof = false; movie.last_end = 0; movie.frame_number = 0;
            }
            movie.cycle = cycle;
        }
        bool changed = false;
        while (!movie.video_eof) {
            if (!movie.pending) {
                if (!movie.decoder->next()) {
                    movie.video_eof = true;
                    if (movie.duration <= 0) movie.duration = movie.last_end;
                    break;
                }
                double stamp = movie.decoder->timestamp();
                movie.pending_time = stamp >= 0 ? stamp : movie.last_end;
                movie.pending = true;
            }
            if (movie.pending_time > time) break;
            ++movie.frame_number;
            movie.last_end = movie.pending_time + movie.decoder->frame_seconds();
            av_frame_unref(movie.presented.get());
            av_frame_move_ref(movie.presented.get(), movie.decoder->frame());
            changed = true;
            movie.pending = false;
        }
        if (changed) pixels(movie.presented.get(), movie.image, movie.scale);
        if (!movie.loop && movie.video_eof && time >= std::max(movie.duration, movie.last_end) &&
            (!movie.audio_handle || !sound_playing(movie.audio_handle))) movie_stop(handle);
    }
}

} // namespace k22

#ifdef K22_MEDIA_SELF_CHECK
#undef NDEBUG // Release self-check must still assert; <cassert> re-evaluates NDEBUG on each inclusion.
#include <cassert>
#include <fstream>
#include <thread>
#include <unistd.h>
int main(int argc, char** argv) {
    auto little = [](std::vector<uint8_t>& bytes, size_t offset, uint32_t value, size_t width) {
        for (size_t i = 0; i < width; ++i) bytes.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
    };
    std::vector<uint8_t> bmp(62);
    bmp[0] = 'B'; bmp[1] = 'M';
    little(bmp, 2, 62, 4); little(bmp, 10, 54, 4); little(bmp, 14, 40, 4);
    little(bmp, 18, 2, 4); little(bmp, 22, 1, 4);
    little(bmp, 26, 1, 2); little(bmp, 28, 24, 2); little(bmp, 34, 8, 4);
    bmp[56] = 255; bmp[58] = 255;
    auto image = k22::decode_image(bmp, "boundary.bmp");
    assert(image.width == 2 && image.height == 1 && image.rgba.size() == 8);
    assert(image.sourceDepth == 24 && !image.nativeTextureAlpha);
    auto bmp16 = bmp;
    bmp16.resize(58);
    little(bmp16, 2, 58, 4); little(bmp16, 28, 16, 2); little(bmp16, 34, 4, 4);
    little(bmp16, 54, 0x7c00, 2); little(bmp16, 56, 0x03e0, 2);
    auto image16 = k22::decode_image(bmp16, "depth16.bmp");
    assert(image16.width == 2 && image16.height == 1 && image16.sourceDepth == 16);
    assert(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[3] == 255);
    assert(image.rgba[4] == 0 && image.rgba[5] == 255 && image.rgba[7] == 255);
    std::vector<uint8_t> pcx(136);
    pcx[0] = 10; pcx[1] = 5; pcx[2] = 1; pcx[3] = 8;
    little(pcx, 8, 1, 2); little(pcx, 12, 72, 2); little(pcx, 14, 72, 2);
    pcx[65] = 3; little(pcx, 66, 2, 2); little(pcx, 68, 1, 2);
    pcx[128] = 0xc1; pcx[129] = 255; pcx[132] = 0xc1; pcx[133] = 255;
    auto image24 = k22::decode_image(pcx, "planes24.pcx");
    assert(image24.width == 2 && image24.height == 1 && image24.sourceDepth == 24 && !image24.nativeTextureAlpha);
    assert(image24.rgba[0] == 255 && image24.rgba[5] == 255);
    std::vector<uint8_t> dds(128);
    std::memcpy(dds.data(), "DDS ", 4);
    little(dds, 4, 124, 4); little(dds, 76, 32, 4);
    little(dds, 80, 0x40, 4); little(dds, 88, 16, 4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr) == 16);
    little(dds, 88, 32, 4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr) == 32);
    little(dds, 80, 4, 4); std::memcpy(dds.data() + 84, "DXT1", 4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr) == 0);
    bool textureAlpha = true;
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 0 && !textureAlpha);
    for(char format:{'2','3','4','5'}) {
        dds[87]=format;
        assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 0 && textureAlpha);
    }
    little(dds,84,113,4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 0 && textureAlpha);
    little(dds,84,36,4); // Also selects21 under the native decoded-texture policy.
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 0 && textureAlpha);
    little(dds,80,0x41,4);little(dds,88,32,4);
    little(dds,92,0x00ff0000,4);little(dds,96,0x0000ff00,4);
    little(dds,100,0x000000ff,4);little(dds,104,0xff000000,4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 32 && textureAlpha);
    little(dds,104,0,4); // X8R8G8B8 source still converts to selected21.
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 32 && textureAlpha);
    little(dds,92,0xff,4);little(dds,100,0x00ff0000,4);little(dds,104,0xff000000,4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 32 && textureAlpha);
    little(dds,88,16,4);little(dds,92,0x0f00,4);little(dds,96,0x00f0,4);
    little(dds,100,0x000f,4);little(dds,104,0xf000,4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 16 && textureAlpha);
    little(dds,80,0x40,4);little(dds,92,0xf800,4);little(dds,96,0x07e0,4);
    little(dds,100,0x001f,4);little(dds,104,0,4);
    assert(k22::image_depth(dds, AV_CODEC_ID_DDS, nullptr, &textureAlpha) == 16 && textureAlpha);
    // Exercise the decoder's actual header-to-Image transfer, not just metadata.
    little(dds,8,0x81007,4);little(dds,12,4,4);little(dds,16,4,4);little(dds,108,0x1000,4);
    little(dds,80,5,4);std::memcpy(dds.data()+84,"DXT1",4);little(dds,20,8,4);
    dds.resize(136);dds[130]=255;dds[131]=255;
    std::fill(dds.begin()+132,dds.end(),255); // c0<c1, all selector3 transparent.
    const auto dxt1=k22::decode_image(dds,"transparent.dds");
    assert(dxt1.width==4 && dxt1.height==4 && !dxt1.nativeTextureAlpha && dxt1.sourceDepth==0);
    for(size_t pixel=3;pixel<dxt1.rgba.size();pixel+=4)assert(dxt1.rgba[pixel]==0);
    dds.resize(144);std::fill(dds.begin()+128,dds.end(),0);
    std::fill(dds.begin()+128,dds.begin()+136,255);
    std::memcpy(dds.data()+84,"DXT3",4);little(dds,20,16,4);
    const auto dxt3=k22::decode_image(dds,"opaque.dds");
    assert(dxt3.width==4 && dxt3.height==4 && dxt3.nativeTextureAlpha && dxt3.sourceDepth==0);
    for(size_t pixel=3;pixel<dxt3.rgba.size();pixel+=4)assert(dxt3.rgba[pixel]==255);
    bool rejected = false;
    try { k22::decode_image({}, "empty.bmp"); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    std::vector<uint8_t> wav(44 + 480 * 2);
    std::memcpy(wav.data(), "RIFF", 4); std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    little(wav, 4, static_cast<uint32_t>(wav.size() - 8), 4); little(wav, 16, 16, 4);
    little(wav, 20, 1, 2); little(wav, 22, 1, 2);
    little(wav, 24, 48000, 4); little(wav, 28, 96000, 4);
    little(wav, 32, 2, 2); little(wav, 34, 16, 2);
    std::memcpy(wav.data() + 36, "data", 4); little(wav, 40, 960, 4);
    for (size_t i = 0; i < 480; ++i) little(wav, 44 + i * 2, i < 240 ? 16384 : 49152, 2);
    auto sound = k22::audio(wav, "boundary.wav", true);
    assert(sound.frames() == 480 && sound.samples.front() > 0 && sound.samples.back() < 0);
    {
        // Scenario media clock: a headless voice ends by advance_clock time only, never by elapsed wall time.
        const auto dir = std::filesystem::temp_directory_path() / ("k22-media-clock-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "clock.wav", std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), std::streamsize(wav.size()));
        {
            k22::Assets store(dir);
            k22::Media media(store, false);
            media.advance_clock(0);
            const auto voice = media.sound_play("clock.wav"); // 480 frames = 10 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            assert(media.sound_playing(voice));
            media.advance_clock(0.005);
            assert(media.sound_playing(voice));
            media.advance_clock(0.006);
            assert(!media.sound_playing(voice));
            const auto resumed = media.sound_play("clock.wav");
            media.real_clock(); // wall time resumes from the manual point
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            assert(!media.sound_playing(resumed));
        }
        std::filesystem::remove_all(dir);
    }
    // The original OggDS asset supplies the bitstream; only RIFF layout is varied.
    assert(argc <= 2);
    if (argc == 2) {
        const auto path = std::filesystem::canonical(argv[1]);
        k22::Assets assets(path.parent_path());
        const auto name = path.filename().string();
        const auto wrapped = assets.read(name);
        const auto payload = k22::media_payload(wrapped);
        assert(wrapped.size() == 18382 && payload.size() == 18316);
        k22::Decoder original(wrapped, name, AVMEDIA_TYPE_AUDIO);
        assert(original.has_stream() && original.codec()->codec_id == AV_CODEC_ID_VORBIS &&
               original.codec()->sample_rate == 22050 && original.codec()->ch_layout.nb_channels == 2);
        // The outer WAV fact is not the Ogg granule duration. Compare the
        // genuine source frames/timestamps with metadata and resampled audio.
        const double duration = original.duration();
        assert(std::isfinite(duration) && duration > 0);
        uint64_t source_frames = 0;
        int largest_frame = 0;
        double start = 0;
        while (original.next()) {
            if (!source_frames) start = std::max(0.0, original.timestamp());
            source_frames += original.frame()->nb_samples;
            largest_frame = std::max(largest_frame, original.frame()->nb_samples);
        }
        const double decoded_duration = start + source_frames / 22050.0;
        assert(source_frames && largest_frame > 0);
        assert(std::abs(decoded_duration - duration) <= largest_frame / 22050.0);
        const auto decoded = k22::audio(wrapped, name, true);
        const auto inner = k22::audio(payload, "original-data.ogg", true);
        assert(std::abs(decoded.frames() / 48000.0 - decoded_duration) <= 2.0 / 48000);
        assert(decoded.samples == inner.samples);
        std::array<double, 2> energy{};
        for (size_t i = 0; i < decoded.samples.size(); ++i) {
            assert(std::isfinite(decoded.samples[i]));
            energy[i % 2] += double(decoded.samples[i]) * decoded.samples[i];
        }
        for (double channel : energy) assert(channel / decoded.frames() > 1e-8);

        auto chunk = [&](std::vector<uint8_t>& bytes, const char* id, std::span<const uint8_t> data) {
            const size_t start = bytes.size();
            bytes.resize(start + 8);
            std::memcpy(bytes.data() + start, id, 4);
            little(bytes, start + 4, static_cast<uint32_t>(data.size()), 4);
            bytes.insert(bytes.end(), data.begin(), data.end());
            if (data.size() & 1) bytes.push_back(0);
            return start;
        };
        const std::array<uint8_t, 3> odd{1, 2, 3};
        for (bool data_first : {false, true}) {
            std::vector<uint8_t> reordered(wrapped.begin(), wrapped.begin() + 12);
            chunk(reordered, "JUNK", odd);
            const auto fmt = std::span<const uint8_t>(wrapped).subspan(20, 26);
            size_t fmt_start{}, data_start{};
            if (data_first) {
                data_start = chunk(reordered, "data", payload);
                fmt_start = chunk(reordered, "fmt ", fmt);
            } else {
                fmt_start = chunk(reordered, "fmt ", fmt);
                data_start = chunk(reordered, "data", payload);
            }
            chunk(reordered, "JUNK", payload.first(4));
            little(reordered, 4, static_cast<uint32_t>(reordered.size() - 8), 4);
            // Trailing physical bytes are outside the declared RIFF extent.
            reordered.insert(reordered.end(), odd.begin(), odd.end());
            const auto clipped = k22::media_payload(reordered);
            assert(clipped.size() == payload.size() && std::equal(clipped.begin(), clipped.end(), payload.begin()));
            assert(k22::audio(reordered, name, true).samples == decoded.samples);
            auto malformed = [&](std::vector<uint8_t> bytes) {
                bool failed = false;
                try { k22::audio(bytes, name, true); }
                catch (const std::runtime_error&) { failed = true; }
                assert(failed);
            };
            auto bad = reordered;
            little(bad, 4, UINT32_MAX, 4); malformed(bad);
            bad = reordered;
            little(bad, data_start + 4, UINT32_MAX, 4); malformed(bad);
            bad = reordered;
            little(bad, fmt_start + 4, 2, 4); malformed(bad);
            bad = reordered;
            bad.resize(bad.size() - 4); malformed(bad);
            bad = reordered;
            // An odd final chunk without its required pad is malformed.
            bad.resize(bad.size() - 3);
            chunk(bad, "JUNK", odd);
            bad.pop_back();
            little(bad, 4, static_cast<uint32_t>(bad.size() - 8), 4);
            malformed(bad);
            bad = reordered;
            // A partial trailing chunk header is not encoded-audio padding.
            bad.resize(bad.size() - 3);
            bad.push_back(1);
            little(bad, 4, static_cast<uint32_t>(bad.size() - 8), 4);
            malformed(bad);
            bad = reordered;
            for (uint32_t tag : {0x674eu, 0x6752u, 0x676eu, 0x6772u}) {
                little(bad, fmt_start + 8, tag, 2);
                assert(k22::media_payload(bad).size() == bad.size());
            }
            // Kaynak.k22 otokapi_acil/zorlan.ogg carry Vorbis ACM mode 1+ (0x676f) around OggS.
            for (uint32_t tag : {0x6750u, 0x6751u, 0x676fu, 0x6770u, 0x6771u}) {
                little(bad, fmt_start + 8, tag, 2);
                assert(k22::audio(bad, name, true).samples == decoded.samples);
            }
            bad = reordered;
            bad[data_start + 8] = 'X';
            assert(k22::media_payload(bad).size() == bad.size());
        }
        assert(k22::media_payload(wav).size() == wav.size());
        assert(k22::media_payload(payload).size() == payload.size());

        k22::Voice real_voice;
        real_voice.buffer = &decoded; real_voice.active = true; real_voice.pitch = 1;
        std::vector<float> output(decoded.samples.size());
        k22::mix_voices({&real_voice, 1}, output, 1, 1);
        for (size_t i = 0; i < output.size(); ++i)
            assert(output[i] == std::clamp(decoded.samples[i], -1.0f, 1.0f));
        assert(!real_voice.active);

        // Assets owns the immutable file; sound's temporary encoded vector dies
        // in sound_play, whereas each movie retains its own encoded vector.
        k22::Media media(assets, false);
        const auto once = media.sound_play(name);
        const auto loop = media.sound_play(name, 100, 0, 100, true);
        const auto movie_once = media.movie_play(name);
        const auto movie_loop = media.movie_play(name, 100, true);
        assert(media.sound_playing(once) && media.sound_playing(loop));
        assert(media.movie_status(movie_once) == 1 && media.movie_status(movie_loop) == 1);
        assert(!media.movie_frame(movie_once) && !media.movie_frame(movie_loop));
        SDL_Delay(static_cast<Uint32>(std::ceil((std::max(duration, decoded_duration) + 0.1) * 1000)));
        media.update();
        assert(!media.sound_playing(once) && media.sound_playing(loop));
        assert(media.movie_status(movie_once) == 0 && media.movie_status(movie_loop) == 1);
        media.sound_stop(loop);
        media.movie_stop(movie_loop);
        assert(!media.sound_playing(loop) && media.movie_status(movie_loop) == 0);
    }
    k22::Voice voice;
    voice.buffer = &sound; voice.active = true; voice.pitch = 2;
    k22::advance(voice, 239); assert(voice.active && voice.cursor == 478);
    k22::advance(voice, 1); assert(!voice.active);
    voice.active = true; voice.loop = true; voice.cursor = 0;
    k22::advance(voice, 241); assert(voice.active && voice.cursor == 2);
    uint32_t next_handle = (uint32_t{1} << 21) - 1;
    voice.handle = k22::allocate_media_handle(next_handle);
    assert(voice.handle == (uint32_t{1} << 21) - 1);
    assert(next_handle == (uint32_t{1} << 21));
    rejected = false;
    try { k22::allocate_media_handle(next_handle); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected && next_handle == (uint32_t{1} << 21));
    assert(voice.active && voice.loop && voice.handle == (uint32_t{1} << 21) - 1 &&
           voice.cursor == 2 && voice.buffer == &sound);
    assert(k22::master_gain(-1) == 0 && k22::master_gain(150) == 1);
    rejected = false;
    try { k22::master_gain(std::numeric_limits<double>::infinity()); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    voice.cursor = 0; voice.pitch = 1;
    std::array<k22::Voice, 2> voices{voice, voice};
    voices[1].movie_source = true;
    std::array<float, 4> mixed{};
    k22::mix_voices(voices, mixed, 0, 1);
    assert(std::abs(mixed[0] - sound.samples[0]) < 0.000001f);
    k22::mix_voices(voices, mixed, 1, 0);
    assert(std::abs(mixed[0] - sound.samples[0]) < 0.000001f);
    k22::mix_voices(voices, mixed, 0.5f, 0.25f);
    assert(std::abs(mixed[0] - sound.samples[0] * 0.75f) < 0.000001f);
    k22::mix_voices(voices, mixed, 0, 0);
    for (float sample : mixed) assert(sample == 0);
    for (const auto& playing : voices)
        assert(playing.active && playing.cursor == 8 && playing.handle == voice.handle &&
               playing.volume == voice.volume && playing.pitch == voice.pitch);
}
#endif
