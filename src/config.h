#ifndef DYNAMICWINDOWS_CONFIG_H
#define DYNAMICWINDOWS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

typedef enum {
    IDLE_CLOCK,
    IDLE_DATE,
    IDLE_CLOCK_DATE,
    IDLE_TEXT,
    IDLE_BATTERY,
    IDLE_VOLUME,
    IDLE_BLANK,
    IDLE_HIDDEN
} IdleContent;

typedef enum {
    TINT_BACKGROUND,
    TINT_TEXT,
    TINT_SECONDARY,
    TINT_BORDER,
    TINT_CONTROL,
    TINT_HOVER,
    TINT_ACCENT,
    TINT_TIMER,
    TINT_POWER,
    TINT_COUNT
} ColorRole;

typedef enum {
    SHORTCUT_ISLAND,
    SHORTCUT_TIMER,
    SHORTCUT_HIDE,
    SHORTCUT_SETTINGS,
    SHORTCUT_SEARCH,
    SHORTCUT_CLOSE,
    SHORTCUT_MEDIA_VIEW,
    SHORTCUT_TIMER_VIEW,
    SHORTCUT_SYSTEM_VIEW,
    SHORTCUT_SETTINGS_LOCAL,
    SHORTCUT_SETTINGS_ALT,
    SHORTCUT_PLAY,
    SHORTCUT_LEFT,
    SHORTCUT_RIGHT,
    SHORTCUT_START,
    SHORTCUT_END,
    SHORTCUT_COUNT
} ShortcutAction;

#define SHORTCUT_GLOBAL_COUNT 5

typedef struct {
    unsigned key;
    unsigned modifiers;
} ShortcutBinding;

typedef struct {
    IdleContent idle;
    wchar_t idle_text[128];
    wchar_t idle_font[32];
    wchar_t monitor[32];
    int idle_font_size;
    bool clock_24h;
    bool clock_seconds;
    bool camera_dot;
    double idle_width;
    double media_width;
    double timer_width;
    double compact_height;
    double media_height;
    double timer_height;
    double expanded_width;
    double expanded_height;
    double compact_radius;
    double expanded_radius;
    double bubble_size;
    double bubble_gap;
    double alert_width;
    double alert_height;
    int anchor;
    int x_offset;
    int y_offset;
    int opacity;
    double border_width;
    int shadow;
    uint32_t colors[TINT_COUNT];
    int response_ms;
    int damping;
    int frame_rate;
    bool reduced_motion;
    int hover_delay;
    int leave_delay;
    int volume_alert_ms;
    int power_alert_ms;
    int wheel_step;
    int timer_seconds;
    bool show_media;
    bool show_timer;
    bool dual_activity;
    bool volume_alerts;
    bool power_alerts;
    bool hover_expand;
    bool hide_fullscreen;
    bool close_on_blur;
    bool timer_sound;
    bool waveform;
    bool artwork;
    bool media_source;
    bool media_progress;
    bool search_button;
    bool settings_on_start;
    int change_preview_ms;
    int media_extra_width;
    bool artwork_background;
    bool headphone_alerts;
    bool headphone_status;
    bool headphone_battery;
    int headphone_alert_ms;
    ShortcutBinding shortcuts[SHORTCUT_COUNT];
} AppConfig;

typedef struct {
    double x;
    double y;
    double width;
    double height;
} ConfigRect;

void config_defaults(AppConfig *config);
void config_normalize(AppConfig *config);
bool config_default_path(wchar_t *path, size_t count);
bool config_load(AppConfig *config, const wchar_t *path);
bool config_save(const AppConfig *config, const wchar_t *path);
bool config_shortcut_parse(const wchar_t *text, ShortcutBinding *binding);
void config_shortcut_text(ShortcutBinding binding, wchar_t *text, size_t count);
bool config_shortcuts_valid(const AppConfig *config);
double config_reference_width(const AppConfig *config);
void config_canvas(const AppConfig *config, double *width, double *height);
ConfigRect config_place(const AppConfig *config, ConfigRect work, double scale, double canvas_width,
                        double canvas_height);

#endif
