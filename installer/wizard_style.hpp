#pragma once
// The original Kabus 22 Windows setup (Inno Setup 5) look, shared by the native installer and
// the settings launcher. The wizard images are the user's own: the installer converts them from
// the original setup header into the install root; without them everything stays plain.
#include <gtk/gtk.h>
#include <filesystem>

namespace k22::wizard {
// Inno's 497x360 wizard at 125%: the 164x314 WizardImage panel becomes 201x391.
inline constexpr int Width = 620, PageHeight = 391, SideWidth = 201;

inline void style() {
    // The original wizard is a light Windows dialog regardless of the desktop's dark theme.
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", "Adwaita",
                 "gtk-application-prefer-dark-theme", FALSE, nullptr);
    auto* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, R"css(
* { font-family: Tahoma, "Segoe UI", "DejaVu Sans", sans-serif; font-size: 9pt; }
window, dialog, .wizard-face { background-color: #f0f0f0; color: #000000; }
.wizard-white { background-color: #ffffff; }
.wizard-side { background-image: linear-gradient(#0000ff, #000033); }
.wizard-welcome-title { font-size: 12pt; font-weight: bold; }
.wizard-title { font-weight: bold; }
button.text-button, .wizard-nav button { border-radius: 0; padding: 2px 8px; min-height: 0; background-image: none;
    background-color: #e1e1e1; border: 1px solid #adadad; box-shadow: none; text-shadow: none; color: #000000; }
.wizard-nav button { min-width: 75px; }
button.text-button:hover, .wizard-nav button:hover { background-color: #e5f1fb; border-color: #0078d7; }
button.text-button:active, .wizard-nav button:active { background-color: #cce4f7; border-color: #005499; }
button.text-button:disabled, .wizard-nav button:disabled { background-color: #cccccc; border-color: #bfbfbf; color: #838383; }
entry, spinbutton, spinbutton button { border-radius: 0; box-shadow: none; background-image: none; }
entry { border-color: #7a7a7a; }
combobox button.combo { border-radius: 0; background-image: none; background-color: #ffffff; border-color: #7a7a7a; box-shadow: none; }
progressbar trough { border-radius: 0; background-color: #e6e6e6; border: 1px solid #bcbcbc; min-height: 16px; }
progressbar progress { border-radius: 0; background-image: none; background-color: #06b025; border-color: #06b025; min-height: 14px; }
notebook > header tab { border-radius: 0; padding: 3px 9px; }
notebook > stack { background-color: #ffffff; }
)css", -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}
inline GtkWidget* styled(GtkWidget* widget, const char* name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
    return widget;
}
inline GtkWidget* text(const char* value, int indent = 0) {
    auto* label = gtk_label_new(value);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_widget_set_margin_start(label, indent);
    return label;
}
// Inno inner-page header: white strip, bold title, indented subtitle, 55x55 small image at the right.
inline GtkWidget* header(GtkWidget*& title, GtkWidget*& subtitle, GtkWidget*& image) {
    auto* strip = styled(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0), "wizard-white");
    auto* words = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_margin_start(words, 22);
    gtk_widget_set_margin_top(words, 12);
    gtk_widget_set_margin_bottom(words, 10);
    title = styled(text(""), "wizard-title");
    subtitle = text("", 16);
    gtk_box_pack_start(GTK_BOX(words), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(words), subtitle, FALSE, FALSE, 0);
    image = gtk_image_new();
    gtk_widget_set_size_request(image, 55, 55);
    gtk_widget_set_margin_top(image, 3);
    gtk_widget_set_margin_bottom(image, 3);
    gtk_widget_set_margin_end(image, 3);
    gtk_box_pack_start(GTK_BOX(strip), words, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(strip), image, FALSE, FALSE, 0);
    auto* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(column), strip, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(column), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    return column;
}
// Image from the install root or preview scratch; nullptr (plain style) when absent or unreadable.
inline GdkPixbuf* load(const std::filesystem::path& file) {
    return file.empty() ? nullptr : gdk_pixbuf_new_from_file(file.c_str(), nullptr);
}
}
