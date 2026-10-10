#include "setup.hpp"
#include "wizard_style.hpp"
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
#include <utility>

#include <nlohmann/json.hpp>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {
namespace fs = std::filesystem;
using namespace k22::setup;
constexpr const char* Version = "kabus22 native installer " K22_VERSION;
std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void interrupt(int) { interrupted.store(true, std::memory_order_relaxed); }

struct Options {
    Request request;
    bool unattended = false, help = false, version = false, check = false, verify = false, repair = false, json = false;
};
Options arguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") { options.help = true; continue; }
        if (flag == "--version") { options.version = true; continue; }
        if (flag == "--self-check") { options.check = true; continue; }
        if (flag == "--unattended") { options.unattended = true; continue; }
        if (flag == "--verify") { options.verify = true; continue; }
        if (flag == "--repair") { options.repair = true; continue; }
        if (flag == "--json") { options.json = true; continue; }
        fs::path* target = nullptr;
        if (flag == "--source") target = &options.request.source;
        else if (flag == "--iso") target = &options.request.sourceIso;
        else if (flag == "--setup") target = &options.request.sourceSetup;
        else if (flag == "--install-dir") target = &options.request.installRoot;
        else if (flag == "--engine") target = &options.request.enginePayload;
        else if (flag == "--launcher") target = &options.request.launcherPayload;
        else if (flag == "--updater") target = &options.request.updaterPayload;
        else if (flag == "--extractor") target = &options.request.extractor;
        else if (flag == "--patch") target = &options.request.optionalPatch;
        else throw std::runtime_error("Unknown option: " + flag);
        if (++i == argc || argv[i][0] == '\0') throw std::runtime_error("Missing value for " + flag);
        if (!target->empty()) throw std::runtime_error("Duplicate option: " + flag);
        *target = fs::absolute(argv[i]).lexically_normal();
    }
    const auto& request = options.request;
    const int sources = int(!request.source.empty()) + int(!request.sourceIso.empty()) + int(!request.sourceSetup.empty());
    if (sources > 1) throw std::runtime_error("Choose one of --source, --iso or the developer --setup route.");
    if (options.verify && options.repair) throw std::runtime_error("Choose --verify or --repair, not both.");
    if ((options.verify || options.repair) && !request.optionalPatch.empty())
        throw std::runtime_error("--verify/--repair cannot be combined with --patch.");
    if (options.verify && sources) throw std::runtime_error("--verify checks the installation only; it takes no source.");
    if (options.json && !options.verify) throw std::runtime_error("--json is only available with --verify.");
    if ((options.unattended || options.repair) && !sources)
        throw std::runtime_error("--unattended and --repair require --source GAME.iso|setup.exe|WINDOWS_GAME_FOLDER.");
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
    const auto repair = parse({"kabus22-setup", "--repair", "--install-dir", "root", "--source", "Kabus 22"});
    const auto verify = parse({"kabus22-setup", "--verify", "--json"});
    if (!repair.repair || repair.request.source.filename() != "Kabus 22" || !verify.verify || !verify.json)
        throw std::runtime_error("Verify/repair CLI parsing regression");
    const auto launcher = parse({"kabus22-setup", "--launcher", "settings with spaces"});
    if (launcher.request.launcherPayload.filename() != "settings with spaces")
        throw std::runtime_error("Launcher payload parsing regression");
    for (const auto& invalid : std::vector<std::vector<std::string>>{
            {"kabus22-setup", "--iso"}, {"kabus22-setup", "--unattended"},
            {"kabus22-setup", "--iso", "one.iso", "--setup", "setup.exe"},
            {"kabus22-setup", "--iso", "one.iso", "--iso", "two.iso"},
            {"kabus22-setup", "--launcher"}, {"kabus22-setup", "--launcher", "one", "--launcher", "two"},
            {"kabus22-setup", "--unknown"}, {"kabus22-setup", "--source", "a", "--iso", "b"},
            {"kabus22-setup", "--verify", "--repair", "--source", "a"}, {"kabus22-setup", "--repair"},
            {"kabus22-setup", "--verify", "--source", "a"}, {"kabus22-setup", "--json"},
            {"kabus22-setup", "--unattended", "--verify", "--source", "a"}}) {
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
    if (request.launcherPayload.empty()) request.launcherPayload = bundle / "kabus22-launcher";
    if (request.updaterPayload.empty()) request.updaterPayload = bundle / "kabus22-updater";
    if (request.extractor.empty()) request.extractor = bundle / "innoextract";
}
// Machine-readable progress for the launcher: "progress <0-100> <message>", only on change.
Progress printer() {
    return [last = std::string(), percent = -1](const std::string& stage, std::uint64_t done, std::uint64_t total) mutable {
        const int now = total ? static_cast<int>(std::min(done, total) * 100 / total) : std::max(percent, 0);
        if (now != percent || stage != last) { std::cout << "progress " << now << ' ' << stage << std::endl; percent = now; last = stage; }
        return !interrupted.load(std::memory_order_relaxed);
    };
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
    // Fits the wizard page body without growing the 391px Inno page height.
    constexpr int maxWidth = 440, maxHeight = 100;
    if (pixels.width > maxWidth || pixels.height > maxHeight) {
        if (std::int64_t(frame->width) * maxHeight >= std::int64_t(frame->height) * maxWidth) {
            pixels.width = maxWidth;
            pixels.height = std::max(1, int(std::int64_t(frame->height) * maxWidth / frame->width));
        } else {
            pixels.height = maxHeight;
            pixels.width = std::max(1, int(std::int64_t(frame->width) * maxHeight / frame->height));
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
// Inno's page icons; themes without them leave the space empty instead of a broken-image glyph.
GtkWidget* themedIcon(const char* name) {
    return gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), name) ? gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_DND) : gtk_image_new();
}

// Languages of the original setup's "Select Setup Language" list, in its order.
enum Language { German, English, Turkish };
Language language = English;
enum Text { Title, LanguageTitle, LanguagePrompt, Ok, Back, Next, Cancel, InstallButton, Finish, Browse, Yes, No,
    WelcomeTitle, WelcomeBody, MediaTitle, MediaSubtitle, MediaLead, MediaHint,
    DestinationTitle, DestinationSubtitle, DestinationLead, DestinationHint, DestinationNote,
    TasksTitle, TasksSubtitle, TasksLead, TasksLabel, TasksNote,
    ReadyTitle, ReadySubtitle, ReadyLead, ReadyDestination, ReadyMedia, ReadyTasks, ReadyShortcuts, ReadyExtras,
    InstallingTitle, InstallingSubtitle, Preparing, NotCompleted, Cancelling, InstallCancelled,
    ReadingArtwork, ArtworkLoaded, ArtworkFailed, ArtworkCancelled,
    FinishedTitle, FinishedBody, OpenSettings, ExitTitle, ExitBody, TextCount };
// Wording follows the original wizard's pages; button captions follow its translations.
constexpr const char* texts[TextCount][3] = {
    {"Setup - Kabus 22", "Setup - Kabus 22", "Kurulum - Kabus 22"},
    {"Setup-Sprache auswählen", "Select Setup Language", "Kurulum Dilini Seçin"},
    {"Wählen Sie die Sprache aus, die während der Installation benutzt werden soll:",
     "Select the language to use during the installation:", "Kurulum sırasında kullanılacak dili seçin:"},
    {"OK", "OK", "TAMAM"},
    {"< _Zurück", "< _Back", "< _GERİ"},
    {"_Weiter >", "_Next >", "İ_LERİ >"},
    {"Abbrechen", "Cancel", "KAPAT"},
    {"_Installieren", "_Install", "_Kur"},
    {"_Fertigstellen", "_Finish", "_BİTTİ"},
    {"Du_rchsuchen ...", "B_rowse...", "K_LASÖR AÇ..."},
    {"_Ja", "_Yes", "_EVET"},
    {"_Nein", "_No", "_HAYIR"},
    {"Willkommen zum Kabus 22 Setup-Assistenten", "Welcome to the Kabus 22 Setup Wizard",
     "Kabus 22 Oyunu Bilgisayarınıza Kurulacaktır"},
    {"Dieser Assistent wird jetzt die native Linux-Version von Kabus 22 auf Ihrem Computer installieren. "
     "Die Spieldaten stammen von Ihrem eigenen Original-Datenträger.\n\n"
     "Sie sollten alle anderen Anwendungen beenden, bevor Sie mit dem Setup fortfahren.\n\n"
     "\"Weiter\" zum Fortfahren, \"Abbrechen\" zum Verlassen.",
     "This will install the native Linux version of Kabus 22 on your computer, using the game data "
     "from your own original disc.\n\nIt is recommended that you close all other applications before continuing.\n\n"
     "Click Next to continue, or Cancel to exit Setup.",
     "Kabus 22'nin yerel Linux sürümü, kendi orijinal diskinizdeki oyun verileriyle bilgisayarınıza kurulacak.\n\n"
     "Kurulum işlemine geçmeden önce çalışan diğer programlarınızı kapatın.\n\n"
     "Kurulumu başlatmak için İLERİ > seçeneğini, çıkmak için KAPAT seçeneğini seçin."},
    {"Spieldatenträger wählen", "Select Game Media", "Oyun Kaynağını Seçin"},
    {"Wo befindet sich Ihr Original-Datenträger von Kabus 22?", "Where is your original Kabus 22 disc?",
     "Orijinal Kabus 22 diskiniz nerede?"},
    {"Das Setup liest die Spieldaten aus dem folgenden ISO-Abbild, der setup.exe oder dem installierten Windows-Spielordner.",
     "Setup will read the game data from the following ISO image, setup.exe or installed Windows game folder.",
     "Oyun verileri aşağıdaki ISO kalıbından, setup.exe dosyasından veya kurulu Windows oyun klasöründen okunacak."},
    {"Klicken Sie auf \"Weiter\", um fortzufahren. Klicken Sie auf \"Durchsuchen\", falls Sie ein anderes Abbild "
     "auswählen möchten. Vom Datenträger wird nichts ausgeführt.",
     "To continue, click Next. If you would like to select a different image, click Browse. "
     "Nothing on the disc is executed.",
     "Devam etmek için İLERİ > seçeneğini seçin. Farklı bir kalıp seçmek için KLASÖR AÇ seçeneğini seçin. "
     "Diskteki hiçbir program çalıştırılmaz."},
    {"Ziel-Ordner wählen", "Select Destination Location", "Oyunun Kurulacağı Yeri Seçin"},
    {"Wohin soll Kabus 22 installiert werden?", "Where should Kabus 22 be installed?", "Kabus 22 nereye kurulsun?"},
    {"Das Setup wird Kabus 22 in den folgenden Ordner installieren.", "Setup will install Kabus 22 into the following folder.",
     "Kabus 22 oyunu belirtilen klasöre kurulacak."},
    {"Klicken Sie auf \"Weiter\", um fortzufahren. Klicken Sie auf \"Durchsuchen\", falls Sie einen anderen Ordner "
     "auswählen möchten.", "To continue, click Next. If you would like to select a different folder, click Browse.",
     "Devam etmek için İLERİ > seçeneğini seçin. Farklı bir klasöre kurmak için KLASÖR AÇ seçeneğini seçin."},
    {"Installiert nur für Ihren Benutzer. Eine Neuinstallation ersetzt eine frühere Kabus 22-Installation und ihre "
     "Verknüpfungen; Spielstände und Einstellungen bleiben erhalten.",
     "Installs for your user only. Reinstalling replaces an earlier Kabus 22 installation and its shortcuts; "
     "saves and settings are kept.",
     "Yalnızca sizin kullanıcınız için kurulur. Yeniden kurulum önceki Kabus 22 kurulumunu ve kısayollarını "
     "değiştirir; kayıtlar ve ayarlar korunur."},
    {"Zusätzliche Aufgaben auswählen", "Select Additional Tasks", "Ekstra Görevleri Seçin"},
    {"Welche zusätzlichen Aufgaben sollen ausgeführt werden?", "Which additional tasks should be performed?",
     "Hangi ek görevler yapılsın?"},
    {"Wählen Sie die zusätzlichen Aufgaben aus, die das Setup während der Installation von Kabus 22 ausführen soll, "
     "und klicken Sie danach auf \"Weiter\".",
     "Select the additional tasks you would like Setup to perform while installing Kabus 22, then click Next.",
     "Kabus 22 kurulurken yapılmasını istediğiniz ek görevleri seçin, ardından İLERİ > seçeneğini seçin."},
    {"_Community-Extras (optional):", "_Community extras (optional):", "_Topluluk eklentileri (isteğe bağlı):"},
    {"Breitbild für die Original-Kampagne ist bereits enthalten; kein Patch-Ordner nötig. Leer lassen, um nur die "
     "Original-Daten zu installieren. Einstellungsprogramm und Verknüpfungen werden immer erstellt.",
     "Original-campaign widescreen is included natively; no patch folder is needed. Leave empty to install only the "
     "original disc data. The settings launcher and shortcuts are always created.",
     "Orijinal kampanya için geniş ekran desteği yerleşiktir; yama klasörü gerekmez. Yalnızca orijinal disk verilerini "
     "kurmak için boş bırakın. Ayarlar başlatıcısı ve kısayollar her zaman oluşturulur."},
    {"Installation durchführen", "Ready to Install", "Kurulmaya Hazır"},
    {"Das Setup ist jetzt bereit, Kabus 22 auf Ihrem Computer zu installieren.",
     "Setup is now ready to begin installing Kabus 22 on your computer.", "Kabus 22 şimdi bilgisayarınıza kurulacak."},
    {"Klicken Sie auf \"Installieren\", um mit der Installation zu beginnen, oder auf \"Zurück\", um Ihre "
     "Einstellungen zu überprüfen oder zu ändern.",
     "Click Install to continue with the installation, or click Back if you want to review or change any settings.",
     "Kuruluma devam etmek için Kur seçeneğini, ayarlarınızı değiştirmek için GERİ seçeneğini seçin."},
    {"Ziel-Ordner:", "Destination location:", "Kurulum yeri:"},
    {"Spieldatenträger:", "Game media:", "Oyun kaynağı:"},
    {"Zusätzliche Aufgaben:", "Additional tasks:", "Ek görevler:"},
    {"Einstellungsprogramm und Verknüpfungen erstellen", "Create the settings launcher and shortcuts",
     "Ayarlar başlatıcısını ve kısayolları oluştur"},
    {"Community-Extras: ", "Community extras: ", "Topluluk eklentileri: "},
    {"Installiere ...", "Installing", "Kurulum İşlemi"},
    {"Warten Sie bitte, während Kabus 22 auf Ihrem Computer installiert wird.",
     "Please wait while Setup installs Kabus 22 on your computer.", "Kabus 22 bilgisayarınıza kuruluyor, lütfen bekleyin."},
    {"Installation wird vorbereitet ...", "Preparing to install...", "Kuruluma hazırlanıyor..."},
    {"Das Setup konnte nicht abgeschlossen werden. Beheben Sie das unten angezeigte Problem und klicken Sie auf "
     "\"Zurück\", um es erneut zu versuchen.",
     "Setup was not completed. Correct the problem below, then click Back to try again.",
     "Kurulum tamamlanmadı. Aşağıdaki sorunu giderip yeniden denemek için GERİ seçeneğini seçin."},
    {"Wird sicher abgebrochen ...", "Cancelling safely...", "Güvenli şekilde iptal ediliyor..."},
    {"Die Installation wurde abgebrochen.", "Installation cancelled.", "Kurulum iptal edildi."},
    {"Originalbild wird vom Datenträger gelesen ...", "Reading the original artwork from your disc...",
     "Orijinal görseller diskinizden okunuyor..."},
    {"Originalbild von Ihrem Datenträger geladen.", "Original artwork loaded from your disc.",
     "Orijinal görseller diskinizden yüklendi."},
    {"Vorschau fehlgeschlagen. Die Installation wurde nicht gestartet.",
     "Artwork preview failed. Installation has not started.", "Önizleme başarısız oldu. Kurulum başlamadı."},
    {"Lesen abgebrochen.", "Artwork reading cancelled.", "Okuma iptal edildi."},
    {"Beenden des Kabus 22 Setup-Assistenten", "Completing the Kabus 22 Setup Wizard", "Kabus 22 Kurulumu Tamamlandı"},
    {"Das Setup hat die Installation von Kabus 22 auf Ihrem Computer abgeschlossen. Das Spiel kann über die "
     "installierten Verknüpfungen gestartet werden.\n\nKlicken Sie auf \"Fertigstellen\", um das Setup zu beenden.",
     "Setup has finished installing Kabus 22 on your computer. The game may be launched by selecting the installed "
     "shortcuts.\n\nClick Finish to exit Setup.",
     "Kabus 22 bilgisayarınıza kuruldu. Oyunu kurulan kısayollardan başlatabilirsiniz.\n\n"
     "Kurulumu kapatmak için BİTTİ seçeneğini seçin."},
    {"Kabus 22-_Einstellungen öffnen", "_Open Kabus 22 settings", "Kabus 22 _ayarlarını aç"},
    {"Setup verlassen", "Exit Setup", "Kurulumdan Çık"},
    {"Das Setup ist noch nicht abgeschlossen. Wenn Sie jetzt beenden, wird Kabus 22 nicht installiert.\n\n"
     "Sie können das Setup zu einem späteren Zeitpunkt nochmals ausführen, um die Installation zu vervollständigen.\n\n"
     "Setup verlassen?",
     "Setup is not complete. If you exit now, Kabus 22 will not be installed.\n\n"
     "You may run Setup again at another time to complete the installation.\n\nExit Setup?",
     "Kurulum tamamlanmadı. Şimdi çıkarsanız Kabus 22 kurulmayacak.\n\n"
     "Kurulumdan çıkmak istediğinizden emin misiniz?"},
};
const char* T(Text id) { return texts[id][language]; }

// The original setup first asks for its language; the system locale preselects it.
bool chooseLanguage() {
    const char* locale = nullptr;
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) if ((locale = g_getenv(name)) && *locale) break;
    const std::string_view code = locale ? locale : "";
    language = code.starts_with("tr") ? Turkish : code.starts_with("de") ? German : English;
    auto* dialog = gtk_dialog_new_with_buttons(T(LanguageTitle), nullptr, GTK_DIALOG_MODAL,
        T(Ok), GTK_RESPONSE_OK, T(Cancel), GTK_RESPONSE_CANCEL, nullptr);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(row), 10);
    auto* icon = themedIcon("media-optical");
    gtk_widget_set_valign(icon, GTK_ALIGN_START);
    auto* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    auto* choices = gtk_combo_box_text_new();
    for (const char* name : {"Deutsch", "English", "Türkçe"}) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choices), name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(choices), language);
    accessible(choices, "Setup language");
    gtk_box_pack_start(GTK_BOX(column), k22::wizard::text(T(LanguagePrompt)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(column), choices, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), column, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), row, TRUE, TRUE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK));
    const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    language = static_cast<Language>(std::clamp(gtk_combo_box_get_active(GTK_COMBO_BOX(choices)), 0, 2));
    gtk_widget_destroy(dialog);
    return accepted;
}

// Inno Setup page flow: welcome, media (native addition), destination, additional tasks, ready,
// installing, finished. Welcome/finished show the large wizard image, inner pages the small one.
enum Page { Welcome, Media, Destination, Tasks, Ready, Installing, Finished };
class Installer {
    Request request_;
    Result result_;
    GtkWidget *window_, *stack_, *source_, *destination_, *patch_, *image_, *mediaStatus_, *mediaErrors_,
              *summary_, *status_, *errors_, *progress_, *launchCheck_, *finishErrors_, *back_, *next_, *cancel_;
    std::vector<GtkWidget*> editable_, sides_, smalls_;
    GtkFileChooserNative* chooser_ = nullptr;
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> cancelled_{false};
    std::string stage_, error_;
    std::uint64_t done_ = 0, total_ = 0;
    Pixels pixels_;
    GdkPixbuf *wizard_ = nullptr, *wizardSmall_ = nullptr; // worker results, under mutex_
    int page_ = Welcome;
    bool finished_ = false, dirty_ = false, succeeded_ = false, busy_ = false, previewing_ = false,
         installed_ = false, closing_ = false;
    guint timer_ = 0;
    bool loose_;

    GtkWidget* button(GtkWidget* box, const char* mnemonic, GCallback callback) {
        auto* widget = gtk_button_new_with_mnemonic(mnemonic);
        g_signal_connect(widget, "clicked", callback, this);
        gtk_box_pack_end(GTK_BOX(box), widget, FALSE, FALSE, 0);
        return widget;
    }
    // Entry with Inno's Browse… button beside it.
    GtkWidget* field(GtkWidget* body, const char* name, const fs::path& value, const char* description, GCallback browse) {
        auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        auto* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
        gtk_widget_set_tooltip_text(entry, description);
        accessible(entry, name, description);
        auto* browser = gtk_button_new_with_mnemonic(T(Browse));
        accessible(browser, (std::string("Browse: ") + name).c_str(), description);
        g_signal_connect(browser, "clicked", browse, this);
        gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), browser, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(body), row, FALSE, FALSE, 0);
        editable_.insert(editable_.end(), {entry, browser});
        return entry;
    }
    void add(GtkWidget* page, int index) {
        gtk_stack_add_named(GTK_STACK(stack_), page, std::to_string(index).c_str());
    }
    // Welcome / finished: large wizard image at the left, white text panel (returned for extras).
    GtkWidget* sidePage(int index, Text title, Text body) {
        auto* page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        auto* side = k22::wizard::styled(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), "wizard-side");
        gtk_widget_set_size_request(side, k22::wizard::SideWidth, k22::wizard::PageHeight);
        auto* image = gtk_image_new();
        gtk_box_pack_start(GTK_BOX(side), image, TRUE, TRUE, 0);
        sides_.push_back(image);
        auto* paper = k22::wizard::styled(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), "wizard-white");
        auto* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        for (auto edge : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom})
            edge(panel, 14);
        gtk_box_pack_start(GTK_BOX(panel), k22::wizard::styled(k22::wizard::text(T(title)), "wizard-welcome-title"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(panel), k22::wizard::text(T(body)), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(paper), panel, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(page), side, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), paper, TRUE, TRUE, 0);
        add(page, index);
        return panel;
    }
    // Inner page: header strip, then the indented body this returns.
    GtkWidget* innerPage(int index, Text title, Text subtitle) {
        GtkWidget *heading, *subheading, *image;
        auto* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(page), k22::wizard::header(heading, subheading, image), FALSE, FALSE, 0);
        gtk_label_set_text(GTK_LABEL(heading), T(title));
        gtk_label_set_text(GTK_LABEL(subheading), T(subtitle));
        smalls_.push_back(image);
        auto* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(body, 36);
        gtk_widget_set_margin_end(body, 36);
        gtk_widget_set_margin_top(body, 16);
        gtk_widget_set_margin_bottom(body, 12);
        gtk_box_pack_start(GTK_BOX(page), body, TRUE, TRUE, 0);
        add(page, index);
        return body;
    }
    // Inno's icon + sentence line at the top of the media and destination pages.
    void lead(GtkWidget* body, const char* icon, Text text) {
        auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(row), themedIcon(icon), FALSE, FALSE, 0);
        auto* label = k22::wizard::text(T(text));
        gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(body), row, FALSE, FALSE, 0);
    }
    GtkWidget* detail(GtkWidget* body, bool expand) {
        auto* label = k22::wizard::text("");
        gtk_label_set_selectable(GTK_LABEL(label), TRUE);
        gtk_widget_set_valign(label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(body), label, expand, expand, 0);
        return label;
    }
    // Consumes both references; a missing image keeps the plain panel.
    void artwork(GdkPixbuf* large, GdkPixbuf* small) {
        if (large) {
            auto* scaled = gdk_pixbuf_scale_simple(large, k22::wizard::SideWidth, k22::wizard::PageHeight, GDK_INTERP_BILINEAR);
            for (auto* image : sides_) gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled);
            if (scaled) g_object_unref(scaled);
            g_object_unref(large);
        }
        if (small) {
            for (auto* image : smalls_) gtk_image_set_from_pixbuf(GTK_IMAGE(image), small);
            g_object_unref(small);
        }
    }
    std::string text(GtkWidget* entry) const { return gtk_entry_get_text(GTK_ENTRY(entry)); }
    void setControls() {
        // The artwork preview runs in the background; only the media row and Install wait for it.
        const bool idle = !busy_, browsing = idle || previewing_;
        for (std::size_t i = 0; i < editable_.size(); ++i) gtk_widget_set_sensitive(editable_[i], !installed_ && (idle || (previewing_ && i >= 2)));
        gtk_widget_set_visible(back_, (page_ > Welcome && page_ < Installing) || (page_ == Installing && idle && !installed_));
        gtk_widget_set_sensitive(back_, browsing);
        gtk_widget_set_visible(next_, page_ != Installing);
        gtk_button_set_label(GTK_BUTTON(next_), T(page_ == Ready ? InstallButton : page_ == Finished ? Finish : Next));
        const bool filled = (page_ != Media || !text(source_).empty()) && (page_ != Destination || !text(destination_).empty());
        gtk_widget_set_sensitive(next_, (page_ == Ready ? idle : browsing) && filled && !closing_);
        gtk_widget_set_visible(cancel_, page_ != Finished);
        gtk_widget_set_sensitive(cancel_, !busy_ || !cancelled_.load());
    }
    void show(int page) {
        page_ = page;
        gtk_stack_set_visible_child_name(GTK_STACK(stack_), std::to_string(page).c_str());
        if (page == Ready) {
            std::string summary = std::string(T(ReadyDestination)) + "\n      " + text(destination_) + "\n\n" + T(ReadyMedia) +
                "\n      " + text(source_) + "\n\n" + T(ReadyTasks) + "\n      " + T(ReadyShortcuts);
            if (!text(patch_).empty()) summary += "\n      " + std::string(T(ReadyExtras)) + text(patch_);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(summary_)), displayText(summary).c_str(), -1);
        }
        setControls();
        if (gtk_widget_get_visible(next_)) gtk_widget_grab_focus(next_);
    }
    void next() {
        if (page_ == Ready) begin(false);
        else if (page_ == Finished) finish();
        else show(page_ + 1);
    }
    void finish() {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(launchCheck_))) {
            try { launch(result_); }
            catch (const std::exception& error) {
                labelText(finishErrors_, error.what());
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(launchCheck_), FALSE);
                return;
            }
        }
        close();
    }
    bool confirmExit() {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", T(ExitBody));
        gtk_window_set_title(GTK_WINDOW(dialog), T(ExitTitle));
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), T(Yes), GTK_RESPONSE_YES, T(No), GTK_RESPONSE_NO, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
        const bool yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(dialog);
        return yes;
    }
    void cancelPressed() {
        if (installed_) close();
        else if (busy_ && previewing_ && page_ == Media) cancel();
        else if (confirmExit()) close();
    }
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
        GtkWidget* status = preview ? mediaStatus_ : status_;
        GtkWidget* errors = preview ? mediaErrors_ : errors_;
        if (!preview) show(Installing);
        try {
            labelText(errors, "");
            const fs::path source = text(source_);
            if (source.empty()) throw std::runtime_error("Select your original ISO image first.");
            Request input = request_;
            input.source = fs::absolute(source).lexically_normal();
            input.sourceIso.clear(); input.sourceSetup.clear();
            input.installRoot = text(destination_);
            if (input.installRoot.empty()) throw std::runtime_error("Choose an installation directory.");
            input.installRoot = fs::absolute(input.installRoot).lexically_normal();
            input.optionalPatch = text(patch_);
            if (!input.optionalPatch.empty()) input.optionalPatch = fs::absolute(input.optionalPatch).lexically_normal();
            cancelled_ = false;
            busy_ = true;
            previewing_ = preview;
            labelText(status, T(preview ? ReadingArtwork : Preparing));
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
                        const auto artwork = previewArtwork(input.source, input.extractor, scratch.root / "media", progress);
                        if (cancelled_.load() || interrupted.load()) throw Cancelled();
                        auto pixels = nativeArtwork(artwork.logo);
                        auto* large = k22::wizard::load(artwork.wizard);
                        auto* small = k22::wizard::load(artwork.wizardSmall);
                        std::lock_guard lock(mutex_);
                        pixels_ = std::move(pixels);
                        if (wizard_) g_object_unref(wizard_);
                        if (wizardSmall_) g_object_unref(wizardSmall_);
                        wizard_ = large; wizardSmall_ = small;
                        stage_ = T(ArtworkLoaded);
                    } else {
                        auto result = install(input, progress);
                        std::lock_guard lock(mutex_);
                        result_ = std::move(result);
                        succeeded_ = true;
                    }
                } catch (const Cancelled& error) {
                    std::lock_guard lock(mutex_);
                    stage_ = T(preview ? ArtworkCancelled : InstallCancelled);
                    error_ = error.what();
                } catch (const std::exception& error) {
                    std::lock_guard lock(mutex_);
                    stage_ = T(preview ? ArtworkFailed : NotCompleted);
                    error_ = error.what();
                } catch (...) {
                    std::lock_guard lock(mutex_);
                    stage_ = T(preview ? ArtworkFailed : NotCompleted);
                    error_ = "Unexpected native installer error.";
                }
                std::lock_guard lock(mutex_);
                finished_ = dirty_ = true;
            });
        } catch (const std::exception& error) {
            busy_ = false;
            previewing_ = false;
            labelText(status, preview ? "" : T(NotCompleted));
            labelText(errors, error.what());
            setControls();
        }
    }
    void cancel() {
        if (!busy_) return;
        cancelled_ = true;
        labelText(previewing_ ? mediaStatus_ : status_, T(Cancelling));
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
                if (!cancelled_.load() || finished) labelText(previewing_ ? mediaStatus_ : status_, stage_);
                labelText(previewing_ ? mediaErrors_ : errors_, error_);
                if (!previewing_)
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), total_ ?
                        std::clamp(static_cast<double>(done_) / static_cast<double>(total_), 0.0, 1.0) : 0);
                dirty_ = false;
            }
            if (!total_ && !finished && !previewing_) gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_));
        }
        if (!finished) return G_SOURCE_CONTINUE;
        worker_.join();
        busy_ = false;
        previewing_ = false;
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
            } else labelText(mediaErrors_, "Cannot display the decoded original artwork.");
        }
        artwork(std::exchange(wizard_, nullptr), std::exchange(wizardSmall_, nullptr));
        if (closing_) { gtk_widget_destroy(window_); return G_SOURCE_CONTINUE; }
        if (installed_) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_), 1);
            show(Finished);
        } else setControls();
        return G_SOURCE_CONTINUE;
    }
public:
    explicit Installer(Request request) : request_(std::move(request)), loose_(!request_.sourceSetup.empty()) {
        window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window_), T(Title));
        gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
        gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
        auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(window_), content);
        stack_ = gtk_stack_new();
        gtk_widget_set_size_request(stack_, k22::wizard::Width, k22::wizard::PageHeight);
        gtk_box_pack_start(GTK_BOX(content), stack_, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
        auto* nav = k22::wizard::styled(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), "wizard-nav");
        gtk_container_set_border_width(GTK_CONTAINER(nav), 12);
        gtk_box_pack_start(GTK_BOX(content), nav, FALSE, FALSE, 0);
        cancel_ = button(nav, T(Cancel), G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->cancelPressed(); }));
        gtk_widget_set_margin_start(cancel_, 10);
        next_ = button(nav, T(Next), G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<Installer*>(data)->next(); }));
        back_ = button(nav, T(Back), G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<Installer*>(data); self->show(self->page_ == Installing ? Ready : self->page_ - 1);
        }));

        sidePage(Welcome, WelcomeTitle, WelcomeBody);

        auto* body = innerPage(Media, MediaTitle, MediaSubtitle);
        lead(body, "media-optical", MediaLead);
        gtk_box_pack_start(GTK_BOX(body), k22::wizard::text(T(MediaHint)), FALSE, FALSE, 0);
        source_ = field(body, !request_.source.empty() ? "Game media" : loose_ ? "Original setup (developer)" : "ISO image",
            !request_.source.empty() ? request_.source : loose_ ? request_.sourceSetup : request_.sourceIso,
            "Select the complete original Kabus22 ISO image, original setup.exe, or an installed Windows game folder. Nothing is executed.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->source_, GTK_FILE_CHOOSER_ACTION_OPEN, true); }));
        image_ = gtk_image_new();
        accessible(image_, "Original artwork preview", "Choose your own game media to load the original logo. No game artwork is bundled.");
        gtk_box_pack_start(GTK_BOX(body), image_, FALSE, FALSE, 0);
        mediaStatus_ = detail(body, false);
        accessible(mediaStatus_, "Media status");
        mediaErrors_ = detail(body, true);
        accessible(mediaErrors_, "Media error details");

        body = innerPage(Destination, DestinationTitle, DestinationSubtitle);
        lead(body, "folder", DestinationLead);
        gtk_box_pack_start(GTK_BOX(body), k22::wizard::text(T(DestinationHint)), FALSE, FALSE, 0);
        destination_ = field(body, "Install directory", request_.installRoot,
            "A new or empty directory, or an existing Kabus22 installation to replace. Browse selects a parent and adds kabus22; edit the path to choose another name.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->destination_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, false, true); }));
        auto* note = k22::wizard::text(T(DestinationNote));
        gtk_widget_set_valign(note, GTK_ALIGN_END);
        gtk_box_pack_start(GTK_BOX(body), note, TRUE, TRUE, 0);

        body = innerPage(Tasks, TasksTitle, TasksSubtitle);
        gtk_box_pack_start(GTK_BOX(body), k22::wizard::text(T(TasksLead)), FALSE, FALSE, 0);
        auto* extras = gtk_label_new_with_mnemonic(T(TasksLabel));
        gtk_label_set_xalign(GTK_LABEL(extras), 0);
        gtk_box_pack_start(GTK_BOX(body), extras, FALSE, FALSE, 0);
        patch_ = field(body, "Community extras (optional)", request_.optionalPatch,
            "Optional, separately obtained Kabus22Patch directory for its extras. Not required for native original-campaign widescreen; leave empty for original ISO data.",
            G_CALLBACK(+[](GtkButton*, gpointer data) { auto* self = static_cast<Installer*>(data); self->choose(self->patch_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, false); }));
        gtk_label_set_mnemonic_widget(GTK_LABEL(extras), patch_);
        gtk_box_pack_start(GTK_BOX(body), k22::wizard::text(T(TasksNote)), FALSE, FALSE, 0);

        body = innerPage(Ready, ReadyTitle, ReadySubtitle);
        gtk_box_pack_start(GTK_BOX(body), k22::wizard::text(T(ReadyLead)), FALSE, FALSE, 0);
        auto* memo = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(memo), GTK_SHADOW_IN);
        summary_ = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(summary_), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(summary_), FALSE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(summary_), 4);
        accessible(summary_, "Installation summary");
        gtk_container_add(GTK_CONTAINER(memo), summary_);
        gtk_box_pack_start(GTK_BOX(body), memo, TRUE, TRUE, 0);

        body = innerPage(Installing, InstallingTitle, InstallingSubtitle);
        status_ = detail(body, false);
        accessible(status_, "Installer status");
        progress_ = gtk_progress_bar_new();
        accessible(progress_, "Installation progress");
        gtk_box_pack_start(GTK_BOX(body), progress_, FALSE, FALSE, 0);
        errors_ = detail(body, true);
        accessible(errors_, "Installer error details");

        auto* finished = sidePage(Finished, FinishedTitle, FinishedBody);
        launchCheck_ = gtk_check_button_new_with_mnemonic(T(OpenSettings));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(launchCheck_), TRUE);
        gtk_box_pack_start(GTK_BOX(finished), launchCheck_, FALSE, FALSE, 0);
        finishErrors_ = detail(finished, false);

        // A reinstall already owns the original wizard images.
        artwork(k22::wizard::load(request_.installRoot / "wizard-image.png"), k22::wizard::load(request_.installRoot / "wizard-small-image.png"));
        for (auto* entry : {source_, destination_})
            g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
                auto* self = static_cast<Installer*>(data);
                if (GTK_WIDGET(editable) == self->source_) gtk_image_clear(GTK_IMAGE(self->image_));
                self->setControls();
            }), this);
        g_signal_connect(window_, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
            auto* self = static_cast<Installer*>(data);
            if (self->installed_ || self->confirmExit()) self->close();
            return TRUE;
        }), this);
        g_signal_connect(window_, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) { gtk_main_quit(); }), nullptr);
        g_signal_connect(window_, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* key, gpointer data) -> gboolean {
            if (key->keyval != GDK_KEY_Escape) return FALSE;
            static_cast<Installer*>(data)->cancelPressed();
            return TRUE;
        }), this);
        timer_ = g_timeout_add(100, +[](gpointer data) -> gboolean { return static_cast<Installer*>(data)->tick(); }, this);
        gtk_widget_show_all(window_);
        show(Welcome);
        if (!text(source_).empty()) begin(true);
    }
    ~Installer() {
        if (timer_) g_source_remove(timer_);
        cancelled_ = true;
        if (worker_.joinable()) worker_.join();
        if (wizard_) g_object_unref(wizard_);
        if (wizardSmall_) g_object_unref(wizardSmall_);
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        auto options = arguments(argc, argv);
        if (options.help) {
            std::cout << Version << "\nUsage: kabus22-setup [--source GAME.iso|setup.exe|WINDOWS_GAME_FOLDER] [--install-dir DIRECTORY]\n"
                "                     [--patch PATCH_DIRECTORY] [--engine NATIVE_ELF] [--launcher SETTINGS_ELF]\n"
                "                     [--updater UPDATER_ELF] [--extractor INNOEXTRACT] [--unattended]\n"
                "       kabus22-setup --verify [--install-dir ROOT] [--json]\n"
                "       kabus22-setup --repair [--install-dir ROOT] --source GAME.iso|setup.exe|WINDOWS_GAME_FOLDER [--unattended]\n"
                "--source detects an ISO image, original setup.exe with sibling setup-1.bin, or an installed\n"
                "Windows Kabus22 folder (K22.wrs + K22.exe; only game data is imported). --iso/--setup still work.\n"
                "Original-campaign widescreen is included in the native engine; no patch folder is required.\n"
                "--patch optionally installs separately obtained Kabus22Patch extras; none are bundled or auto-discovered.\n"
                "--unattended installs without a window; SIGINT/SIGTERM cancel safely.\n"
                "--verify compares installed files with the recorded inventory (exit 3 on mismatch).\n"
                "--repair restores missing/modified game files from the source, then verifies (exit 3 if any remain).\n"
                "--engine, --launcher, --updater and --extractor override trusted bundled siblings for development.\n"
                "--self-check runs CLI/core security regression checks without installing.\n";
            return 0;
        }
        if (options.version) { std::cout << Version << '\n'; return 0; }
        if (options.check) { argumentCheck(); selfCheck(); std::cout << "Installer self-check passed.\n"; return 0; }
        defaults(options.request);
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        if (options.verify || options.repair) {
            auto& request = options.request;
            const auto source = !request.source.empty() ? request.source : !request.sourceIso.empty() ? request.sourceIso : request.sourceSetup;
            const auto result = options.verify ? verify(request.installRoot, options.json ? Progress{} : printer())
                                               : repair(request.installRoot, source, request.extractor, printer());
            if (options.json) {
                std::cout << nlohmann::json{{"ok", result.ok()}, {"checked", result.checked},
                    {"missing", result.missing}, {"modified", result.modified}}.dump() << '\n';
            } else {
                for (const auto& path : result.missing) std::cerr << "missing: " << path << '\n';
                for (const auto& path : result.modified) std::cerr << "modified: " << path << '\n';
                std::cout << (result.ok() ? "Installation verified: " : "Installation has mismatches: ") << result.checked << " files checked, "
                          << result.missing.size() << " missing, " << result.modified.size() << " modified\n";
            }
            return result.ok() ? 0 : 3;
        }
        if (options.unattended) {
            const auto result = install(options.request, printer());
            std::cout << "Native installation complete: " << result.root << "\nShortcut: " << result.shortcut << '\n';
            return 0;
        }
        if (!gtk_init_check(nullptr, nullptr))
            throw std::runtime_error("Cannot open the GTK display. Use --unattended with --iso for a headless install.");
        k22::wizard::style();
        if (!chooseLanguage()) return 0;
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
