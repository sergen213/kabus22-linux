#include "setup.hpp"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <cstdint>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {
namespace fs = std::filesystem;
using namespace k22::setup;
constexpr const char* Version = "kabus22 native installer 1.0";
std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void interrupt(int) { interrupted.store(true, std::memory_order_relaxed); }

struct Options {
    Request request;
    bool unattended = false, help = false, version = false, check = false;
};
Options arguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") { options.help = true; continue; }
        if (flag == "--version") { options.version = true; continue; }
        if (flag == "--self-check") { options.check = true; continue; }
        if (flag == "--unattended") { options.unattended = true; continue; }
        fs::path* target = nullptr;
        if (flag == "--iso") target = &options.request.sourceIso;
        else if (flag == "--setup") target = &options.request.sourceSetup;
        else if (flag == "--install-dir") target = &options.request.installRoot;
        else if (flag == "--engine") target = &options.request.enginePayload;
        else if (flag == "--extractor") target = &options.request.extractor;
        else if (flag == "--patch") target = &options.request.optionalPatch;
        else throw std::runtime_error("Unknown option: " + flag);
        if (++i == argc || argv[i][0] == '\0') throw std::runtime_error("Missing value for " + flag);
        if (!target->empty()) throw std::runtime_error("Duplicate option: " + flag);
        *target = fs::absolute(argv[i]).lexically_normal();
    }
    if (!options.request.sourceIso.empty() && !options.request.sourceSetup.empty())
        throw std::runtime_error("Choose --iso or the developer --setup route, not both.");
    if (options.unattended && options.request.sourceIso.empty() && options.request.sourceSetup.empty())
        throw std::runtime_error("--unattended requires --iso GAME.iso or --setup setup.exe.");
    return options;
}
void argumentCheck() {
    auto parse = [](std::vector<std::string> values) {
        std::vector<char*> argv;
        for (auto& value : values) argv.push_back(value.data());
        return arguments(static_cast<int>(argv.size()), argv.data());
    };
    const auto result = parse({"kabus22-setup", "--iso", "media with spaces.iso", "--unattended"});
    if (!result.unattended || result.request.sourceIso.filename() != "media with spaces.iso")
        throw std::runtime_error("CLI path parsing regression");
    for (const auto& invalid : std::vector<std::vector<std::string>>{
            {"kabus22-setup", "--iso"}, {"kabus22-setup", "--unattended"},
            {"kabus22-setup", "--iso", "one.iso", "--setup", "setup.exe"},
            {"kabus22-setup", "--iso", "one.iso", "--iso", "two.iso"},
            {"kabus22-setup", "--unknown"}}) {
        bool rejected = false;
        try { (void)parse(invalid); } catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("CLI invalid argument regression");
    }
}
void defaults(Request& request) {
    const auto paths = defaultPaths();
    if (request.installRoot.empty()) request.installRoot = paths.installRoot;
    request.applicationsDir = paths.applicationsDir;
    request.desktopDir = paths.desktopDir;
    // Only executable siblings are implicit payloads; never search PATH or the working directory.
    const auto bundle = fs::read_symlink("/proc/self/exe").parent_path();
    if (request.enginePayload.empty()) request.enginePayload = bundle / "kabus22";
    if (request.extractor.empty()) request.extractor = bundle / "innoextract";
}

struct Pixels {
    int width = 0, height = 0, stride = 0;
    std::vector<unsigned char> rgba;
};
Pixels nativeArtwork(const fs::path& path) {
    constexpr int maxBytes = 8 * 1024 * 1024;
    constexpr std::int64_t maxPixels = 16 * 1024 * 1024;
    const auto failure = [](const char* action, int error) {
        char detail[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(error, detail, sizeof(detail));
        return std::runtime_error(std::string("Artwork: ") + action + ": " + detail);
    };
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Artwork: cannot open extracted JPEG");
    const auto length = input.tellg();
    if (length < std::streampos(4) || length > std::streampos(maxBytes))
        throw std::runtime_error("Artwork: JPEG size must be between 4 bytes and 8 MiB");
    const int bytes = static_cast<int>(static_cast<std::streamoff>(length));
    input.seekg(0);
    const auto freePacket = [](AVPacket* value) { av_packet_free(&value); };
    std::unique_ptr<AVPacket, decltype(freePacket)> packet(av_packet_alloc(), freePacket);
    if (!packet) throw std::runtime_error("Artwork: cannot allocate JPEG packet");
    int result = av_new_packet(packet.get(), bytes);
    if (result < 0) throw failure("cannot allocate padded JPEG bytes", result);
    if (!input.read(reinterpret_cast<char*>(packet->data), bytes) ||
        input.peek() != std::char_traits<char>::eof() || input.bad())
        throw std::runtime_error("Artwork: cannot read complete JPEG, or file changed during reading");
    if (packet->data[0] != 0xff || packet->data[1] != 0xd8)
        throw std::runtime_error("Artwork: extracted file is not a standalone JPEG");
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!decoder) throw std::runtime_error("Artwork: FFmpeg MJPEG decoder is unavailable");
    const auto freeCodec = [](AVCodecContext* value) { avcodec_free_context(&value); };
    std::unique_ptr<AVCodecContext, decltype(freeCodec)> context(avcodec_alloc_context3(decoder), freeCodec);
    if (!context) throw std::runtime_error("Artwork: cannot allocate JPEG decoder");
    context->thread_count = 1;
    context->max_pixels = maxPixels;
    context->err_recognition = AV_EF_EXPLODE;
    result = avcodec_open2(context.get(), decoder, nullptr);
    if (result < 0) throw failure("cannot open JPEG decoder", result);
    result = avcodec_send_packet(context.get(), packet.get());
    if (result < 0) throw failure("cannot decode JPEG packet", result);
    const auto freeFrame = [](AVFrame* value) { av_frame_free(&value); };
    std::unique_ptr<AVFrame, decltype(freeFrame)> frame(av_frame_alloc(), freeFrame);
    if (!frame) throw std::runtime_error("Artwork: cannot allocate decoded frame");
    result = avcodec_receive_frame(context.get(), frame.get());
    if (result < 0) throw failure("cannot receive decoded JPEG image", result);
    if (frame->width <= 0 || frame->height <= 0 || frame->format < 0 ||
        std::int64_t(frame->width) * frame->height > maxPixels)
        throw std::runtime_error("Artwork: invalid image dimensions or image exceeds 16 megapixels");
    Pixels pixels;
    pixels.width = frame->width;
    pixels.height = frame->height;
    if (pixels.width > 600 || pixels.height > 240) {
        if (std::int64_t(frame->width) * 240 >= std::int64_t(frame->height) * 600) {
            pixels.width = 600;
            pixels.height = std::max(1, int(std::int64_t(frame->height) * 600 / frame->width));
        } else {
            pixels.height = 240;
            pixels.width = std::max(1, int(std::int64_t(frame->width) * 240 / frame->height));
        }
    }
    pixels.stride = pixels.width * 4;
    pixels.rgba.resize(std::size_t(pixels.stride) * pixels.height);
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(
        sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            pixels.width, pixels.height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr),
        &sws_freeContext);
    if (!scaler) throw std::runtime_error("Artwork: cannot create JPEG-to-RGBA scaler");
    unsigned char* destination[4] = {pixels.rgba.data(), nullptr, nullptr, nullptr};
    const int strides[4] = {pixels.stride, 0, 0, 0};
    result = sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, destination, strides);
    if (result < 0) throw failure("cannot convert JPEG to RGBA", result);
    if (result != pixels.height) throw std::runtime_error("Artwork: incomplete JPEG-to-RGBA conversion");
    return pixels;
}

struct PreviewScratch {
    fs::path root;
    PreviewScratch() {
        GError* error = nullptr;
        char* directory = g_dir_make_tmp("kabus22-artwork-XXXXXX", &error);
        if (!directory) {
            const std::string message = error ? error->message : "Cannot create artwork scratch directory";
            if (error) g_error_free(error);
            throw std::runtime_error(message);
        }
        root = directory;
        g_free(directory);
    }
    ~PreviewScratch() { std::error_code error; fs::remove_all(root, error); }
};
void accessible(GtkWidget* widget, const char* name, const char* description = nullptr) {
    AtkObject* object = gtk_widget_get_accessible(widget);
    atk_object_set_name(object, name);
    if (description) atk_object_set_description(object, description);
}
std::string displayText(const std::string& text) {
    char* valid = g_utf8_make_valid(text.data(), static_cast<gssize>(text.size()));
    std::string result = valid;
    g_free(valid);
    return result;
}
void labelText(GtkWidget* label, const std::string& text) {
    const auto valid = displayText(text);
    gtk_label_set_text(GTK_LABEL(label), valid.c_str());
}

class Installer {
    Request request_;
    Result result_;
    GtkWidget *window_, *fields_, *source_, *destination_, *patch_, *image_, *status_, *errors_,
              *progress_, *preview_, *install_, *cancel_, *launch_, *close_;
    GtkFileChooserNative* chooser_ = nullptr;
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> cancelled_{false};
    std::string stage_, error_;
    std::uint64_t done_ = 0, total_ = 0;
    Pixels pixels_;
    bool finished_ = false, dirty_ = false, succeeded_ = false, busy_ = false,
         installed_ = false, closing_ = false;
    guint timer_ = 0;
    bool loose_;

    GtkWidget* field(GtkWidget* grid, int row, const char* mnemonic, const fs::path& value,
                     const char* description, GCallback browse) {
        auto* label = gtk_label_new_with_mnemonic(mnemonic);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        auto* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
        gtk_widget_set_hexpand(entry, TRUE);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
        gtk_widget_set_tooltip_text(entry, description);
        accessible(entry, mnemonic + 1, description);
        auto* button = gtk_button_new_with_label("Browse…");
        accessible(button, (std::string("Browse: ") + (mnemonic + 1)).c_str(), description);
        g_signal_connect(button, "clicked", browse, this);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), button, 2, row, 1, 1);
        return entry;
    }
    GtkWidget* button(GtkWidget* box, const char* mnemonic, GCallback callback) {
        auto* widget = gtk_button_new_with_mnemonic(mnemonic);
        g_signal_connect(widget, "clicked", callback, this);
        gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
        return widget;
    }
    void setControls() {
        const bool editable = !busy_ && !installed_;
        gtk_widget_set_sensitive(fields_, editable);
        gtk_widget_set_sensitive(preview_, editable && *gtk_entry_get_text(GTK_ENTRY(source_)));
        gtk_widget_set_sensitive(install_, editable && *gtk_entry_get_text(GTK_ENTRY(source_)));
        gtk_widget_set_sensitive(cancel_, busy_ && !cancelled_.load());
        gtk_widget_set_sensitive(launch_, installed_ && !busy_);
        gtk_widget_set_sensitive(close_, !closing_);
    }
    void report(const std::string& message) { labelText(errors_, message); }
    void choose(GtkWidget* entry, GtkFileChooserAction action, bool source, bool parent = false) {
        auto* dialog = gtk_file_chooser_native_new(source ? "Select original Kabus22 media" :
                parent ? "Select parent for the new kabus22 directory" : "Select optional Kabus22Patch community extras",
                GTK_WINDOW(window_), action, "Select", "Cancel");
        chooser_ = dialog;
        if (source) {
            auto* filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, loose_ ? "Original setup.exe (developer media)" : "ISO images");
            gtk_file_filter_add_pattern(filter, loose_ ? "*.exe" : "*.iso");
            gtk_file_filter_add_pattern(filter, loose_ ? "*.EXE" : "*.ISO");
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
            auto* all = gtk_file_filter_new();
            gtk_file_filter_set_name(all, "All files");
            gtk_file_filter_add_pattern(all, "*");
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);
        }
        const fs::path current = gtk_entry_get_text(GTK_ENTRY(entry));
        if (!current.empty()) {
            const auto folder = (source || parent) ? current.parent_path() : current;
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), folder.c_str());
        }
        bool selectedSource = false;
        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !closing_) {
            char* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (selected) {
                fs::path path = selected;
                g_free(selected);
                if (parent) path /= "kabus22";
                gtk_entry_set_text(GTK_ENTRY(entry), path.c_str());
                selectedSource = source;
            }
        }
        g_object_unref(dialog);
        chooser_ = nullptr;
        if (closing_) { close(); return; }
        if (selectedSource) begin(true);
    }
    void begin(bool preview) {
        if (busy_ || installed_) return;
        try {
            const fs::path source = gtk_entry_get_text(GTK_ENTRY(source_));
            if (source.empty()) throw std::runtime_error("Select your original ISO image first.");
            Request input = request_;
            if (loose_) input.sourceSetup = fs::absolute(source).lexically_normal();
            else input.sourceIso = fs::absolute(source).lexically_normal();
            input.installRoot = gtk_entry_get_text(GTK_ENTRY(destination_));
            if (input.installRoot.empty()) throw std::runtime_error("Choose an installation directory.");
            input.installRoot = fs::absolute(input.installRoot).lexically_normal();
            input.optionalPatch = gtk_entry_get_text(GTK_ENTRY(patch_));
            if (!input.optionalPatch.empty()) input.optionalPatch = fs::absolute(input.optionalPatch).lexically_normal();
            cancelled_ = false;
            busy_ = true;
            labelText(errors_, "");
            labelText(status_, preview ? "Reading original artwork from your media…" : "Preparing native installation…");
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 0);
            {
                std::lock_guard lock(mutex_);
                finished_ = succeeded_ = dirty_ = false;
                stage_.clear(); error_.clear(); done_ = total_ = 0;
                pixels_ = {};
            }
            if (preview) gtk_image_clear(GTK_IMAGE(image_));
            setControls();
            worker_ = std::thread([this, input = std::move(input), preview] {
                const auto progress = [this](const std::string& stage, std::uint64_t done, std::uint64_t total) {
                    std::lock_guard lock(mutex_);
                    stage_ = stage; done_ = done; total_ = total; dirty_ = true;
                    return !cancelled_.load(std::memory_order_relaxed) && !interrupted.load(std::memory_order_relaxed);
                };
                try {
                    if (preview) {
                        PreviewScratch scratch;
                        const auto source = loose_ ? input.sourceSetup : input.sourceIso;
                        const auto artwork = previewArtwork(source, input.extractor, scratch.root / "media", progress);
                        if (cancelled_.load() || interrupted.load()) throw Cancelled();
                        auto pixels = nativeArtwork(artwork);
                        if (cancelled_.load() || interrupted.load()) throw Cancelled();
                        std::lock_guard lock(mutex_);
                        pixels_ = std::move(pixels);
                        stage_ = "Original artwork loaded from your media.";
                    } else {
                        auto result = install(input, progress);
                        std::lock_guard lock(mutex_);
                        result_ = std::move(result);
                        succeeded_ = true;
                        stage_ = "Installation complete. Native game and desktop shortcuts are ready.";
                    }
                } catch (const Cancelled& error) {
                    std::lock_guard lock(mutex_);
                    stage_ = preview ? "Artwork reading cancelled." : "Installation cancelled.";
                    error_ = error.what();
                } catch (const std::exception& error) {
                    std::lock_guard lock(mutex_);
                    stage_ = preview ? "Original artwork preview failed. Installation has not started." : "Installation failed.";
                    error_ = error.what();
                } catch (...) {
                    std::lock_guard lock(mutex_);
                    stage_ = "Operation failed.";
                    error_ = "Unexpected native installer error.";
                }
                std::lock_guard lock(mutex_);
                finished_ = dirty_ = true;
            });
        } catch (const std::exception& error) {
            busy_ = false;
            report(error.what());
            setControls();
        }
    }
    void cancel() {
        if (!busy_) return;
        cancelled_ = true;
        labelText(status_, "Cancelling safely; waiting for the current operation to stop…");
        setControls();
    }
    void close() {
        if (chooser_) {
            closing_ = true;
            gtk_native_dialog_hide(GTK_NATIVE_DIALOG(chooser_));
            g_signal_emit_by_name(chooser_, "response", GTK_RESPONSE_CANCEL);
        } else if (busy_) { closing_ = true; cancel(); }
        else gtk_widget_destroy(window_);
    }
    gboolean tick() {
        if (interrupted.load()) close();
        if (!busy_) return G_SOURCE_CONTINUE;
        bool finished;
        {
            std::lock_guard lock(mutex_);
            finished = finished_;
            if (dirty_) {
                if (!cancelled_.load() || finished) labelText(status_, stage_);
                labelText(errors_, error_);
                const double fraction = total_ ? std::clamp(static_cast<double>(done_) / static_cast<double>(total_), 0.0, 1.0) : 0;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), fraction);
                gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_), total_ ?
                    (std::to_string(static_cast<int>(fraction * 100)) + "%").c_str() : "Working…");
                dirty_ = false;
            }
            if (!total_ && !finished) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_));
        }
        if (!finished) return G_SOURCE_CONTINUE;
        worker_.join();
        busy_ = false;
        installed_ = succeeded_;
        if (!pixels_.rgba.empty()) {
            auto pixels = std::make_unique<Pixels>(std::move(pixels_));
            auto* pixbuf = gdk_pixbuf_new_from_data(pixels->rgba.data(), GDK_COLORSPACE_RGB, TRUE, 8,
                pixels->width, pixels->height, pixels->stride,
                [](guchar*, gpointer owner) { delete static_cast<Pixels*>(owner); }, pixels.get());
            if (pixbuf) {
                pixels.release();
                gtk_image_set_from_pixbuf(GTK_IMAGE(image_), pixbuf);
                g_object_unref(pixbuf);
                accessible(image_, "Original Kabus22 logo", "Decoded from the selected original game media.");
            } else report("Cannot display the decoded original artwork.");
        }
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_), installed_ ? "Complete" : "Idle");
        if (installed_) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1);
        setControls();
        if (closing_) gtk_widget_destroy(window_);
        else gtk_widget_grab_focus(installed_ ? launch_ : install_);
        return G_SOURCE_CONTINUE;
    }
public:
    explicit Installer(Request request) : request_(std::move(request)), loose_(!request_.sourceSetup.empty()) {
        window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window_), "Kabus22 — Native Linux Setup");
        gtk_window_set_default_size(GTK_WINDOW(window_), 780, 620);
        gtk_container_set_border_width(GTK_CONTAINER(window_), 20);
        auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_add(GTK_CONTAINER(window_), content);
        auto* heading = gtk_label_new("Kabus22 — Native Linux Setup");
        gtk_box_pack_start(GTK_BOX(content), heading, FALSE, FALSE, 0);
        image_ = gtk_image_new();
        gtk_widget_set_size_request(image_, -1, 224);
        accessible(image_, "Original artwork preview", "Choose your own game media to load the original logo. No game artwork is bundled.");
        gtk_box_pack_start(GTK_BOX(content), image_, FALSE, FALSE, 0);
        fields_ = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(fields_), 10);
        gtk_grid_set_column_spacing(GTK_GRID(fields_), 10);
        gtk_box_pack_start(GTK_BOX(content), fields_, FALSE, FALSE, 0);
        source_ = field(fields_, 0, loose_ ? "_Original setup (developer):" : "_ISO image:",
            loose_ ? request_.sourceSetup : request_.sourceIso, "Select the complete original Kabus22 ISO image. The media is never executed.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->source_, GTK_FILE_CHOOSER_ACTION_OPEN, true); }));
        destination_ = field(fields_, 1, "_Install directory:", request_.installRoot,
            "A new directory is required. Browse selects a parent and adds kabus22; edit the path to choose another name.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->destination_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, false, true); }));
        patch_ = field(fields_, 2, "_Community extras (optional):", request_.optionalPatch,
            "Optional, separately obtained Kabus22Patch directory for its extras. Not required for native original-campaign widescreen; leave empty for original ISO data.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->patch_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, false); }));
        auto* note = gtk_label_new("Original-campaign widescreen is included natively; no patch folder is needed.\nInstalls for your user only. Existing installations are not overwritten.\nWindows executables are never launched; original ISO artwork is retained without extras.");
        gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
        gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 0);
        progress_ = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_), TRUE);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_), "Idle");
        accessible(progress_, "Installation progress");
        gtk_box_pack_start(GTK_BOX(content), progress_, FALSE, FALSE, 0);
        status_ = gtk_label_new("Choose your original ISO image to begin.");
        gtk_label_set_line_wrap(GTK_LABEL(status_), TRUE);
        gtk_label_set_selectable(GTK_LABEL(status_), TRUE);
        accessible(status_, "Installer status");
        gtk_box_pack_start(GTK_BOX(content), status_, FALSE, FALSE, 0);
        errors_ = gtk_label_new("");
        gtk_label_set_line_wrap(GTK_LABEL(errors_), TRUE);
        gtk_label_set_selectable(GTK_LABEL(errors_), TRUE);
        accessible(errors_, "Installer error details");
        gtk_box_pack_start(GTK_BOX(content), errors_, TRUE, TRUE, 0);
        auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_end(GTK_BOX(content), actions, FALSE, FALSE, 0);
        preview_ = button(actions, "Read _artwork", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->begin(true); }));
        install_ = button(actions, "_Install", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->begin(false); }));
        cancel_ = button(actions, "_Cancel", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->cancel(); }));
        launch_ = button(actions, "_Launch game", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<Installer*>(data);
            try { const int pid = launch(self->result_); labelText(self->status_, "Native game launched (PID " + std::to_string(pid) + ")."); }
            catch (const std::exception& error) { self->report(error.what()); }
        }));
        close_ = button(actions, "Cl_ose", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->close(); }));
        g_signal_connect(source_, "changed", G_CALLBACK(+[](GtkEditable*, gpointer data) {
            auto* self = static_cast<Installer*>(data); gtk_image_clear(GTK_IMAGE(self->image_)); self->setControls();
        }), this);
        g_signal_connect(window_, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean { static_cast<Installer*>(data)->close(); return TRUE; }), this);
        g_signal_connect(window_, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { gtk_main_quit(); }), nullptr);
        g_signal_connect(window_, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* key, gpointer data) -> gboolean {
            if (key->keyval != GDK_KEY_Escape) return FALSE;
            auto* self = static_cast<Installer*>(data);
            if (self->busy_) self->cancel(); else self->close();
            return TRUE;
        }), this);
        timer_ = g_timeout_add(100, +[](gpointer data) -> gboolean { return static_cast<Installer*>(data)->tick(); }, this);
        setControls();
        gtk_widget_show_all(window_);
        gtk_widget_grab_focus(source_);
        if (*gtk_entry_get_text(GTK_ENTRY(source_))) begin(true);
    }
    ~Installer() {
        if (timer_) g_source_remove(timer_);
        cancelled_ = true;
        if (worker_.joinable()) worker_.join();
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        auto options = arguments(argc, argv);
        if (options.help) {
            std::cout << Version << "\nUsage: kabus22-setup [--iso GAME.iso] [--install-dir NEW_DIRECTORY]\n"
                "                     [--patch PATCH_DIRECTORY] [--engine NATIVE_ELF]\n"
                "                     [--extractor INNOEXTRACT] [--unattended]\n"
                "Developer media: --setup ORIGINAL_setup.exe (with original sibling setup-1.bin), instead of --iso.\n"
                "Original-campaign widescreen is included in the native engine; no patch folder is required.\n"
                "--patch optionally installs separately obtained Kabus22Patch extras; none are bundled or auto-discovered.\n"
                "--unattended installs without a window; SIGINT/SIGTERM cancel safely.\n"
                "--engine and --extractor override trusted bundled siblings for development.\n"
                "--self-check runs CLI/core security regression checks without installing.\n";
            return 0;
        }
        if (options.version) { std::cout << Version << '\n'; return 0; }
        if (options.check) { argumentCheck(); selfCheck(); std::cout << "Installer self-check passed.\n"; return 0; }
        defaults(options.request);
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        if (options.unattended) {
            const auto result = install(options.request, [](const std::string& stage, std::uint64_t done, std::uint64_t total) {
                std::cerr << stage;
                if (total) std::cerr << " (" << done << '/' << total << ')';
                std::cerr << '\n';
                return !interrupted.load(std::memory_order_relaxed);
            });
            std::cout << "Native installation complete: " << result.root << "\nShortcut: " << result.shortcut << '\n';
            return 0;
        }
        if (!gtk_init_check(nullptr, nullptr))
            throw std::runtime_error("Cannot open the GTK display. Use --unattended with --iso for a headless install.");
        Installer installer(std::move(options.request));
        gtk_main();
        return 0;
    } catch (const Cancelled& error) {
        std::cerr << "kabus22-setup: " << error.what() << '\n';
        return 130;
    } catch (const std::exception& error) {
        std::cerr << "kabus22-setup: " << error.what() << '\n';
        return 1;
    }
}
