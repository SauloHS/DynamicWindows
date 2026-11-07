#include "settings.h"
#include "app.h"

#include <commdlg.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define SETTINGS_PAGE_CLASS L"DynamicWindowsSettingsPage"
#define SETTINGS_PAGE_COUNT 6
#define SETTINGS_MARGIN 16
#define COUNT_OF(items) (sizeof(items) / sizeof((items)[0]))

typedef enum {
    FIELD_INTEGER,
    FIELD_NUMBER,
    FIELD_BOOLEAN,
    FIELD_TEXT,
    FIELD_FONT,
    FIELD_IDLE,
    FIELD_ANCHOR,
    FIELD_MONITOR,
    FIELD_COLOR,
    FIELD_SHORTCUT
} FieldKind;

typedef enum {
    PAGE_IDLE,
    PAGE_LAYOUT,
    PAGE_APPEARANCE,
    PAGE_MOTION,
    PAGE_BEHAVIOR,
    PAGE_SHORTCUTS
} SettingsPage;

typedef struct {
    int id;
    SettingsPage page;
    FieldKind kind;
    size_t offset;
    double minimum;
    double maximum;
    const wchar_t *label;
    const wchar_t *help;
    const wchar_t *section;
} SettingsField;

#define INTEGER(id, page, member, low, high, label, help, section)                                 \
    {id, page, FIELD_INTEGER, offsetof(AppConfig, member), low, high, label, help, section}
#define NUMBER(id, page, member, low, high, label, help, section)                                  \
    {id, page, FIELD_NUMBER, offsetof(AppConfig, member), low, high, label, help, section}
#define BOOLEAN(id, page, member, label, help, section)                                            \
    {id, page, FIELD_BOOLEAN, offsetof(AppConfig, member), 0, 1, label, help, section}
#define STRING(id, page, kind, member, limit, label, help, section)                                \
    {id, page, kind, offsetof(AppConfig, member), 0, limit, label, help, section}
#define CHOICE(id, page, kind, member, high, label, help, section)                                 \
    {id, page, kind, offsetof(AppConfig, member), 0, high, label, help, section}
#define COLOR(id, role, label, section)                                                            \
    {                                                                                              \
        id,          PAGE_APPEARANCE,                                                              \
        FIELD_COLOR, offsetof(AppConfig, colors) + (role) * sizeof(uint32_t),                      \
        0,           0xffffff,                                                                     \
        label,       L"Use #RRGGBB, or choose a color.",                                           \
        section}

#define SHORTCUT(id, action, label, help, section)                                                 \
    {id,                                                                                           \
     PAGE_SHORTCUTS,                                                                               \
     FIELD_SHORTCUT,                                                                               \
     offsetof(AppConfig, shortcuts) + (action) * sizeof(ShortcutBinding),                          \
     0,                                                                                            \
     127,                                                                                          \
     label,                                                                                        \
     help,                                                                                         \
     section}

static const SettingsField fields[] = {
    CHOICE(ID_SETTINGS_IDLE, PAGE_IDLE, FIELD_IDLE, idle, IDLE_HIDDEN, L"&Idle content",
           L"Shown when no activity or alert takes priority.", L"Idle content"),
    STRING(ID_SETTINGS_IDLE_TEXT, PAGE_IDLE, FIELD_TEXT, idle_text, 127, L"&Custom text",
           L"Up to 127 characters. Used by Custom text mode.", NULL),
    STRING(ID_SETTINGS_IDLE_FONT, PAGE_IDLE, FIELD_FONT, idle_font, 31, L"&Typeface",
           L"Installed font name, up to 31 characters.", L"Text and clock"),
    INTEGER(ID_SETTINGS_IDLE_FONT_SIZE, PAGE_IDLE, idle_font_size, 8, 48, L"&Font size",
            L"logical px.", NULL),
    BOOLEAN(ID_SETTINGS_CLOCK_24H, PAGE_IDLE, clock_24h, L"Use a &24-hour clock",
            L"Applies to Clock and Clock + date modes.", NULL),
    BOOLEAN(ID_SETTINGS_CLOCK_SECONDS, PAGE_IDLE, clock_seconds, L"Show clock &seconds",
            L"Applies to Clock and Clock + date modes.", NULL),
    BOOLEAN(ID_SETTINGS_CAMERA_DOT, PAGE_IDLE, camera_dot, L"Show &camera-style dot",
            L"Decorative only; does not monitor the camera.", NULL),

    NUMBER(ID_SETTINGS_IDLE_WIDTH, PAGE_LAYOUT, idle_width, 80, 1200, L"Idle width", L"logical px.",
           L"Compact island"),
    NUMBER(ID_SETTINGS_MEDIA_WIDTH, PAGE_LAYOUT, media_width, 160, 1200, L"Media width",
           L"logical px.", NULL),
    INTEGER(ID_SETTINGS_MEDIA_EXTRA_WIDTH, PAGE_LAYOUT, media_extra_width, 0, 200,
            L"Media preview extra width", L"logical px added during a media change preview.", NULL),
    NUMBER(ID_SETTINGS_TIMER_WIDTH, PAGE_LAYOUT, timer_width, 160, 1200, L"Timer width",
           L"logical px.", NULL),
    NUMBER(ID_SETTINGS_COMPACT_HEIGHT, PAGE_LAYOUT, compact_height, 28, 160, L"Idle height",
           L"logical px. Used when no activity is showing.", NULL),
    NUMBER(ID_SETTINGS_MEDIA_HEIGHT, PAGE_LAYOUT, media_height, 28, 160, L"Media height",
           L"logical px. Used for music or other media activity.", NULL),
    NUMBER(ID_SETTINGS_TIMER_HEIGHT, PAGE_LAYOUT, timer_height, 28, 160, L"Timer height",
           L"logical px. Used while a timer is active.", NULL),
    NUMBER(ID_SETTINGS_COMPACT_RADIUS, PAGE_LAYOUT, compact_radius, 0, 80, L"Compact corner radius",
           L"logical px; at most half the compact height.", NULL),
    NUMBER(ID_SETTINGS_EXPANDED_WIDTH, PAGE_LAYOUT, expanded_width, 408, 1200, L"Expanded width",
           L"logical px. Minimum keeps all controls usable.", L"Expanded island"),
    NUMBER(ID_SETTINGS_EXPANDED_HEIGHT, PAGE_LAYOUT, expanded_height, 264, 800, L"Expanded height",
           L"logical px. Minimum keeps all controls usable.", NULL),
    NUMBER(ID_SETTINGS_EXPANDED_RADIUS, PAGE_LAYOUT, expanded_radius, 0, 400,
           L"Expanded corner radius", L"logical px; at most half the shorter expanded side.", NULL),
    NUMBER(ID_SETTINGS_BUBBLE_SIZE, PAGE_LAYOUT, bubble_size, 28, 128, L"Secondary bubble size",
           L"logical px.", L"Bubble and alerts"),
    NUMBER(ID_SETTINGS_BUBBLE_GAP, PAGE_LAYOUT, bubble_gap, 0, 64, L"Secondary bubble gap",
           L"logical px.", NULL),
    NUMBER(ID_SETTINGS_ALERT_WIDTH, PAGE_LAYOUT, alert_width, 280, 1200, L"Alert width",
           L"logical px.", NULL),
    NUMBER(ID_SETTINGS_ALERT_HEIGHT, PAGE_LAYOUT, alert_height, 76, 240, L"Alert height",
           L"logical px.", NULL),
    CHOICE(ID_SETTINGS_MONITOR, PAGE_LAYOUT, FIELD_MONITOR, monitor, 31, L"&Monitor",
           L"Automatic follows the primary display. Device names select a display.",
           L"Position on screen"),
    CHOICE(ID_SETTINGS_ANCHOR, PAGE_LAYOUT, FIELD_ANCHOR, anchor, 8, L"&Anchor",
           L"Position relative to the selected display's work area.", NULL),
    INTEGER(ID_SETTINGS_X_OFFSET, PAGE_LAYOUT, x_offset, -16000, 16000, L"Horizontal offset",
            L"logical px. Positive moves right; negative moves left.", NULL),
    INTEGER(ID_SETTINGS_Y_OFFSET, PAGE_LAYOUT, y_offset, -16000, 16000, L"Vertical offset",
            L"logical px. Positive moves down; negative moves up.", NULL),

    INTEGER(ID_SETTINGS_OPACITY, PAGE_APPEARANCE, opacity, 10, 100, L"Island opacity",
            L"%. Applies to the entire island.", L"Surface"),
    NUMBER(ID_SETTINGS_BORDER_WIDTH, PAGE_APPEARANCE, border_width, 0, 6, L"Border width",
           L"logical px. Zero removes the border.", NULL),
    INTEGER(ID_SETTINGS_SHADOW, PAGE_APPEARANCE, shadow, 0, 100, L"Shadow strength",
            L"%. Zero removes the shadow.", NULL),
    COLOR(ID_SETTINGS_COLOR_BACKGROUND, TINT_BACKGROUND, L"Background color", L"Colors"),
    COLOR(ID_SETTINGS_COLOR_TEXT, TINT_TEXT, L"Primary text color", NULL),
    COLOR(ID_SETTINGS_COLOR_SECONDARY, TINT_SECONDARY, L"Secondary text color", NULL),
    COLOR(ID_SETTINGS_COLOR_BORDER, TINT_BORDER, L"Border color", NULL),
    COLOR(ID_SETTINGS_COLOR_CONTROL, TINT_CONTROL, L"Control color", NULL),
    COLOR(ID_SETTINGS_COLOR_HOVER, TINT_HOVER, L"Hovered control color", NULL),
    COLOR(ID_SETTINGS_COLOR_ACCENT, TINT_ACCENT, L"Accent color", NULL),
    COLOR(ID_SETTINGS_COLOR_TIMER, TINT_TIMER, L"Timer color", NULL),
    COLOR(ID_SETTINGS_COLOR_POWER, TINT_POWER, L"Power / battery color", NULL),

    INTEGER(ID_SETTINGS_RESPONSE_MS, PAGE_MOTION, response_ms, 150, 1800, L"Motion response",
            L"ms. Lower values settle faster.", L"Animation"),
    INTEGER(ID_SETTINGS_DAMPING, PAGE_MOTION, damping, 50, 100, L"Spring damping",
            L"%. Higher values reduce bounce.", NULL),
    INTEGER(ID_SETTINGS_FRAME_RATE, PAGE_MOTION, frame_rate, 30, 144, L"Frame rate",
            L"frames per second while animating.", NULL),
    BOOLEAN(ID_SETTINGS_REDUCED_MOTION, PAGE_MOTION, reduced_motion, L"&Reduce motion",
            L"Use immediate transitions. Windows' reduced-motion preference is also respected.",
            NULL),

    BOOLEAN(ID_SETTINGS_SHOW_MEDIA, PAGE_BEHAVIOR, show_media, L"Show &media activity",
            L"Show playback activity and its controls.", L"Activities"),
    BOOLEAN(ID_SETTINGS_SHOW_TIMER, PAGE_BEHAVIOR, show_timer, L"Show &timer activity",
            L"Show timer activity and its controls.", NULL),
    BOOLEAN(ID_SETTINGS_DUAL_ACTIVITY, PAGE_BEHAVIOR, dual_activity, L"Show a &secondary activity",
            L"Keep a second activity visible in the side bubble.", NULL),
    INTEGER(ID_SETTINGS_TIMER_SECONDS, PAGE_BEHAVIOR, timer_seconds, 1, 86400, L"Default timer",
            L"seconds. Applies when no timer is running.", NULL),
    BOOLEAN(ID_SETTINGS_TIMER_SOUND, PAGE_BEHAVIOR, timer_sound, L"Play timer completion sound",
            L"Play a sound when the timer finishes.", NULL),
    BOOLEAN(ID_SETTINGS_WAVEFORM, PAGE_BEHAVIOR, waveform, L"Show audio waveform",
            L"Animate the playback waveform when audio is available.", L"Media details"),
    BOOLEAN(ID_SETTINGS_ARTWORK, PAGE_BEHAVIOR, artwork, L"Show media artwork",
            L"Show cover artwork supplied by the player.", NULL),
    BOOLEAN(ID_SETTINGS_ARTWORK_BACKGROUND, PAGE_BEHAVIOR, artwork_background,
            L"Use artwork as media background",
            L"Use cover artwork behind expanded media controls.", NULL),
    BOOLEAN(ID_SETTINGS_MEDIA_SOURCE, PAGE_BEHAVIOR, media_source, L"Show media source",
            L"Show the player or application name.", NULL),
    BOOLEAN(ID_SETTINGS_MEDIA_PROGRESS, PAGE_BEHAVIOR, media_progress, L"Show media progress",
            L"Show the playback progress bar.", NULL),
    INTEGER(ID_SETTINGS_CHANGE_PREVIEW_MS, PAGE_BEHAVIOR, change_preview_ms, 500, 15000,
            L"Media change preview duration", L"ms. How long media changes remain visible.", NULL),
    BOOLEAN(ID_SETTINGS_VOLUME_ALERTS, PAGE_BEHAVIOR, volume_alerts, L"Show &volume alerts",
            L"Show the volume HUD when the volume changes.", L"Alerts and volume"),
    BOOLEAN(ID_SETTINGS_POWER_ALERTS, PAGE_BEHAVIOR, power_alerts, L"Show &power alerts",
            L"Show charging or power alerts. Windows notification toasts are not read.", NULL),
    INTEGER(ID_SETTINGS_VOLUME_ALERT_MS, PAGE_BEHAVIOR, volume_alert_ms, 250, 15000,
            L"Volume alert duration", L"ms.", NULL),
    INTEGER(ID_SETTINGS_POWER_ALERT_MS, PAGE_BEHAVIOR, power_alert_ms, 250, 15000,
            L"Power alert duration", L"ms.", NULL),
    INTEGER(ID_SETTINGS_WHEEL_STEP, PAGE_BEHAVIOR, wheel_step, 1, 20, L"Volume wheel step",
            L"percentage points per wheel notch.", NULL),
    BOOLEAN(ID_SETTINGS_HOVER_EXPAND, PAGE_BEHAVIOR, hover_expand, L"&Expand on hover",
            L"Open the island after the pointer remains over it.", L"Interaction"),
    INTEGER(ID_SETTINGS_HOVER_DELAY, PAGE_BEHAVIOR, hover_delay, 0, 3000, L"Hover expansion delay",
            L"ms. Zero expands immediately.", NULL),
    INTEGER(ID_SETTINGS_LEAVE_DELAY, PAGE_BEHAVIOR, leave_delay, 0, 5000, L"Pointer leave delay",
            L"ms before a hover-opened island collapses.", NULL),
    BOOLEAN(ID_SETTINGS_CLOSE_ON_BLUR, PAGE_BEHAVIOR, close_on_blur, L"Collapse when focus leaves",
            L"Close an expanded island when another window receives focus.", NULL),
    BOOLEAN(ID_SETTINGS_SEARCH_BUTTON, PAGE_BEHAVIOR, search_button, L"Show &search button",
            L"Show the native Windows Search shortcut in the island.", NULL),
    BOOLEAN(ID_SETTINGS_HIDE_FULLSCREEN, PAGE_BEHAVIOR, hide_fullscreen,
            L"&Hide over fullscreen applications", L"Keep the island out of fullscreen work.",
            L"Startup and visibility"),
    BOOLEAN(ID_SETTINGS_SETTINGS_ON_START, PAGE_BEHAVIOR, settings_on_start,
            L"Open settings at startup", L"Show this settings window when DynamicWindows starts.",
            NULL),

    SHORTCUT(ID_SETTINGS_SHORTCUT_ISLAND, SHORTCUT_ISLAND, L"Toggle island (global)",
             L"Works in any application. Type Ctrl+Alt+D, Shift+F2, or None to disable.",
             L"Global shortcuts"),
    SHORTCUT(ID_SETTINGS_SHORTCUT_TIMER, SHORTCUT_TIMER, L"Open timer (global)",
             L"Works in any application. Opens the timer controls; None disables this shortcut.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_HIDE, SHORTCUT_HIDE, L"Hide / show island (global)",
             L"Works in any application. Toggles island visibility; None disables this shortcut.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_SETTINGS, SHORTCUT_SETTINGS, L"Open settings (global)",
             L"Works in any application. Opens this window; None disables this shortcut.", NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_SEARCH, SHORTCUT_SEARCH, L"Windows Search (global)",
             L"Works in any application. Opens Windows Search; None disables this shortcut.", NULL),
    SHORTCUT(
        ID_SETTINGS_SHORTCUT_CLOSE, SHORTCUT_CLOSE, L"Collapse island (local)",
        L"Only with island focus. Type Escape, Shift+F2, or None. Tab and Enter keep navigation.",
        L"Local shortcuts"),
    SHORTCUT(ID_SETTINGS_SHORTCUT_MEDIA_VIEW, SHORTCUT_MEDIA_VIEW, L"Media view (local)",
             L"Only with island focus. Switches to media controls; None disables this shortcut.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_TIMER_VIEW, SHORTCUT_TIMER_VIEW, L"Timer view (local)",
             L"Only with island focus. Switches to timer controls; None disables this shortcut.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_SYSTEM_VIEW, SHORTCUT_SYSTEM_VIEW, L"System view (local)",
             L"Only with island focus. Switches to system controls; None disables this shortcut.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_SETTINGS_LOCAL, SHORTCUT_SETTINGS_LOCAL, L"Open settings (local)",
             L"Only with island focus. Opens this window; type F2 or None to disable.", NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_SETTINGS_ALT, SHORTCUT_SETTINGS_ALT,
             L"Settings alternate (local)",
             L"Only with island focus. Another settings shortcut; type Ctrl+Comma or None.", NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_PLAY, SHORTCUT_PLAY, L"Play / pause (local)",
             L"Only with island focus. Type Space or None. Enter / Space still activate focused "
             L"buttons.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_LEFT, SHORTCUT_LEFT, L"Decrease slider (local)",
             L"Only with island focus. Lowers volume or seeks backward; type Left or None.", NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_RIGHT, SHORTCUT_RIGHT, L"Increase slider (local)",
             L"Only with island focus. Raises volume or seeks forward; type Right or None.", NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_START, SHORTCUT_START, L"Slider start (local)",
             L"Only with island focus. Sets volume or playback to the start; type Home or None.",
             NULL),
    SHORTCUT(ID_SETTINGS_SHORTCUT_END, SHORTCUT_END, L"Slider end (local)",
             L"Only with island focus. Sets volume or playback to the end; type End or None.",
             NULL),
};

#undef INTEGER
#undef NUMBER
#undef BOOLEAN
#undef STRING
#undef CHOICE
#undef COLOR
#undef SHORTCUT

static const wchar_t *const page_names[] = {L"Idle",   L"Layout",   L"Appearance",
                                            L"Motion", L"Behavior", L"Shortcuts"};
static const wchar_t *const idle_choices[] = {
    L"Clock", L"Date", L"Clock + date", L"Custom text", L"Battery", L"Volume", L"Blank", L"Hidden"};
static const wchar_t *const anchor_choices[] = {L"Top left",    L"Top center",    L"Top right",
                                                L"Middle left", L"Center",        L"Middle right",
                                                L"Bottom left", L"Bottom center", L"Bottom right"};

_Static_assert(COUNT_OF(page_names) == SETTINGS_PAGE_COUNT, "Settings tab count must match");
_Static_assert(ID_SETTINGS_PAGE_SHORTCUTS - ID_SETTINGS_PAGE_IDLE + 1 == SETTINGS_PAGE_COUNT,
               "Settings page IDs must match tabs");
_Static_assert(ID_SETTINGS_SHORTCUT_END - ID_SETTINGS_SHORTCUT_ISLAND + 1 == SHORTCUT_COUNT,
               "Every shortcut needs a settings control");
_Static_assert(ID_SETTINGS_COLOR_POWER - ID_SETTINGS_COLOR_BACKGROUND + 1 == TINT_COUNT,
               "Every color needs a settings control");

typedef struct {
    HWND label;
    HWND control;
    HWND help;
    HWND section;
    HWND picker;
    HWND swatch;
    bool invalid;
    bool duplicate;
    int top;
    int bottom;
    int height;
    int help_height;
    wchar_t error[160];
} FieldControl;

typedef struct {
    App *app;
    HWND window;
    HWND tabs;
    HWND pages[SETTINGS_PAGE_COUNT];
    HWND title;
    HWND hint;
    HWND status;
    HWND live_control;
    HWND preview_control;
    HWND apply_control;
    HWND reset_control;
    HWND cancel_control;
    FieldControl controls[COUNT_OF(fields)];
    AppConfig baseline;
    AppConfig draft;
    IAccPropServices *accessibility;
    HFONT font;
    HFONT title_font;
    HFONT section_font;
    HBRUSH invalid_brush;
    COLORREF custom_colors[16];
    UINT dpi;
    int selected_page;
    int scroll[SETTINGS_PAGE_COUNT];
    int content_width[SETTINGS_PAGE_COUNT];
    int wheel[SETTINGS_PAGE_COUNT];
    int first_invalid;
    int default_id;
    IslandView original_view;
    bool original_expanded;
    bool original_hidden;
    bool original_fullscreen_hidden;
    bool original_hover_open;
    bool original_hover_blocked;
    bool live;
    bool valid;
    bool preview_changed;
    bool expansion_changed;
    bool loading;
    bool creating;
    bool ready;
    bool high_contrast;
    bool status_error;
} SettingsState;

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK page_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK control_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR subclass_id, DWORD_PTR reference);
static void layout_window(SettingsState *state);
static void layout_page(SettingsState *state, int page);
static void ensure_visible(SettingsState *state, HWND control);
static void draft_changed(SettingsState *state);

static SettingsState *window_state(HWND window) {
    return (SettingsState *)GetWindowLongPtrW(window, GWLP_USERDATA);
}

static int maximum(int a, int b) {
    return a > b ? a : b;
}

static int minimum(int a, int b) {
    return a < b ? a : b;
}

static int pixels(const SettingsState *state, int logical) {
    return MulDiv(logical, (int)state->dpi, 96);
}

static int logical_pixels(const SettingsState *state, int physical) {
    return MulDiv(physical, 96, (int)state->dpi);
}

static size_t field_index(int id) {
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if (fields[i].id == id)
            return i;
    }
    return COUNT_OF(fields);
}

static void set_text(HWND window, const wchar_t *text) {
    wchar_t previous[320];
    GetWindowTextW(window, previous, (int)COUNT_OF(previous));
    if (wcscmp(previous, text) != 0)
        SetWindowTextW(window, text);
}

static void plain_label(const wchar_t *source, wchar_t *target, size_t count) {
    size_t used = 0;
    while (*source && used + 1 < count) {
        if (*source == L'&') {
            ++source;
            if (*source != L'&')
                continue;
        }
        target[used++] = *source++;
    }
    target[used] = L'\0';
}

static void describe_control(SettingsState *state, HWND control, const wchar_t *label,
                             const wchar_t *help) {
    if (!state->accessibility || !control)
        return;
    wchar_t name[192];
    plain_label(label, name, COUNT_OF(name));
    IAccPropServices_SetHwndPropStr(state->accessibility, control, OBJID_CLIENT, CHILDID_SELF,
                                    PROPID_ACC_NAME, name);
    IAccPropServices_SetHwndPropStr(state->accessibility, control, OBJID_CLIENT, CHILDID_SELF,
                                    PROPID_ACC_DESCRIPTION, help ? help : L"");
}

static HWND create_control(SettingsState *state, HWND parent, const wchar_t *class_name,
                           const wchar_t *text, DWORD style, DWORD extended_style, int id) {
    HWND control =
        CreateWindowExW(extended_style, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                        parent, (HMENU)(INT_PTR)id, state->app->instance, NULL);
    if (!control)
        return NULL;
    SendMessageW(control, WM_SETFONT, (WPARAM)state->font, FALSE);
    if (style & WS_TABSTOP)
        SetWindowSubclass(control, control_proc, 1, (DWORD_PTR)state);
    return control;
}

static BOOL CALLBACK set_child_font(HWND control, LPARAM context) {
    SettingsState *state = (SettingsState *)context;
    SendMessageW(control, WM_SETFONT, (WPARAM)state->font, TRUE);
    return TRUE;
}

static bool update_fonts(SettingsState *state) {
    HFONT font = CreateFontW(-MulDiv(10, (int)state->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT title = CreateFontW(-MulDiv(16, (int)state->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                              FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT section = CreateFontW(-MulDiv(10, (int)state->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!font || !title || !section) {
        if (font)
            DeleteObject(font);
        if (title)
            DeleteObject(title);
        if (section)
            DeleteObject(section);
        return false;
    }
    HFONT old_font = state->font;
    HFONT old_title = state->title_font;
    HFONT old_section = state->section_font;
    memset(state->content_width, 0, sizeof(state->content_width));
    state->font = font;
    state->title_font = title;
    state->section_font = section;
    EnumChildWindows(state->window, set_child_font, (LPARAM)state);
    if (state->title)
        SendMessageW(state->title, WM_SETFONT, (WPARAM)title, TRUE);
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if (state->controls[i].section)
            SendMessageW(state->controls[i].section, WM_SETFONT, (WPARAM)section, TRUE);
    }
    if (old_font)
        DeleteObject(old_font);
    if (old_title)
        DeleteObject(old_title);
    if (old_section)
        DeleteObject(old_section);
    return true;
}

static void update_contrast(SettingsState *state) {
    HIGHCONTRASTW contrast = {.cbSize = sizeof(contrast)};
    state->high_contrast =
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

static bool create_field(SettingsState *state, size_t index) {
    const SettingsField *field = &fields[index];
    FieldControl *control = &state->controls[index];
    HWND parent = state->pages[field->page];
    if (field->section) {
        control->section = create_control(state, parent, L"STATIC", field->section, SS_LEFT, 0,
                                          field->id + ID_SETTINGS_SECTION_OFFSET);
        if (!control->section)
            return false;
        SendMessageW(control->section, WM_SETFONT, (WPARAM)state->section_font, FALSE);
    }
    if (field->kind != FIELD_BOOLEAN) {
        control->label = create_control(state, parent, L"STATIC", field->label, SS_LEFT, 0,
                                        field->id + ID_SETTINGS_LABEL_OFFSET);
        if (!control->label)
            return false;
    }
    DWORD style = WS_TABSTOP;
    const wchar_t *class_name = L"EDIT";
    DWORD extended = WS_EX_CLIENTEDGE;
    switch (field->kind) {
    case FIELD_BOOLEAN:
        class_name = L"BUTTON";
        style |= BS_AUTOCHECKBOX | BS_MULTILINE;
        extended = 0;
        break;
    case FIELD_IDLE:
    case FIELD_ANCHOR:
    case FIELD_MONITOR:
        class_name = WC_COMBOBOXW;
        style |= CBS_DROPDOWNLIST | WS_VSCROLL;
        extended = 0;
        break;
    case FIELD_FONT:
        class_name = WC_COMBOBOXW;
        style |= CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL;
        extended = 0;
        break;
    default:
        style |= ES_AUTOHSCROLL;
        break;
    }
    control->control =
        create_control(state, parent, class_name, field->kind == FIELD_BOOLEAN ? field->label : L"",
                       style, extended, field->id);
    if (!control->control)
        return false;
    if (field->kind == FIELD_FONT) {
        static const wchar_t *const fonts[] = {L"Segoe UI", L"Arial",  L"Calibri",
                                               L"Consolas", L"Tahoma", L"Verdana"};
        for (size_t i = 0; i < COUNT_OF(fonts); ++i)
            SendMessageW(control->control, CB_ADDSTRING, 0, (LPARAM)fonts[i]);
        SendMessageW(control->control, CB_LIMITTEXT, 31, 0);
    } else if (field->kind == FIELD_IDLE || field->kind == FIELD_ANCHOR) {
        const wchar_t *const *choices = field->kind == FIELD_IDLE ? idle_choices : anchor_choices;
        size_t count =
            field->kind == FIELD_IDLE ? COUNT_OF(idle_choices) : COUNT_OF(anchor_choices);
        for (size_t i = 0; i < count; ++i)
            SendMessageW(control->control, CB_ADDSTRING, 0, (LPARAM)choices[i]);
    } else if (field->kind == FIELD_TEXT || field->kind == FIELD_SHORTCUT) {
        SendMessageW(control->control, EM_SETLIMITTEXT, (WPARAM)field->maximum, 0);
    } else if (field->kind == FIELD_INTEGER || field->kind == FIELD_NUMBER ||
               field->kind == FIELD_COLOR) {
        SendMessageW(control->control, EM_SETLIMITTEXT, field->kind == FIELD_COLOR ? 7 : 63, 0);
    }
    describe_control(state, control->control, field->label, field->help);
    if (field->kind == FIELD_COLOR) {
        int role = field->id - ID_SETTINGS_COLOR_BACKGROUND;
        control->swatch = create_control(state, parent, L"STATIC", L"", SS_OWNERDRAW, 0,
                                         ID_SETTINGS_SWATCH_BACKGROUND + role);
        control->picker =
            create_control(state, parent, L"BUTTON", L"Choose...", WS_TABSTOP | BS_PUSHBUTTON, 0,
                           ID_SETTINGS_PICK_BACKGROUND + role);
        if (!control->swatch || !control->picker)
            return false;
        wchar_t label[192];
        swprintf(label, COUNT_OF(label), L"Choose %ls", field->label);
        describe_control(state, control->picker, label, L"Open the native Windows color picker.");
    }
    control->help = create_control(state, parent, L"STATIC", field->help, SS_LEFT | SS_NOPREFIX, 0,
                                   field->id + ID_SETTINGS_HELP_OFFSET);
    return control->help != NULL;
}

static bool create_form(SettingsState *state) {
    state->title =
        create_control(state, state->window, L"STATIC", L"Settings", SS_LEFT, 0, ID_SETTINGS_TITLE);
    state->hint =
        create_control(state, state->window, L"STATIC",
                       L"Customize the island. Apply saves changes; Cancel discards the draft.",
                       SS_LEFT | SS_NOPREFIX, 0, ID_SETTINGS_HINT);
    state->tabs = create_control(state, state->window, WC_TABCONTROLW, L"Settings categories",
                                 WS_TABSTOP | WS_CLIPSIBLINGS | TCS_MULTILINE, 0, ID_SETTINGS_TABS);
    if (!state->title || !state->hint || !state->tabs)
        return false;
    SendMessageW(state->title, WM_SETFONT, (WPARAM)state->title_font, FALSE);
    describe_control(
        state, state->tabs, L"Settings categories",
        L"Idle, Layout, Appearance, Motion, Behavior, Shortcuts. Control+Tab switches categories.");
    for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i) {
        TCITEMW item = {.mask = TCIF_TEXT, .pszText = (wchar_t *)page_names[i]};
        if (TabCtrl_InsertItem(state->tabs, i, &item) < 0)
            return false;
        state->pages[i] = CreateWindowExW(
            WS_EX_CONTROLPARENT, SETTINGS_PAGE_CLASS, page_names[i],
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VSCROLL, 0, 0, 0, 0, state->window,
            (HMENU)(INT_PTR)(ID_SETTINGS_PAGE_IDLE + i), state->app->instance, state);
        if (!state->pages[i])
            return false;
    }
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if (!create_field(state, i))
            return false;
    }
    state->live_control = create_control(state, state->window, L"BUTTON", L"&Live preview",
                                         WS_TABSTOP | BS_AUTOCHECKBOX, 0, ID_SETTINGS_LIVE);
    state->preview_control = create_control(state, state->window, L"BUTTON", L"&Preview expanded",
                                            WS_TABSTOP | BS_PUSHBUTTON, 0, ID_SETTINGS_PREVIEW);
    state->status = create_control(state, state->window, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0,
                                   ID_SETTINGS_STATUS);
    state->reset_control =
        create_control(state, state->window, L"BUTTON", L"Reset &defaults",
                       WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE, 0, ID_SETTINGS_RESET);
    state->apply_control = create_control(state, state->window, L"BUTTON", L"&Apply",
                                          WS_TABSTOP | BS_DEFPUSHBUTTON, 0, ID_SETTINGS_APPLY);
    state->cancel_control = create_control(state, state->window, L"BUTTON", L"&Cancel",
                                           WS_TABSTOP | BS_PUSHBUTTON, 0, ID_SETTINGS_CANCEL);
    if (!state->live_control || !state->preview_control || !state->status ||
        !state->reset_control || !state->apply_control || !state->cancel_control)
        return false;
    describe_control(state, state->live_control, L"Live preview",
                     L"Preview valid edits immediately, without writing settings to disk.");
    describe_control(state, state->preview_control, L"Preview expanded",
                     L"Toggle the island between expanded and compact preview.");
    describe_control(state, state->apply_control, L"Save and apply",
                     L"Save the draft and use it as the new baseline. Keep this window open.");
    describe_control(state, state->reset_control, L"Reset defaults",
                     L"Reset the draft only. Apply is required to save defaults.");
    describe_control(state, state->cancel_control, L"Cancel",
                     L"Discard the draft and restore settings from the last successful Apply.");
    SendMessageW(state->live_control, BM_SETCHECK, BST_CHECKED, 0);
    return true;
}

static BOOL CALLBACK add_monitor(HMONITOR monitor, HDC dc, LPRECT rectangle, LPARAM context) {
    (void)dc;
    (void)rectangle;
    HWND combo = (HWND)context;
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info))
        return TRUE;
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)info.szDevice) == CB_ERR)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)info.szDevice);
    return TRUE;
}

static void refresh_monitors(SettingsState *state, const wchar_t *device) {
    size_t index = field_index(ID_SETTINGS_MONITOR);
    HWND combo = state->controls[index].control;
    bool loading = state->loading;
    state->loading = true;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Primary display (automatic)");
    EnumDisplayMonitors(NULL, NULL, add_monitor, (LPARAM)combo);
    LRESULT selected = 0;
    if (*device) {
        selected = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)device);
        if (selected == CB_ERR)
            selected = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)device);
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
    state->loading = loading;
}

static void fill_form(SettingsState *state, const AppConfig *config) {
    state->loading = true;
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        const SettingsField *field = &fields[i];
        HWND control = state->controls[i].control;
        const unsigned char *value = (const unsigned char *)config + field->offset;
        wchar_t text[160];
        switch (field->kind) {
        case FIELD_BOOLEAN:
            SendMessageW(control, BM_SETCHECK, *(const bool *)value ? BST_CHECKED : BST_UNCHECKED,
                         0);
            break;
        case FIELD_IDLE:
            SendMessageW(control, CB_SETCURSEL, *(const IdleContent *)value, 0);
            break;
        case FIELD_ANCHOR:
            SendMessageW(control, CB_SETCURSEL, *(const int *)value, 0);
            break;
        case FIELD_MONITOR:
            refresh_monitors(state, (const wchar_t *)value);
            break;
        case FIELD_TEXT:
        case FIELD_FONT:
            SetWindowTextW(control, (const wchar_t *)value);
            break;
        case FIELD_INTEGER:
            swprintf(text, COUNT_OF(text), L"%d", *(const int *)value);
            SetWindowTextW(control, text);
            break;
        case FIELD_NUMBER:
            swprintf(text, COUNT_OF(text), L"%.12g", *(const double *)value);
            SetWindowTextW(control, text);
            break;
        case FIELD_SHORTCUT:
            config_shortcut_text(*(const ShortcutBinding *)value, text, COUNT_OF(text));
            SetWindowTextW(control, text);
            break;
        case FIELD_COLOR:
            swprintf(text, COUNT_OF(text), L"#%06lX", (unsigned long)*(const uint32_t *)value);
            SetWindowTextW(control, text);
            break;
        }
    }
    state->loading = false;
    memset(state->content_width, 0, sizeof(state->content_width));
}

static bool read_monitor(HWND combo, wchar_t *device, size_t count) {
    LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR)
        return false;
    if (selected == 0) {
        *device = L'\0';
        return true;
    }
    LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)selected, 0);
    if (length <= 0 || (size_t)length >= count)
        return false;
    return SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selected, (LPARAM)device) != CB_ERR;
}

static bool read_color(const wchar_t *text, uint32_t *value) {
    if (wcslen(text) != 7 || text[0] != L'#')
        return false;
    uint32_t color = 0;
    for (int i = 1; i < 7; ++i) {
        wchar_t digit = towupper(text[i]);
        int number = digit >= L'0' && digit <= L'9'   ? digit - L'0'
                     : digit >= L'A' && digit <= L'F' ? digit - L'A' + 10
                                                      : -1;
        if (number < 0)
            return false;
        color = color * 16 + (uint32_t)number;
    }
    *value = color;
    return true;
}

static bool read_number(const wchar_t *text, const SettingsField *field, double *result) {
    while (iswspace(*text))
        ++text;
    if (!*text)
        return false;
    errno = 0;
    wchar_t *end = NULL;
    double value;
    if (field->kind == FIELD_INTEGER)
        value = (double)wcstol(text, &end, 10);
    else
        value = wcstod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(value))
        return false;
    while (iswspace(*end))
        ++end;
    if (*end || value < field->minimum || value > field->maximum)
        return false;
    *result = value;
    return true;
}

static bool read_field(SettingsState *state, size_t index, AppConfig *config) {
    const SettingsField *field = &fields[index];
    HWND control = state->controls[index].control;
    unsigned char *destination = (unsigned char *)config + field->offset;
    wchar_t text[160];
    switch (field->kind) {
    case FIELD_BOOLEAN:
        *(bool *)destination = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    case FIELD_IDLE:
    case FIELD_ANCHOR: {
        LRESULT choice = SendMessageW(control, CB_GETCURSEL, 0, 0);
        if (choice < 0 || choice > (LRESULT)field->maximum)
            return false;
        if (field->kind == FIELD_IDLE)
            *(IdleContent *)destination = (IdleContent)choice;
        else
            *(int *)destination = (int)choice;
        return true;
    }
    case FIELD_MONITOR:
        return read_monitor(control, (wchar_t *)destination, 32);
    default:
        break;
    }
    int length = GetWindowTextLengthW(control);
    if (length < 0 || length >= (int)COUNT_OF(text))
        return false;
    GetWindowTextW(control, text, (int)COUNT_OF(text));
    if (field->kind == FIELD_TEXT || field->kind == FIELD_FONT) {
        if (length > (int)field->maximum || (field->kind == FIELD_FONT && !*text))
            return false;
        if (field->kind == FIELD_FONT) {
            const wchar_t *letter = text;
            while (*letter && iswspace(*letter))
                ++letter;
            if (!*letter)
                return false;
        }
        memcpy(destination, text, ((size_t)length + 1) * sizeof(wchar_t));
        return true;
    }
    if (field->kind == FIELD_SHORTCUT)
        return config_shortcut_parse(text, (ShortcutBinding *)destination);
    if (field->kind == FIELD_COLOR)
        return read_color(text, (uint32_t *)destination);
    double number;
    if (!read_number(text, field, &number))
        return false;
    if (field->kind == FIELD_INTEGER)
        *(int *)destination = (int)number;
    else
        *(double *)destination = number;
    return true;
}

static void field_help(const SettingsField *field, const AppConfig *config, bool invalid,
                       wchar_t *text, size_t count) {
    double upper = field->maximum;
    if (field->id == ID_SETTINGS_COMPACT_RADIUS)
        upper = config->compact_height / 2;
    else if (field->id == ID_SETTINGS_EXPANDED_RADIUS)
        upper = fmin(config->expanded_width, config->expanded_height) / 2;
    if (field->kind == FIELD_INTEGER || field->kind == FIELD_NUMBER) {
        if (invalid)
            swprintf(text, count, L"Enter %ls from %.12g to %.12g. %ls",
                     field->kind == FIELD_INTEGER ? L"a whole number" : L"a number", field->minimum,
                     upper, field->help);
        else
            swprintf(text, count, L"%.12g - %.12g %ls", field->minimum, upper, field->help);
        return;
    }
    if (!invalid) {
        swprintf(text, count, L"%ls", field->help);
        return;
    }
    switch (field->kind) {
    case FIELD_SHORTCUT:
        swprintf(text, count,
                 L"Type Ctrl+Alt+H, Shift+F2, Escape, Space, Left, Right, Home, End, or None.");
        break;
    case FIELD_COLOR:
        swprintf(text, count, L"Enter # followed by six hexadecimal digits, for example #9DD8FF.");
        break;
    case FIELD_FONT:
        swprintf(text, count, L"Enter a font name with 1 to 31 characters, for example Segoe UI.");
        break;
    case FIELD_TEXT:
        swprintf(text, count, L"Use no more than 127 characters.");
        break;
    default:
        swprintf(text, count, L"Choose an option from the list.");
        break;
    }
}

static void update_field_help(SettingsState *state, size_t index, const AppConfig *config) {
    FieldControl *control = &state->controls[index];
    wchar_t text[160];
    field_help(&fields[index], config, control->invalid, text, COUNT_OF(text));
    if (control->duplicate)
        swprintf(text, COUNT_OF(text),
                 L"Another %ls shortcut uses this binding. Choose a different binding or None.",
                 fields[index].id < ID_SETTINGS_SHORTCUT_ISLAND + SHORTCUT_GLOBAL_COUNT ? L"global"
                                                                                        : L"local");
    set_text(control->help, text);
    describe_control(state, control->control, fields[index].label, text);
    if (control->invalid)
        swprintf(control->error, COUNT_OF(control->error), L"%ls", text);
    else
        *control->error = L'\0';
    state->content_width[fields[index].page] = 0;
    InvalidateRect(control->control, NULL, TRUE);
    InvalidateRect(control->help, NULL, TRUE);
    if (control->swatch)
        InvalidateRect(control->swatch, NULL, TRUE);
}

static void validate_shortcuts(SettingsState *state, const AppConfig *config) {
    if (config_shortcuts_valid(config))
        return;
    for (int i = 0; i < SHORTCUT_COUNT; ++i) {
        size_t index = field_index(ID_SETTINGS_SHORTCUT_ISLAND + i);
        ShortcutBinding binding = config->shortcuts[i];
        if (state->controls[index].invalid || !binding.key)
            continue;
        for (int j = i + 1; j < SHORTCUT_COUNT; ++j) {
            if ((i < SHORTCUT_GLOBAL_COUNT) != (j < SHORTCUT_GLOBAL_COUNT))
                continue;
            size_t other = field_index(ID_SETTINGS_SHORTCUT_ISLAND + j);
            if (state->controls[other].invalid)
                continue;
            if (binding.key != config->shortcuts[j].key ||
                binding.modifiers != config->shortcuts[j].modifiers)
                continue;
            state->controls[index].duplicate = true;
            state->controls[other].duplicate = true;
        }
    }
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if (state->controls[i].duplicate)
            state->controls[i].invalid = true;
    }
}

static bool validate_form(SettingsState *state, AppConfig *config) {
    *config = state->baseline;
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        state->controls[i].invalid = !read_field(state, i, config);
        state->controls[i].duplicate = false;
    }
    validate_shortcuts(state, config);
    size_t radius = field_index(ID_SETTINGS_COMPACT_RADIUS);
    size_t height = field_index(ID_SETTINGS_COMPACT_HEIGHT);
    if (!state->controls[height].invalid && config->compact_radius > config->compact_height / 2)
        state->controls[radius].invalid = true;
    radius = field_index(ID_SETTINGS_EXPANDED_RADIUS);
    size_t width = field_index(ID_SETTINGS_EXPANDED_WIDTH);
    height = field_index(ID_SETTINGS_EXPANDED_HEIGHT);
    if (!state->controls[width].invalid && !state->controls[height].invalid &&
        config->expanded_radius > fmin(config->expanded_width, config->expanded_height) / 2)
        state->controls[radius].invalid = true;
    state->first_invalid = -1;
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        update_field_help(state, i, config);
        if (state->controls[i].invalid && state->first_invalid < 0)
            state->first_invalid = (int)i;
    }
    state->valid = state->first_invalid < 0;
    EnableWindow(state->apply_control, state->valid);
    EnableWindow(state->preview_control, state->valid && state->live);
    if (state->valid)
        state->draft = *config;
    layout_page(state, state->selected_page);
    return state->valid;
}

static void status_text(SettingsState *state, const wchar_t *text, bool error) {
    wchar_t previous[320];
    GetWindowTextW(state->status, previous, (int)COUNT_OF(previous));
    state->status_error = error;
    set_text(state->status, text);
    InvalidateRect(state->status, NULL, TRUE);
    if (wcscmp(previous, text) == 0)
        return;
    layout_window(state);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, state->status, OBJID_CLIENT, CHILDID_SELF);
    if (error)
        NotifyWinEvent(EVENT_SYSTEM_ALERT, state->status, OBJID_CLIENT, CHILDID_SELF);
}

static void invalid_status(SettingsState *state) {
    if (state->first_invalid < 0)
        return;
    size_t index = (size_t)state->first_invalid;
    wchar_t label[192];
    wchar_t text[320];
    plain_label(fields[index].label, label, COUNT_OF(label));
    swprintf(text, COUNT_OF(text), L"%ls / %ls: %ls", page_names[fields[index].page], label,
             state->controls[index].error);
    status_text(state, text, true);
}

static void move_control(SettingsState *state, HWND control, int x, int y, int width, int height) {
    if (!control)
        return;
    SetWindowPos(control, NULL, pixels(state, x), pixels(state, y),
                 pixels(state, maximum(1, width)), pixels(state, maximum(1, height)),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
}

static int wrapped_height(SettingsState *state, HDC dc, HWND control, int width, int floor) {
    if (!dc)
        return floor;
    wchar_t text[320];
    GetWindowTextW(control, text, (int)COUNT_OF(text));
    RECT rectangle = {0, 0, maximum(1, pixels(state, width)), 0};
    DrawTextW(dc, text, -1, &rectangle, DT_CALCRECT | DT_WORDBREAK);
    return maximum(floor, logical_pixels(state, rectangle.bottom - rectangle.top) + 2);
}

static int page_content_height(SettingsState *state, int page, int width) {
    bool narrow = width < 520;
    int available = maximum(1, width - 2 * SETTINGS_MARGIN);
    int input_x = narrow ? SETTINGS_MARGIN : 240;
    int input_width = maximum(1, width - SETTINGS_MARGIN - input_x);
    HDC dc = GetDC(state->pages[page]);
    HGDIOBJ previous = dc ? SelectObject(dc, state->font) : NULL;
    int height = 12;
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if ((int)fields[i].page != page)
            continue;
        FieldControl *control = &state->controls[i];
        if (fields[i].section)
            height += 40;
        int help_width = fields[i].kind == FIELD_BOOLEAN ? maximum(1, available - 24) : input_width;
        control->help_height = wrapped_height(state, dc, control->help, help_width, 18);
        if (fields[i].kind == FIELD_BOOLEAN) {
            int check_height =
                wrapped_height(state, dc, control->control, maximum(1, available - 24), 24);
            control->height = check_height + 4 + control->help_height + 12;
        } else {
            control->height = (narrow ? 24 : 0) + 32 + control->help_height + 12;
        }
        height += control->height;
    }
    if (dc) {
        SelectObject(dc, previous);
        ReleaseDC(state->pages[page], dc);
    }
    state->content_width[page] = width;
    return pixels(state, height + 12);
}

static int measured_content_height(SettingsState *state, int page, int width) {
    if (state->content_width[page] != width)
        return page_content_height(state, page, width);
    int height = 24;
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        if ((int)fields[i].page == page)
            height += state->controls[i].height + (fields[i].section ? 40 : 0);
    }
    return pixels(state, height);
}

static void layout_page(SettingsState *state, int page) {
    if (!state->ready || !state->pages[page])
        return;
    HWND window = state->pages[page];
    RECT client;
    GetClientRect(window, &client);
    int width = logical_pixels(state, client.right);
    int content = measured_content_height(state, page, width);
    SCROLLINFO info = {.cbSize = sizeof(info),
                       .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
                       .nMin = 0,
                       .nMax = maximum(0, content - 1),
                       .nPage = (UINT)client.bottom,
                       .nPos = state->scroll[page]};
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    GetClientRect(window, &client);
    int final_width = logical_pixels(state, client.right);
    if (final_width != width) {
        width = final_width;
        content = page_content_height(state, page, width);
        info.nMax = maximum(0, content - 1);
        info.nPage = (UINT)client.bottom;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    state->scroll[page] = GetScrollPos(window, SB_VERT);
    bool narrow = width < 520;
    int offset = logical_pixels(state, state->scroll[page]);
    int content_y = 12;
    int left = SETTINGS_MARGIN;
    int available = maximum(1, width - 2 * left);
    for (size_t i = 0; i < COUNT_OF(fields); ++i) {
        const SettingsField *field = &fields[i];
        FieldControl *control = &state->controls[i];
        if ((int)field->page != page)
            continue;
        if (control->section) {
            move_control(state, control->section, left, content_y - offset + 10, available, 24);
            content_y += 40;
        }
        int y = content_y - offset;
        control->top = pixels(state, content_y);
        control->bottom = pixels(state, content_y + control->height);
        if (field->kind == FIELD_BOOLEAN) {
            int check_height = control->height - control->help_height - 16;
            move_control(state, control->control, left, y, available, check_height);
            move_control(state, control->help, left + 24, y + check_height + 4,
                         maximum(1, available - 24), control->help_height);
            content_y += control->height;
            continue;
        }
        int input_x = narrow ? left : 240;
        int input_y = y + (narrow ? 24 : 0);
        int input_width = maximum(1, width - left - input_x);
        move_control(state, control->label, left, y + 4, narrow ? available : 214, 22);
        int box_width = field->kind == FIELD_TEXT || field->kind == FIELD_FONT ||
                                field->kind == FIELD_IDLE || field->kind == FIELD_ANCHOR ||
                                field->kind == FIELD_MONITOR
                            ? input_width
                            : minimum(160, input_width);
        int box_height = field->kind == FIELD_IDLE || field->kind == FIELD_ANCHOR ||
                                 field->kind == FIELD_MONITOR || field->kind == FIELD_FONT
                             ? 220
                             : 26;
        if (field->kind == FIELD_COLOR) {
            box_width = minimum(120, maximum(60, input_width - 132));
            move_control(state, control->swatch, input_x + box_width + 8, input_y + 2, 24, 24);
            move_control(state, control->picker, input_x + box_width + 40, input_y,
                         maximum(50, minimum(92, input_width - box_width - 40)), 28);
        }
        move_control(state, control->control, input_x, input_y, box_width, box_height);
        move_control(state, control->help, input_x, input_y + 32, input_width,
                     control->help_height);
        content_y += control->height;
    }
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void layout_window(SettingsState *state) {
    if (!state->ready)
        return;
    RECT client;
    GetClientRect(state->window, &client);
    int width = logical_pixels(state, client.right);
    int height = logical_pixels(state, client.bottom);
    bool narrow = width < 520;
    int available = maximum(1, width - 2 * SETTINGS_MARGIN);
    HDC dc = GetDC(state->window);
    HGDIOBJ previous = dc ? SelectObject(dc, state->font) : NULL;
    int hint_height = wrapped_height(state, dc, state->hint, available, 24);
    int status_height = wrapped_height(state, dc, state->status, available, 34);
    if (dc) {
        SelectObject(dc, previous);
        ReleaseDC(state->window, dc);
    }
    int header = maximum(narrow ? 96 : 80, 54 + hint_height);
    int footer_height = (narrow ? 124 : 96) + status_height;
    int footer = maximum(header + 64, height - footer_height);
    move_control(state, state->title, SETTINGS_MARGIN, 12, available, 30);
    move_control(state, state->hint, SETTINGS_MARGIN, 46, available, hint_height);
    move_control(state, state->tabs, SETTINGS_MARGIN, header, available,
                 maximum(54, footer - header - 8));
    RECT page;
    GetClientRect(state->tabs, &page);
    TabCtrl_AdjustRect(state->tabs, FALSE, &page);
    MapWindowPoints(state->tabs, state->window, (POINT *)&page, 2);
    for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i) {
        MoveWindow(state->pages[i], page.left, page.top, maximum(1, page.right - page.left),
                   maximum(1, page.bottom - page.top), TRUE);
        ShowWindow(state->pages[i], i == state->selected_page ? SW_SHOW : SW_HIDE);
        layout_page(state, i);
    }
    SetWindowPos(state->tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (narrow) {
        move_control(state, state->live_control, SETTINGS_MARGIN, footer + 6, available, 26);
        move_control(state, state->preview_control, SETTINGS_MARGIN, footer + 38,
                     minimum(200, available), 28);
        move_control(state, state->status, SETTINGS_MARGIN, footer + 74, available, status_height);
        int button_y = footer + 82 + status_height;
        int button_width = maximum(1, (available - 16) / 3);
        move_control(state, state->reset_control, SETTINGS_MARGIN, button_y, button_width, 30);
        move_control(state, state->apply_control, SETTINGS_MARGIN + button_width + 8, button_y,
                     button_width, 30);
        move_control(state, state->cancel_control, SETTINGS_MARGIN + 2 * (button_width + 8),
                     button_y, button_width, 30);
    } else {
        move_control(state, state->live_control, SETTINGS_MARGIN, footer + 8, 170, 28);
        move_control(state, state->preview_control, 198, footer + 8, 188, 28);
        move_control(state, state->status, SETTINGS_MARGIN, footer + 46, available, status_height);
        int button_y = footer + 54 + status_height;
        move_control(state, state->reset_control, SETTINGS_MARGIN, button_y, 130, 30);
        move_control(state, state->apply_control, width - SETTINGS_MARGIN - 210, button_y, 104, 30);
        move_control(state, state->cancel_control, width - SETTINGS_MARGIN - 96, button_y, 96, 30);
    }
    RedrawWindow(state->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static void scroll_page(SettingsState *state, int page, int position) {
    if (position == state->scroll[page])
        return;
    SCROLLINFO info = {.cbSize = sizeof(info), .fMask = SIF_POS, .nPos = position};
    SetScrollInfo(state->pages[page], SB_VERT, &info, TRUE);
    int actual = GetScrollPos(state->pages[page], SB_VERT);
    if (actual == state->scroll[page])
        return;
    state->scroll[page] = actual;
    layout_page(state, page);
    RedrawWindow(state->pages[page], NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void ensure_visible(SettingsState *state, HWND control) {
    HWND parent = GetParent(control);
    int page = GetDlgCtrlID(parent) - ID_SETTINGS_PAGE_IDLE;
    if (page < 0 || page >= SETTINGS_PAGE_COUNT)
        return;
    int id = GetDlgCtrlID(control);
    if (id >= ID_SETTINGS_PICK_BACKGROUND && id <= ID_SETTINGS_PICK_POWER)
        id = ID_SETTINGS_COLOR_BACKGROUND + id - ID_SETTINGS_PICK_BACKGROUND;
    size_t index = field_index(id);
    if (index == COUNT_OF(fields))
        return;
    RECT client;
    GetClientRect(parent, &client);
    int top = state->controls[index].top;
    int bottom = state->controls[index].bottom;
    int margin = pixels(state, 8);
    if (top < state->scroll[page] + margin)
        scroll_page(state, page, maximum(0, top - margin));
    else if (bottom > state->scroll[page] + client.bottom - margin)
        scroll_page(state, page, bottom - client.bottom + margin);
}

static void select_page(SettingsState *state, int page, bool focus_tab) {
    if (page < 0 || page >= SETTINGS_PAGE_COUNT)
        return;
    state->selected_page = page;
    TabCtrl_SetCurSel(state->tabs, page);
    layout_window(state);
    if (focus_tab)
        SetFocus(state->tabs);
}

static void update_preview_button(SettingsState *state) {
    const wchar_t *label = state->app->island.expanded ? L"&Preview compact" : L"&Preview expanded";
    set_text(state->preview_control, label);
    describe_control(state, state->preview_control, label,
                     L"Toggle the island between expanded and compact preview.");
    EnableWindow(state->preview_control, state->live && state->valid);
}

static void restore_expansion(SettingsState *state) {
    bool needs_fullscreen_refresh =
        state->original_fullscreen_hidden && !state->app->fullscreen_hidden;
    if (!state->expansion_changed && !needs_fullscreen_refresh)
        return;
    if (state->original_expanded)
        app_open(state->app, state->original_view, false);
    else
        app_close(state->app);
    state->app->hidden = state->original_hidden;
    state->app->fullscreen_hidden = state->original_fullscreen_hidden;
    state->app->hover_open = state->original_hover_open;
    state->app->hover_blocked = state->original_hover_blocked;
    if (state->original_hidden || state->original_fullscreen_hidden)
        ShowWindow(state->app->window, SW_HIDE);
    state->expansion_changed = false;
    update_preview_button(state);
}

static bool revert_preview(SettingsState *state) {
    if (state->preview_changed && !app_apply_config(state->app, &state->baseline, false)) {
        status_text(state,
                    L"Could not restore the baseline. Keep this window open and try Cancel again.",
                    true);
        return false;
    }
    state->preview_changed = false;
    restore_expansion(state);
    return true;
}

static void draft_changed(SettingsState *state) {
    if (state->loading)
        return;
    AppConfig config;
    if (!validate_form(state, &config)) {
        invalid_status(state);
        return;
    }
    if (state->live) {
        state->preview_changed = true;
        if (!app_apply_config(state->app, &config, false)) {
            status_text(
                state,
                L"The preview could not be updated. A global shortcut may be in use. Nothing "
                L"was saved; Cancel restores the baseline.",
                true);
            return;
        }
        update_preview_button(state);
        status_text(state, L"Unsaved changes. Live preview updated; Apply saves your choices.",
                    false);
        return;
    }
    status_text(state, L"Unsaved draft. Preview is off; Apply saves your choices.", false);
}

static void apply_draft(SettingsState *state) {
    AppConfig config;
    if (!validate_form(state, &config)) {
        invalid_status(state);
        if (state->first_invalid >= 0) {
            size_t index = (size_t)state->first_invalid;
            select_page(state, fields[index].page, false);
            SetFocus(state->controls[index].control);
            ensure_visible(state, state->controls[index].control);
        }
        return;
    }
    if (!app_apply_config(state->app, &config, true)) {
        state->preview_changed = true;
        status_text(
            state,
            L"Could not save and apply settings. A global shortcut may be in use, or the settings "
            L"folder may not be writable. Try another binding or check the folder, then Apply "
            L"again.",
            true);
        return;
    }
    state->baseline = state->app->config;
    state->draft = state->baseline;
    state->preview_changed = false;
    fill_form(state, &state->baseline);
    validate_form(state, &config);
    update_preview_button(state);
    status_text(
        state,
        L"Saved. These choices are now the baseline; you can keep editing or close this window.",
        false);
}

static void toggle_live(SettingsState *state) {
    bool enabled = SendMessageW(state->live_control, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!enabled) {
        if (!revert_preview(state)) {
            SendMessageW(state->live_control, BM_SETCHECK, BST_CHECKED, 0);
            return;
        }
        state->live = false;
        update_preview_button(state);
        if (state->valid)
            status_text(state, L"Live preview is off. The draft is unchanged; Apply saves it.",
                        false);
        else
            invalid_status(state);
        return;
    }
    state->live = true;
    draft_changed(state);
}

static void toggle_expansion(SettingsState *state) {
    if (!state->live)
        return;
    AppConfig config;
    if (!validate_form(state, &config)) {
        invalid_status(state);
        return;
    }
    state->preview_changed = true;
    if (!app_apply_config(state->app, &config, false)) {
        status_text(state,
                    L"The preview could not be updated. A global shortcut may be in use. Nothing "
                    L"was saved.",
                    true);
        return;
    }
    state->expansion_changed = true;
    if (state->app->island.expanded)
        app_close(state->app);
    else
        app_open(state->app, state->app->island.view, false);
    update_preview_button(state);
    status_text(state, L"Preview only. Apply saves settings; Cancel restores the baseline.", false);
}

static COLORREF native_color(uint32_t color) {
    return RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
}

static uint32_t control_color(SettingsState *state, int role) {
    wchar_t text[32];
    size_t index = field_index(ID_SETTINGS_COLOR_BACKGROUND + role);
    GetWindowTextW(state->controls[index].control, text, (int)COUNT_OF(text));
    uint32_t color;
    return read_color(text, &color) ? color : state->draft.colors[role];
}

static void choose_color(SettingsState *state, int role) {
    HWND window = state->window;
    CHOOSECOLORW color = {.lStructSize = sizeof(color),
                          .hwndOwner = window,
                          .rgbResult = native_color(control_color(state, role)),
                          .lpCustColors = state->custom_colors,
                          .Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR};
    BOOL selected = ChooseColorW(&color);
    if (!IsWindow(window) || window_state(window) != state)
        return;
    if (!selected) {
        if (CommDlgExtendedError())
            status_text(state,
                        L"The Windows color picker could not open. You can still edit #RRGGBB.",
                        true);
        return;
    }
    uint32_t value = ((uint32_t)GetRValue(color.rgbResult) << 16) |
                     ((uint32_t)GetGValue(color.rgbResult) << 8) | GetBValue(color.rgbResult);
    wchar_t text[16];
    swprintf(text, COUNT_OF(text), L"#%06lX", (unsigned long)value);
    size_t index = field_index(ID_SETTINGS_COLOR_BACKGROUND + role);
    SetWindowTextW(state->controls[index].control, text);
}

static bool draw_swatch(SettingsState *state, const DRAWITEMSTRUCT *item) {
    if (item->CtlID < ID_SETTINGS_SWATCH_BACKGROUND || item->CtlID > ID_SETTINGS_SWATCH_POWER)
        return false;
    int role = (int)item->CtlID - ID_SETTINGS_SWATCH_BACKGROUND;
    HBRUSH brush = CreateSolidBrush(native_color(control_color(state, role)));
    if (brush) {
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
    }
    FrameRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_WINDOWTEXT));
    return true;
}

static LRESULT control_colors(SettingsState *state, UINT message, WPARAM wparam, LPARAM lparam) {
    HWND control = (HWND)lparam;
    HDC dc = (HDC)wparam;
    int id = GetDlgCtrlID(control);
    bool page = GetParent(control) != state->window;
    bool invalid = false;
    size_t index = field_index(id);
    if (index == COUNT_OF(fields))
        index = field_index(id - ID_SETTINGS_HELP_OFFSET);
    if (index < COUNT_OF(fields))
        invalid = state->controls[index].invalid;
    COLORREF background = GetSysColor(page ? COLOR_WINDOW : COLOR_BTNFACE);
    COLORREF foreground = GetSysColor(page ? COLOR_WINDOWTEXT : COLOR_BTNTEXT);
    bool error = invalid || (control == state->status && state->status_error);
    if (error && !state->high_contrast)
        foreground = RGB(174, 28, 28);
    else if (!error &&
             (id == ID_SETTINGS_HINT ||
              (index < COUNT_OF(fields) && id == fields[index].id + ID_SETTINGS_HELP_OFFSET)))
        foreground = state->high_contrast ? foreground : RGB(88, 88, 88);
    if (message == WM_CTLCOLOREDIT) {
        background =
            invalid && !state->high_contrast ? RGB(255, 239, 239) : GetSysColor(COLOR_WINDOW);
        SetTextColor(dc, foreground);
        SetBkColor(dc, background);
        return (LRESULT)(invalid && !state->high_contrast ? state->invalid_brush
                                                          : GetSysColorBrush(COLOR_WINDOW));
    }
    SetTextColor(dc, foreground);
    SetBkColor(dc, background);
    return (LRESULT)GetSysColorBrush(page ? COLOR_WINDOW : COLOR_BTNFACE);
}

static void close_settings(SettingsState *state) {
    if (revert_preview(state))
        DestroyWindow(state->window);
}

static void handle_command(SettingsState *state, WPARAM wparam, LPARAM lparam) {
    int id = LOWORD(wparam);
    int notification = HIWORD(wparam);
    HWND control = (HWND)lparam;
    if (notification == EN_SETFOCUS || notification == CBN_SETFOCUS)
        ensure_visible(state, control);
    if (state->loading)
        return;
    if (id == ID_SETTINGS_APPLY || id == IDOK) {
        if (notification == BN_CLICKED)
            apply_draft(state);
        return;
    }
    if (id == ID_SETTINGS_CANCEL || id == IDCANCEL) {
        if (notification == BN_CLICKED)
            close_settings(state);
        return;
    }
    if (notification == BN_CLICKED) {
        if (id == ID_SETTINGS_RESET) {
            AppConfig defaults;
            config_defaults(&defaults);
            fill_form(state, &defaults);
            draft_changed(state);
            if (state->valid && !state->status_error)
                status_text(state,
                            L"Defaults loaded into the draft only. Apply saves them; Cancel "
                            L"restores the baseline.",
                            false);
            return;
        }
        if (id == ID_SETTINGS_LIVE) {
            toggle_live(state);
            return;
        }
        if (id == ID_SETTINGS_PREVIEW) {
            toggle_expansion(state);
            return;
        }
        if (id >= ID_SETTINGS_PICK_BACKGROUND && id <= ID_SETTINGS_PICK_POWER) {
            choose_color(state, id - ID_SETTINGS_PICK_BACKGROUND);
            return;
        }
    }
    size_t index = field_index(id);
    if (index == COUNT_OF(fields))
        return;
    FieldKind kind = fields[index].kind;
    if (kind == FIELD_FONT && notification == CBN_SELCHANGE) {
        LRESULT choice = SendMessageW(control, CB_GETCURSEL, 0, 0);
        LRESULT length = SendMessageW(control, CB_GETLBTEXTLEN, (WPARAM)choice, 0);
        if (choice != CB_ERR && length >= 0 && length < 32) {
            wchar_t text[32];
            SendMessageW(control, CB_GETLBTEXT, (WPARAM)choice, (LPARAM)text);
            state->loading = true;
            SetWindowTextW(control, text);
            state->loading = false;
        }
    }
    bool changed = kind == FIELD_BOOLEAN ? notification == BN_CLICKED
                   : kind == FIELD_IDLE || kind == FIELD_ANCHOR || kind == FIELD_MONITOR
                       ? notification == CBN_SELCHANGE
                   : kind == FIELD_FONT
                       ? notification == CBN_EDITCHANGE || notification == CBN_SELCHANGE
                       : notification == EN_CHANGE;
    if (changed)
        draft_changed(state);
}

static LRESULT CALLBACK control_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR subclass_id, DWORD_PTR reference) {
    SettingsState *state = (SettingsState *)reference;
    if (message == WM_SETFOCUS)
        ensure_visible(state, window);
    if (message == WM_NCDESTROY) {
        if (state->accessibility) {
            const MSAAPROPID properties[] = {PROPID_ACC_NAME, PROPID_ACC_DESCRIPTION};
            IAccPropServices_ClearHwndProps(state->accessibility, window, OBJID_CLIENT,
                                            CHILDID_SELF, properties, (int)COUNT_OF(properties));
        }
        RemoveWindowSubclass(window, control_proc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

static LRESULT CALLBACK page_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    SettingsState *state = window_state(window);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        state = (SettingsState *)create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
    }
    if (!state)
        return DefWindowProcW(window, message, wparam, lparam);
    int page = GetDlgCtrlID(window) - ID_SETTINGS_PAGE_IDLE;
    switch (message) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        return SendMessageW(state->window, message, wparam, lparam);
    case WM_SIZE:
        if (page >= 0 && page < SETTINGS_PAGE_COUNT)
            layout_page(state, page);
        return 0;
    case WM_VSCROLL: {
        if (page < 0 || page >= SETTINGS_PAGE_COUNT)
            return 0;
        SCROLLINFO info = {.cbSize = sizeof(info), .fMask = SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int position = info.nPos;
        int line = pixels(state, 32);
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            position -= line;
            break;
        case SB_LINEDOWN:
            position += line;
            break;
        case SB_PAGEUP:
            position -= maximum(line, (int)info.nPage - line);
            break;
        case SB_PAGEDOWN:
            position += maximum(line, (int)info.nPage - line);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            position = info.nTrackPos;
            break;
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = info.nMax;
            break;
        default:
            return 0;
        }
        scroll_page(state, page, position);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (page < 0 || page >= SETTINGS_PAGE_COUNT)
            return 0;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        state->wheel[page] += GET_WHEEL_DELTA_WPARAM(wparam);
        int steps = state->wheel[page] / WHEEL_DELTA;
        state->wheel[page] %= WHEEL_DELTA;
        if (!steps || !lines)
            return 0;
        RECT client;
        GetClientRect(window, &client);
        int distance = lines == WHEEL_PAGESCROLL
                           ? client.bottom
                           : pixels(state, 20) * (int)minimum((int)lines, 100);
        scroll_page(state, page, state->scroll[page] - steps * distance);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static void window_limits(SettingsState *state, MINMAXINFO *limits) {
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(MonitorFromWindow(state->window, MONITOR_DEFAULTTONEAREST), &info))
        return;
    int width = info.rcWork.right - info.rcWork.left;
    int height = info.rcWork.bottom - info.rcWork.top;
    limits->ptMinTrackSize.x = minimum(pixels(state, 420), width);
    limits->ptMinTrackSize.y = minimum(pixels(state, 390), height);
}

static void free_state(SettingsState *state) {
    if (state->accessibility)
        IAccPropServices_Release(state->accessibility);
    if (state->font)
        DeleteObject(state->font);
    if (state->title_font)
        DeleteObject(state->title_font);
    if (state->section_font)
        DeleteObject(state->section_font);
    if (state->invalid_brush)
        DeleteObject(state->invalid_brush);
    if (!state->creating)
        free(state);
}

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    SettingsState *state = window_state(window);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        state = (SettingsState *)create->lpCreateParams;
        state->window = window;
        state->app->settings_window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
    }
    if (!state)
        return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        state->dpi = GetDpiForWindow(window);
        if (!state->dpi)
            state->dpi = 96;
        update_contrast(state);
        state->invalid_brush = CreateSolidBrush(RGB(255, 239, 239));
        if (!state->invalid_brush || !update_fonts(state))
            return -1;
        CoCreateInstance(&CLSID_AccPropServices, NULL, CLSCTX_INPROC_SERVER, &IID_IAccPropServices,
                         (void **)&state->accessibility);
        if (!create_form(state))
            return -1;
        state->ready = true;
        fill_form(state, &state->baseline);
        AppConfig config;
        validate_form(state, &config);
        update_preview_button(state);
        layout_window(state);
        status_text(state,
                    L"Live preview is on. Apply saves changes; Cancel restores the baseline.",
                    false);
        return 0;
    }
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
            layout_window(state);
        return 0;
    case WM_GETMINMAXINFO:
        window_limits(state, (MINMAXINFO *)lparam);
        return 0;
    case WM_DPICHANGED: {
        UINT previous = state->dpi;
        state->dpi = HIWORD(wparam);
        if (!state->dpi)
            state->dpi = 96;
        for (int i = 0; i < SETTINGS_PAGE_COUNT; ++i)
            state->scroll[i] = MulDiv(state->scroll[i], (int)state->dpi, (int)previous);
        update_fonts(state);
        const RECT *rectangle = (const RECT *)lparam;
        SetWindowPos(window, NULL, rectangle->left, rectangle->top,
                     rectangle->right - rectangle->left, rectangle->bottom - rectangle->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        layout_window(state);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
        update_contrast(state);
        InvalidateRect(window, NULL, TRUE);
        layout_window(state);
        return 0;
    case WM_DISPLAYCHANGE: {
        wchar_t device[32];
        HWND monitor = state->controls[field_index(ID_SETTINGS_MONITOR)].control;
        if (!read_monitor(monitor, device, COUNT_OF(device)))
            swprintf(device, COUNT_OF(device), L"%ls", state->draft.monitor);
        refresh_monitors(state, device);
        layout_window(state);
        return 0;
    }
    case WM_COMMAND:
        handle_command(state, wparam, lparam);
        return 0;
    case WM_NOTIFY: {
        const NMHDR *header = (const NMHDR *)lparam;
        if (header->hwndFrom == state->tabs && header->code == TCN_SELCHANGE)
            select_page(state, TabCtrl_GetCurSel(state->tabs), false);
        return 0;
    }
    case WM_DRAWITEM:
        if (draw_swatch(state, (const DRAWITEMSTRUCT *)lparam))
            return TRUE;
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        return control_colors(state, message, wparam, lparam);
    case WM_MOUSEWHEEL:
        return SendMessageW(state->pages[state->selected_page], message, wparam, lparam);
    case DM_GETDEFID:
        return MAKELONG(state->default_id, DC_HASDEFID);
    case DM_SETDEFID: {
        HWND previous = GetDlgItem(window, state->default_id);
        HWND next = GetDlgItem(window, (int)wparam);
        if (!next)
            return 0;
        if (previous)
            SendMessageW(previous, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
        state->default_id = (int)wparam;
        SendMessageW(next, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        return 0;
    }
    case WM_CLOSE:
        close_settings(state);
        return 0;
    case WM_NCDESTROY:
        state->app->settings_window = NULL;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        free_state(state);
        return DefWindowProcW(window, message, wparam, lparam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static bool register_windows(HINSTANCE instance) {
    WNDCLASSEXW settings = {.cbSize = sizeof(settings),
                            .style = CS_HREDRAW | CS_VREDRAW,
                            .lpfnWndProc = settings_proc,
                            .hInstance = instance,
                            .hCursor = LoadCursorW(NULL, IDC_ARROW),
                            .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
                            .lpszClassName = DYNAMICWINDOWS_SETTINGS_CLASS};
    if (!RegisterClassExW(&settings) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    WNDCLASSEXW page = {.cbSize = sizeof(page),
                        .lpfnWndProc = page_proc,
                        .hInstance = instance,
                        .hCursor = LoadCursorW(NULL, IDC_ARROW),
                        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
                        .lpszClassName = SETTINGS_PAGE_CLASS};
    return RegisterClassExW(&page) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void settings_show(App *app) {
    if (!app)
        return;
    if (IsWindow(app->settings_window)) {
        ShowWindow(app->settings_window, IsIconic(app->settings_window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(app->settings_window);
        return;
    }
    INITCOMMONCONTROLSEX controls = {.dwSize = sizeof(controls),
                                     .dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls) || !register_windows(app->instance)) {
        MessageBoxW(app->window, L"The native settings window could not be created.",
                    L"DynamicWindows", MB_OK | MB_ICONERROR);
        return;
    }
    SettingsState *state = calloc(1, sizeof(*state));
    if (!state)
        return;
    state->app = app;
    state->baseline = app->config;
    state->draft = app->config;
    state->original_view = app->island.view;
    state->original_expanded = app->island.expanded;
    state->original_hidden = app->hidden;
    state->original_fullscreen_hidden = app->fullscreen_hidden;
    state->original_hover_open = app->hover_open;
    state->original_hover_blocked = app->hover_blocked;
    state->live = true;
    state->creating = true;
    state->default_id = ID_SETTINGS_APPLY;
    state->dpi = GetDpiForWindow(app->window);
    if (!state->dpi)
        state->dpi = 96;
    MONITORINFO info = {.cbSize = sizeof(info)};
    HMONITOR monitor = MonitorFromWindow(app->window, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &info)) {
        free(state);
        return;
    }
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD extended = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
    RECT frame = {0, 0, pixels(state, 700), pixels(state, 580)};
    AdjustWindowRectExForDpi(&frame, style, FALSE, extended, state->dpi);
    int work_width = info.rcWork.right - info.rcWork.left;
    int work_height = info.rcWork.bottom - info.rcWork.top;
    int width = minimum(frame.right - frame.left, maximum(1, work_width - pixels(state, 16)));
    int height = minimum(frame.bottom - frame.top, maximum(1, work_height - pixels(state, 16)));
    int x = info.rcWork.left + (work_width - width) / 2;
    int y = info.rcWork.top + (work_height - height) / 2;
    HWND window =
        CreateWindowExW(extended, DYNAMICWINDOWS_SETTINGS_CLASS, L"DynamicWindows settings", style,
                        x, y, width, height, NULL, NULL, app->instance, state);
    if (!window) {
        free(state);
        MessageBoxW(app->window, L"The native settings controls could not be created.",
                    L"DynamicWindows", MB_OK | MB_ICONERROR);
        return;
    }
    state->creating = false;
    if (app->icon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)app->icon);
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)app->icon);
    }
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(state->controls[field_index(ID_SETTINGS_IDLE)].control);
}

void settings_destroy(App *app) {
    if (!app || !IsWindow(app->settings_window))
        return;
    SettingsState *state = window_state(app->settings_window);
    if (state)
        revert_preview(state);
    DestroyWindow(app->settings_window);
    app->settings_window = NULL;
}

bool settings_message(App *app, MSG *message) {
    if (!app || !message || !IsWindow(app->settings_window))
        return false;
    HWND window = app->settings_window;
    if (message->hwnd != window && !IsChild(window, message->hwnd))
        return false;
    SettingsState *state = window_state(window);
    if (!state)
        return false;
    if (message->message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
        WPARAM key = message->wParam;
        if (key == VK_TAB || key == VK_PRIOR || key == VK_NEXT) {
            int direction =
                key == VK_PRIOR || (key == VK_TAB && GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
            int page =
                (state->selected_page + direction + SETTINGS_PAGE_COUNT) % SETTINGS_PAGE_COUNT;
            select_page(state, page, true);
            return true;
        }
        if (key == 'S') {
            apply_draft(state);
            return true;
        }
    }
    return IsDialogMessageW(window, message) != FALSE;
}

bool settings_is_preview(const App *app) {
    if (!app || !IsWindow(app->settings_window))
        return false;
    const SettingsState *state = window_state(app->settings_window);
    return state && state->live;
}
