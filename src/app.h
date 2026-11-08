#ifndef DYNAMICWINDOWS_APP_H
#define DYNAMICWINDOWS_APP_H

#include "config.h"
#include "headphones.h"
#include "island.h"
#include "media.h"
#include "platform.h"

#include <commctrl.h>
#include <gdiplus.h>
#include <oleacc.h>
#include <shellapi.h>

#define UI_WIDTH 512
#define UI_HEIGHT 316
#define UI_TOP 16
#define UI_HITS 24
#define WM_DYNAMICWINDOWS_ACCESS (WM_APP + 2)
#define WM_DYNAMICWINDOWS_TRAY (WM_APP + 3)
#define WM_DYNAMICWINDOWS_OUTSIDE (WM_APP + 4)

#define COLOR_SURFACE 0xff050506u
#define COLOR_TEXT 0xfff5f5f7u
#define COLOR_SECONDARY 0xffa6a6adu
#define COLOR_BORDER 0xff303035u
#define COLOR_CONTROL 0xff222226u
#define COLOR_HOVER 0xff34343bu
#define COLOR_ACCENT 0xff9dd8ffu
#define COLOR_TIMER 0xffffb74du
#define COLOR_GOOD 0xff87dfabu

struct Accessible;

typedef enum {
    ACTION_NONE,
    ACTION_EXPAND,
    ACTION_SECONDARY,
    ACTION_CLOSE,
    ACTION_TAB_MEDIA,
    ACTION_TAB_TIMER,
    ACTION_TAB_SYSTEM,
    ACTION_PLAY,
    ACTION_PREVIOUS,
    ACTION_NEXT,
    ACTION_SEEK,
    ACTION_VOLUME,
    ACTION_MUTE,
    ACTION_TIMER_TOGGLE,
    ACTION_TIMER_RESET,
    ACTION_TIMER_1,
    ACTION_TIMER_5,
    ACTION_TIMER_15,
    ACTION_TIMER_25,
    ACTION_TIMER_DISMISS,
    ACTION_TRACK_INFO,
    ACTION_SEARCH,
    ACTION_SETTINGS
} Action;

typedef struct {
    RectF rect;
    Action action;
    bool enabled;
    wchar_t label[600];
} UiHit;

typedef enum {
    FONT_SMALL,
    FONT_BODY,
    FONT_LABEL,
    FONT_TITLE,
    FONT_LARGE,
    FONT_TIMER,
    FONT_COUNT
} UiFont;

typedef struct {
    ULONG_PTR token;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    BYTE *pixels;
    int width;
    int height;
    GpBitmap *image;
    GpGraphics *graphics;
    GpFontFamily *family;
    GpFont *fonts[FONT_COUNT];
    GpFontFamily *idle_family;
    GpFont *idle_font;
    GpStringFormat *text_format;
    GpImage *artwork;
    IStream *artwork_stream;
    double opacity;
    uint32_t colors[TINT_COUNT];
    double border_width;
    double shadow;
    double canvas_width;
    double canvas_height;
    RectF body;
    RectF bubble;
    double radius;
    bool panel_layout;
    double panel_scale;
    double panel_x;
    double panel_y;
} Renderer;

typedef struct App {
    HINSTANCE instance;
    HWND window;
    HWND settings_window;
    HWND tooltip;
    HWND previous_foreground;
    HMONITOR monitor;
    HICON icon;
    NOTIFYICONDATAW tray;
    UINT taskbar_created;
    bool tray_added;
    bool test_mode;
    bool demo;
    bool motion_allowed;
    unsigned timer_interval;
    uint64_t quit_at;
    bool hidden;
    bool fullscreen_hidden;
    bool quiet_fullscreen;
    bool reduced_motion;
    bool hover_open;
    bool hover_blocked;
    bool hover_enabled;
    bool hotkey_island;
    bool hotkey_timer;
    bool hotkey_hide;
    bool hotkeys[SHORTCUT_GLOBAL_COUNT];
    HHOOK outside_hook;
    POINT base_position;
    uint64_t event_until;
    uint64_t media_peek_until;
    bool initialized;
    double scale;
    double canvas_width;
    double canvas_height;
    AppConfig config;
    wchar_t config_path[1024];
    HANDLE frame_timer;
    LARGE_INTEGER clock_frequency;
    LARGE_INTEGER last_clock;
    Island island;
    IslandSpring width;
    IslandSpring height;
    IslandSpring radius;
    IslandSpring bubble;
    IslandSpring reveal;
    IslandSpring visibility;
    IslandSpring media_peek;
    double media_display_position;
    double media_target_position;
    uint64_t media_position_at;
    double wave_levels[5];
    Platform platform;
    SystemSnapshot system;
    bool system_sampled;
    HeadphoneService headphone_service;
    HeadphoneSnapshot headphones;
    HeadphoneEvent headphone_event;
    MediaService media_service;
    MediaSnapshot media;
    Renderer renderer;
    UiHit hits[UI_HITS];
    int hit_count;
    int focus;
    Action hover;
    Action pressed;
    bool dragging;
    uint64_t last_frame;
    uint64_t pointer_entered;
    uint64_t hover_started;
    uint64_t pointer_left;
    uint64_t last_poll;
    float peaks[10];
    wchar_t tooltip_text[600];
    wchar_t notice[160];
    struct Accessible *accessible;
} App;

bool renderer_init(App *app);
void renderer_destroy(Renderer *renderer);
bool renderer_resize(App *app);
bool renderer_style(App *app);
bool renderer_draw(App *app);
bool renderer_save(App *app, const wchar_t *path);
void renderer_artwork(App *app, const BYTE *data, UINT size);
HICON renderer_icon(App *app);
int app_hit(const App *app, double x, double y);
bool app_inside(const App *app, double x, double y);
void app_action(App *app, Action action, double value);
void app_render(App *app);
void app_open(App *app, IslandView view, bool activate);
void app_close(App *app);
bool app_apply_config(App *app, const AppConfig *config, bool persist);
IslandSize app_target_size(const App *app);
IslandActivity app_primary(const App *app);
IslandActivity app_secondary(const App *app);
void app_animate(App *app);
void app_event(App *app, bool media_change);
bool app_register_shortcuts(App *app);
double app_media_position(const App *app);
void app_media_name(const App *app, wchar_t *text, size_t count);
void app_idle_label(const App *app, wchar_t *text, size_t count);
void app_summary(const App *app, wchar_t *text, size_t count);
void app_clock(uint64_t milliseconds, wchar_t *text, size_t count);
IAccessible *accessibility_create(App *app);
void accessibility_disconnect(App *app);
LRESULT accessibility_object(App *app, WPARAM wparam, LPARAM lparam);

#endif
