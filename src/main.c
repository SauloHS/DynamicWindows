#include "app.h"
#include "settings.h"

#include <math.h>
#include <shellscalingapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windowsx.h>

#define FRAME_TIMER 1
#define HOTKEY_ISLAND 1
#define HOTKEY_TIMER 2
#define HOTKEY_HIDE 3
#define MENU_TOGGLE 100
#define MENU_MEDIA 101
#define MENU_TIMER 102
#define MENU_SYSTEM 103
#define MENU_SEARCH 104
#define MENU_HOVER 105
#define MENU_MOTION 106
#define MENU_FULLSCREEN 107
#define MENU_HIDE 108
#define MENU_EXIT 109
#define MENU_SETTINGS 110
#define MENU_ABOUT 111
#define MENU_CRAB 112
#define MENU_MONITOR_BASE 200

static const wchar_t window_class[] = L"DynamicWindows.ActivityIsland";

static void toggle_visibility(App *app);
static void shortcut_action(App *app, ShortcutAction shortcut);
static App *hook_app;

typedef struct {
    bool smoke;
    bool demo;
    bool reduced;
    bool help;
    bool settings;
    bool isolated;
    wchar_t config_path[1024];
    IslandView view;
    bool expanded;
    unsigned seconds;
    wchar_t smoke_dir[MAX_PATH];
} Options;

typedef struct {
    App *app;
    HMENU menu;
    unsigned count;
    HMONITOR selected;
    unsigned target;
} MonitorMenu;

void app_crab_error_text(HRESULT error, wchar_t *text, size_t count) {
    DWORD code = HRESULT_CODE(error);
    const wchar_t *message;
    if (error == REGDB_E_CLASSNOTREG)
        message = L"Windows Speech Recognition is not installed for this language";
    else if (error == E_ACCESSDENIED)
        message = L"Microphone or provider access was denied";
    else if (code == ERROR_NOT_FOUND)
        message = L"API key missing for this provider endpoint";
    else if (code == ERROR_TIMEOUT)
        message = L"Listening or provider request timed out";
    else if (code == ERROR_BUSY)
        message = L"Crab is already handling a request";
    else if (code == ERROR_INVALID_PARAMETER)
        message = L"Provider, base URL, or model is invalid";
    else if (code == ERROR_INVALID_DATA)
        message = L"Speech or provider returned unreadable data";
    else if (code >= 400 && code <= 599)
        message = L"Provider rejected the request";
    else
        message = L"Windows Speech Recognition could not start";
    swprintf(text, count, L"%ls (0x%08lX)", message, (unsigned long)error);
}

void app_clock(uint64_t milliseconds, wchar_t *text, size_t count) {
    uint64_t seconds = milliseconds / 1000 + (milliseconds % 1000 != 0);
    unsigned long long hours = seconds / 3600;
    unsigned minutes = (unsigned)((seconds / 60) % 60);
    unsigned remainder = (unsigned)(seconds % 60);
    if (hours)
        swprintf(text, count, L"%llu:%02u:%02u", hours, minutes, remainder);
    else
        swprintf(text, count, L"%02u:%02u", minutes, remainder);
}

void app_summary(const App *app, wchar_t *text, size_t count) {
    if (app->crab_state == CRAB_ERROR) {
        wchar_t error[160];
        app_crab_error_text(app->crab_error, error, 160);
        swprintf(text, count, L"DynamicWindows. %ls error. %ls.", app->config.crab_name, error);
        return;
    }
    if (app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
        app->crab_state == CRAB_SPEAKING) {
        swprintf(text, count, L"DynamicWindows. %ls. %ls.", app->config.crab_name,
                 crab_state_text(app->crab_state));
        return;
    }
    if (app->island.alert == ALERT_TIMER) {
        swprintf(text, count, L"DynamicWindows. Timer finished. Click to dismiss.");
        return;
    }
    if (app->island.alert == ALERT_HEADPHONES && !app->island.expanded) {
        wchar_t detail[160];
        headphones_detail(&app->headphone_event.device, app->config.headphone_battery, detail, 160);
        swprintf(text, count, L"DynamicWindows. %ls. %ls. %ls.", app->headphone_event.device.name,
                 app->headphone_event.change == HEADPHONE_DISCONNECTED ? L"Disconnected"
                 : app->headphone_event.change == HEADPHONE_BATTERY    ? L"Battery updated"
                                                                       : L"Connected",
                 detail);
        return;
    }
    if (app->island.alert == ALERT_NOTIFICATION && !app->island.expanded) {
        const AppNotification *notification = &app->notification_alert;
        if (app->config.notification_content)
            swprintf(text, count, L"DynamicWindows. Notification from %ls. %ls. %ls.",
                     notification->source, notification->title, notification->body);
        else
            swprintf(text, count, L"DynamicWindows. New notification from %ls.",
                     notification->source);
        return;
    }
    if (app->island.expanded) {
        if (app->island.view == ISLAND_TIMER) {
            wchar_t clock[32];
            app_clock(app->island.timer.remaining, clock, 32);
            swprintf(text, count, L"DynamicWindows. Timer. %ls remaining. %ls. Expanded.", clock,
                     app->island.timer.finished  ? L"Finished"
                     : app->island.timer.paused  ? L"Paused"
                     : app->island.timer.running ? L"Running"
                                                 : L"Ready");
        } else if (app->island.view == ISLAND_NOTIFICATIONS) {
            swprintf(text, count, L"DynamicWindows. Notifications. %u captured. Expanded.",
                     app->notification_count);
        } else if (app->island.view == ISLAND_SYSTEM) {
            swprintf(text, count,
                     L"DynamicWindows. System controls. %ls. Volume %d%%. %ls. Expanded.",
                     app->system.audio_available ? app->system.output_name : L"No audio output",
                     (int)lround(app->system.volume * 100),
                     app->system.plugged_in ? L"External power" : L"Battery power");
        } else if (app->media.available) {
            swprintf(text, count, L"DynamicWindows. Media controls. %ls. %ls. %ls. Expanded.",
                     app->media.title, app->media.artist,
                     app->media.playing ? L"Playing" : L"Paused");
        } else {
            swprintf(text, count, L"DynamicWindows. Media controls. %ls. Expanded.",
                     FAILED(app->media.error) ? L"Media service unavailable"
                     : app->media.initialized ? L"Nothing playing"
                                              : L"Connecting to media");
        }
        return;
    }
    IslandActivity primary = app_primary(app);
    if (primary == ACTIVITY_MEDIA) {
        swprintf(text, count, L"DynamicWindows. %ls. %ls. %ls. Click to expand.", app->media.title,
                 app->media.artist, app->media.playing ? L"Playing" : L"Paused");
        return;
    }
    if (primary == ACTIVITY_TIMER) {
        wchar_t clock[32];
        app_clock(app->island.timer.remaining, clock, 32);
        swprintf(text, count, L"DynamicWindows. Timer %ls remaining. %ls. Click to expand.", clock,
                 app->island.timer.paused ? L"Paused" : L"Running");
        return;
    }
    const HeadphoneDevice *headphones = app_headphones(app);
    if (headphones) {
        wchar_t detail[160];
        headphones_detail(headphones, app->config.headphone_battery, detail, 160);
        swprintf(text, count, L"DynamicWindows. %ls. %ls. Click for system controls.",
                 headphones->name, detail);
        return;
    }
    wchar_t idle[256];
    app_idle_label(app, idle, 256);
    swprintf(text, count, L"DynamicWindows. %ls. Click to expand. Right-click for settings.", idle);
}

static void motion_setting(App *app) {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    app->reduced_motion = app->config.reduced_motion || !animations;
}

typedef struct {
    const wchar_t *device;
    HMONITOR monitor;
} MonitorFind;

static BOOL CALLBACK find_monitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc;
    (void)rect;
    MonitorFind *find = (MonitorFind *)data;
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) &&
        wcscmp(info.szDevice, find->device) == 0) {
        find->monitor = monitor;
        return FALSE;
    }
    return TRUE;
}

static HMONITOR configured_monitor(const App *app) {
    MonitorFind find = {.device = app->config.monitor};
    if (*find.device)
        EnumDisplayMonitors(NULL, NULL, find_monitor, (LPARAM)&find);
    return find.monitor ? find.monitor : MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

static void position_window(App *app, HMONITOR monitor) {
    MONITORINFO info = {.cbSize = sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info))
        return;
    app->monitor = monitor;
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    (void)GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
    config_canvas(&app->config, &app->canvas_width, &app->canvas_height);
    double scale = (double)dpi_x / 96;
    double room_x = (double)(info.rcWork.right - info.rcWork.left - 16) / (app->canvas_width - 32);
    double room_y = (double)(info.rcWork.bottom - info.rcWork.top) / app->config.expanded_height;
    scale = island_clamp(fmin(scale, fmin(room_x, room_y)), 0.25, 4);
    bool resized = fabs(scale - app->scale) > 0.001 ||
                   fabs(app->canvas_width * scale - app->renderer.width) > 1 ||
                   fabs(app->canvas_height * scale - app->renderer.height) > 1;
    app->scale = scale;
    ConfigRect work = {info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                       info.rcWork.bottom - info.rcWork.top};
    ConfigRect rect =
        config_place(&app->config, work, scale, app->canvas_width, app->canvas_height);
    app->base_position = (POINT){(LONG)lround(rect.x), (LONG)lround(rect.y)};
    SetWindowPos(app->window, HWND_TOPMOST, app->base_position.x, app->base_position.y,
                 (int)ceil(rect.width), (int)ceil(rect.height), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (resized && app->renderer.token && !renderer_resize(app)) {
        MessageBoxW(app->window, L"The drawing surface could not be resized.", L"DynamicWindows",
                    MB_ICONERROR);
        PostMessageW(app->window, WM_CLOSE, 0, 0);
    }
}

static Island visible_island(const App *app) {
    Island island = app->island;
    island.media_available = island.media_available && app->config.show_media;
    if (!app->config.show_timer)
        island.timer.running = false;
    return island;
}

IslandActivity app_primary(const App *app) {
    Island island = visible_island(app);
    return island_primary(&island);
}

IslandActivity app_secondary(const App *app) {
    if (!app->config.dual_activity)
        return ACTIVITY_NONE;
    Island island = visible_island(app);
    return island_secondary(&island);
}

const HeadphoneDevice *app_headphones(const App *app) {
    return app->config.headphone_status
               ? headphones_select(&app->headphones, app->platform.device_id)
               : NULL;
}

IslandSize app_target_size(const App *app) {
    const AppConfig *config = &app->config;
    bool crab_orb = app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
                    app->crab_state == CRAB_SPEAKING || GetTickCount64() < app->crab_orb_until;
    if (crab_orb)
        return (IslandSize){72, 72, 36, 0, 0};
    if (app->crab_state == CRAB_ERROR)
        return (IslandSize){config->alert_width, config->alert_height, config->alert_height / 2, 0,
                            0};
    if (app->island.expanded)
        return (IslandSize){config->expanded_width, config->expanded_height,
                            config->expanded_radius, 0, 0};
    if (app->island.alert == ALERT_HEADPHONES)
        return (IslandSize){config->alert_width, config->alert_height, config->alert_height / 2, 0,
                            0};
    if (app->island.alert != ALERT_NONE)
        return (IslandSize){config->alert_width, config->alert_height,
                            fmin(config->alert_height / 2, config->compact_radius), 0, 0};
    IslandActivity primary = app_primary(app);
    double width = primary == ACTIVITY_MEDIA   ? config->idle_width + config->media_extra_width
                   : primary == ACTIVITY_TIMER ? config->timer_width
                                               : config->idle_width;
    double bubble =
        config->dual_activity && app_secondary(app) != ACTIVITY_NONE ? config->bubble_size : 0;
    const HeadphoneDevice *headphones = app_headphones(app);
    if (headphones && primary == ACTIVITY_NONE)
        width += config->media_extra_width;
    double height = primary == ACTIVITY_TIMER ? config->timer_height : config->compact_height;
    double headphone_bubble = headphones && primary != ACTIVITY_NONE ? height : 0;
    return (IslandSize){width, height, fmin(config->compact_radius, height / 2), bubble,
                        headphone_bubble};
}

static void frame_interval(App *app, unsigned milliseconds) {
    if (!app->frame_timer || app->timer_interval == milliseconds)
        return;
    LARGE_INTEGER due = {.QuadPart = -10000};
    if (SetWaitableTimer(app->frame_timer, &due, (LONG)milliseconds, NULL, NULL, FALSE))
        app->timer_interval = milliseconds;
}

void app_animate(App *app) {
    unsigned interval = (unsigned)lround(1000.0 / app->config.frame_rate);
    frame_interval(app, interval);
    QueryPerformanceCounter(&app->last_clock);
    app->last_frame = 0;
}

void app_event(App *app, bool media_change) {
    uint64_t until = GetTickCount64() + (uint64_t)app->config.change_preview_ms;
    app->event_until = until;
    if (media_change)
        app->media_peek_until = until;
    app_animate(app);
}

static LRESULT CALLBACK outside_click(int code, WPARAM wparam, LPARAM lparam) {
    App *app = hook_app;
    if (code == HC_ACTION && app && app->island.expanded && !app->hover_open && !app->dragging &&
        (wparam == WM_LBUTTONDOWN || wparam == WM_RBUTTONDOWN || wparam == WM_MBUTTONDOWN)) {
        const MSLLHOOKSTRUCT *mouse = (const MSLLHOOKSTRUCT *)lparam;
        POINT point = mouse->pt;
        ScreenToClient(app->window, &point);
        if (!app_inside(app, point.x / app->scale, point.y / app->scale))
            PostMessageW(app->window, WM_DYNAMICWINDOWS_OUTSIDE, 0, 0);
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static void outside_tracking(App *app, bool enabled) {
    if (enabled && !app->outside_hook) {
        hook_app = app;
        app->outside_hook = SetWindowsHookExW(WH_MOUSE_LL, outside_click, app->instance, 0);
    } else if (!enabled && app->outside_hook) {
        UnhookWindowsHookEx(app->outside_hook);
        app->outside_hook = NULL;
        hook_app = NULL;
    }
}

bool app_register_shortcuts(App *app) {
    if (app->test_mode)
        return true;
    bool registered = true;
    for (int i = 0; i < SHORTCUT_GLOBAL_COUNT; ++i) {
        if (app->hotkeys[i])
            UnregisterHotKey(app->window, i + 1);
        app->hotkeys[i] = false;
    }
    if (app->crab_hotkey)
        UnregisterHotKey(app->window, SHORTCUT_GLOBAL_COUNT + 1);
    app->crab_hotkey = false;
    for (int i = 0; i < SHORTCUT_GLOBAL_COUNT; ++i) {
        ShortcutBinding binding = app->config.shortcuts[i];
        if (!binding.key)
            continue;
        app->hotkeys[i] = RegisterHotKey(app->window, i + 1, binding.modifiers | MOD_NOREPEAT,
                                         binding.key) != FALSE;
        registered = app->hotkeys[i] && registered;
    }
    if (app->config.crab_shortcut.key) {
        app->crab_hotkey = RegisterHotKey(app->window, SHORTCUT_GLOBAL_COUNT + 1,
                                          app->config.crab_shortcut.modifiers | MOD_NOREPEAT,
                                          app->config.crab_shortcut.key) != FALSE;
        registered = app->crab_hotkey && registered;
    }
    return registered;
}

double app_media_position(const App *app) {
    double position = app->media_target_position;
    uint64_t now = GetTickCount64();
    if (app->media.playing && app->media_position_at && now >= app->media_position_at)
        position += (now - app->media_position_at) / 1000.0 * app->media.rate;
    return island_clamp(position, 0, fmax(0, app->media.duration));
}

void app_media_name(const App *app, wchar_t *text, size_t count) {
    const wchar_t *source = app->media.source;
    if (StrStrIW(source, L"Spotify"))
        swprintf(text, count, L"Spotify");
    else if (StrStrIW(source, L"chrome"))
        swprintf(text, count, L"Chrome");
    else if (StrStrIW(source, L"msedge"))
        swprintf(text, count, L"Microsoft Edge");
    else if (StrStrIW(source, L"firefox"))
        swprintf(text, count, L"Firefox");
    else if (StrStrIW(source, L"brave"))
        swprintf(text, count, L"Brave");
    else if (StrStrIW(source, L"vlc"))
        swprintf(text, count, L"VLC");
    else {
        const wchar_t *name = wcsrchr(source, L'!');
        if (name)
            source = name + 1;
        else {
            name = wcsrchr(source, L'\\');
            if (name)
                source = name + 1;
        }
        swprintf(text, count, L"%ls", source);
    }
}

bool app_apply_config(App *app, const AppConfig *input, bool persist) {
    AppConfig config = *input;
    AppConfig previous = app->config;
    config_normalize(&config);
    if (!config_shortcuts_valid(&config))
        return false;
    bool bindings_changed =
        memcmp(previous.shortcuts, config.shortcuts, sizeof(config.shortcuts)) != 0 ||
        memcmp(&previous.crab_shortcut, &config.crab_shortcut, sizeof(config.crab_shortcut)) != 0;
    app->config = config;
    if (bindings_changed && app->initialized && !app_register_shortcuts(app)) {
        app->config = previous;
        app_register_shortcuts(app);
        return false;
    }
    if ((app->renderer.token && !renderer_style(app)) ||
        (persist && !config_save(&config, app->config_path))) {
        app->config = previous;
        if (app->renderer.token)
            renderer_style(app);
        if (bindings_changed)
            app_register_shortcuts(app);
        return false;
    }
    app->hover_enabled = config.hover_expand;
    app->quiet_fullscreen = config.hide_fullscreen;
    crab_configure(&app->crab_service, config.crab_enabled, config.crab_speak, config.crab_provider,
                   config.crab_name, config.crab_base_url, config.crab_model);
    notifications_configure(&app->notification_service, app->window, config.notifications_enabled,
                            config.notification_content);
    app->motion_allowed = !config.reduced_motion;
    motion_setting(app);
    app->island.media_available = app->media.available && config.show_media;
    app->island.media_playing = app->media.playing;
    if (!app->island.timer.running && !app->island.timer.finished) {
        app->island.timer.duration = (uint64_t)config.timer_seconds * 1000;
        app->island.timer.remaining = app->island.timer.duration;
    }
    position_window(app, configured_monitor(app));
    app_animate(app);
    app_render(app);
    return true;
}

void app_idle_label(const App *app, wchar_t *text, size_t count) {
    const AppConfig *config = &app->config;
    SYSTEMTIME now;
    GetLocalTime(&now);
    if (config->idle == IDLE_BLANK || config->idle == IDLE_HIDDEN) {
        *text = L'\0';
        return;
    }
    if (config->idle == IDLE_TEXT) {
        swprintf(text, count, L"%ls", config->idle_text);
        return;
    }
    if (config->idle == IDLE_BATTERY) {
        if (app->system.has_battery && app->system.battery_percent >= 0)
            swprintf(text, count, L"%d%% battery", app->system.battery_percent);
        else
            swprintf(text, count, L"%ls",
                     app->system.plugged_in ? L"External power" : L"No battery");
        return;
    }
    if (config->idle == IDLE_VOLUME) {
        if (!app->system.audio_available)
            swprintf(text, count, L"No audio output");
        else if (app->system.muted)
            swprintf(text, count, L"Muted");
        else
            swprintf(text, count, L"Volume %d%%", (int)lround(app->system.volume * 100));
        return;
    }
    wchar_t date[80] = {0};
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &now, L"ddd, MMM d", date, 80);
    if (config->idle == IDLE_DATE) {
        swprintf(text, count, L"%ls", date);
        return;
    }
    wchar_t clock[64];
    unsigned hours = config->clock_24h ? now.wHour : (now.wHour % 12 ? now.wHour % 12 : 12);
    if (config->clock_seconds)
        swprintf(clock, 64, L"%02u:%02u:%02u%ls", hours, now.wMinute, now.wSecond,
                 config->clock_24h ? L""
                 : now.wHour < 12  ? L" AM"
                                   : L" PM");
    else
        swprintf(clock, 64, L"%02u:%02u%ls", hours, now.wMinute,
                 config->clock_24h ? L""
                 : now.wHour < 12  ? L" AM"
                                   : L" PM");
    swprintf(text, count, config->idle == IDLE_CLOCK_DATE ? L"%ls  |  %ls" : L"%ls", clock, date);
}

static void tray_tooltip(App *app) {
    if (!app->tray_added)
        return;
    wchar_t summary[600];
    app_summary(app, summary, 600);
    wcsncpy(app->tray.szTip, summary, 127);
    app->tray.szTip[127] = L'\0';
    app->tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &app->tray);
}

static void tray_add(App *app) {
    if (app->test_mode)
        return;
    app->tray = (NOTIFYICONDATAW){.cbSize = sizeof(NOTIFYICONDATAW),
                                  .hWnd = app->window,
                                  .uID = 1,
                                  .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
                                  .uCallbackMessage = WM_DYNAMICWINDOWS_TRAY,
                                  .hIcon = app->icon};
    swprintf(app->tray.szTip, 128, L"DynamicWindows - right-click to customize");
    app->tray_added = Shell_NotifyIconW(NIM_ADD, &app->tray) != FALSE;
    if (app->tray_added) {
        app->tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &app->tray);
    }
}

void app_render(App *app) {
    if (!app->renderer.graphics)
        return;
    if (!renderer_draw(app)) {
        PostMessageW(app->window, WM_CLOSE, 0, 0);
        return;
    }
    wchar_t summary[600];
    app_summary(app, summary, 600);
    SetWindowTextW(app->window, summary);
}

void app_open(App *app, IslandView view, bool activate) {
    if (activate) {
        app->hidden = false;
        app->event_until = 0;
    } else if (settings_is_preview(app)) {
        app->event_until = GetTickCount64() + (uint64_t)app->config.change_preview_ms;
    }
    app->fullscreen_hidden = false;
    if (activate && GetForegroundWindow() != app->window)
        app->previous_foreground = GetForegroundWindow();
    island_open(&app->island, view);
    app->hover_open = !activate;
    app->focus = -1;
    ShowWindow(app->window, SW_SHOWNOACTIVATE);
    SetWindowPos(app->window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (activate) {
        SetForegroundWindow(app->window);
        SetFocus(app->window);
        outside_tracking(app, true);
    }
    app_animate(app);
    app_render(app);
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, app->window, OBJID_CLIENT, CHILDID_SELF);
    tray_tooltip(app);
}

void app_close(App *app) {
    if (app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
        app->crab_state == CRAB_SPEAKING) {
        crab_cancel(&app->crab_service);
        app->crab_state = CRAB_IDLE;
        app->event_until = GetTickCount64() + 1200;
        app_animate(app);
        return;
    }
    outside_tracking(app, false);
    island_close(&app->island);
    app->hover_open = false;
    app->hover_blocked = true;
    app->focus = -1;
    app->pressed = ACTION_NONE;
    app->dragging = false;
    if (GetCapture() == app->window)
        ReleaseCapture();
    if (GetForegroundWindow() == app->window && IsWindow(app->previous_foreground))
        SetForegroundWindow(app->previous_foreground);
    app_animate(app);
    app_render(app);
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, app->window, OBJID_CLIENT, CHILDID_SELF);
    tray_tooltip(app);
}

static void timer_duration(App *app, uint64_t duration) {
    if (app->island.timer.running || app->island.timer.finished)
        return;
    app->island.timer.duration = duration;
    app->island.timer.remaining = duration;
}

void app_action(App *app, Action action, double value) {
    switch (action) {
    case ACTION_EXPAND:
        if (app->island.expanded) {
            app_close(app);
            return;
        }
        IslandView target = app->island.alert == ALERT_NOTIFICATION ? ISLAND_NOTIFICATIONS
                            : app->island.alert == ALERT_HEADPHONES ||
                                    (app_primary(app) == ACTIVITY_NONE && app_headphones(app))
                                ? ISLAND_SYSTEM
                            : app_primary(app) == ACTIVITY_TIMER ? ISLAND_TIMER
                                                                 : ISLAND_MEDIA;
        if (app->island.alert == ALERT_NOTIFICATION) {
            app->island.alert = ALERT_NONE;
            app->island.alert_deadline = 0;
        }
        app_open(app, target, true);
        return;
    case ACTION_SECONDARY:
        app_open(app, app_secondary(app) == ACTIVITY_TIMER ? ISLAND_TIMER : ISLAND_MEDIA, true);
        return;
    case ACTION_HEADPHONES:
        app_open(app, ISLAND_SYSTEM, true);
        return;
    case ACTION_CLOSE:
        app_close(app);
        return;
    case ACTION_TAB_MEDIA:
    case ACTION_TAB_TIMER:
    case ACTION_TAB_SYSTEM:
    case ACTION_TAB_NOTIFICATIONS:
        if (!app->island.expanded) {
            app_open(app,
                     action == ACTION_TAB_NOTIFICATIONS ? ISLAND_NOTIFICATIONS
                                                        : (IslandView)(action - ACTION_TAB_MEDIA),
                     true);
            return;
        }
        app->hover_open = false;
        island_open(&app->island, action == ACTION_TAB_NOTIFICATIONS
                                      ? ISLAND_NOTIFICATIONS
                                      : (IslandView)(action - ACTION_TAB_MEDIA));
        app->focus = -1;
        NotifyWinEvent(EVENT_OBJECT_REORDER, app->window, OBJID_CLIENT, CHILDID_SELF);
        break;
    case ACTION_PLAY:
    case ACTION_PREVIOUS:
    case ACTION_NEXT:
        if (app->demo) {
            if (action == ACTION_PLAY)
                app->media.playing = !app->media.playing;
            break;
        }
        media_command(&app->media_service,
                      action == ACTION_PLAY       ? MEDIA_TOGGLE
                      : action == ACTION_PREVIOUS ? MEDIA_PREVIOUS
                                                  : MEDIA_NEXT,
                      0);
        break;
    case ACTION_SEEK:
        if (app->media.can_seek)
            media_command(&app->media_service, MEDIA_SEEK,
                          island_clamp(value, 0, 1) * app->media.duration);
        break;
    case ACTION_VOLUME:
        if (!platform_set_volume(&app->platform, value))
            wcscpy(app->notice, L"Windows could not change the volume.");
        break;
    case ACTION_MUTE:
        if (!platform_toggle_mute(&app->platform))
            wcscpy(app->notice, L"Windows could not change mute state.");
        break;
    case ACTION_TIMER_TOGGLE:
        island_timer_toggle(&app->island, GetTickCount64());
        app_event(app, false);
        break;
    case ACTION_TIMER_RESET:
    case ACTION_TIMER_DISMISS:
        island_timer_reset(&app->island);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, app->window, OBJID_CLIENT, CHILDID_SELF);
        break;
    case ACTION_TIMER_1:
        timer_duration(app, 60000);
        break;
    case ACTION_TIMER_5:
        timer_duration(app, 300000);
        break;
    case ACTION_TIMER_15:
        timer_duration(app, 900000);
        break;
    case ACTION_TIMER_25:
        timer_duration(app, 1500000);
        break;
    case ACTION_SEARCH:
        app_close(app);
        if (!platform_open_search()) {
            wcscpy(app->notice, L"Windows Search could not be opened. Try Win+S.");
            MessageBoxW(app->settings_window ? app->settings_window : app->window, app->notice,
                        L"DynamicWindows", MB_ICONWARNING);
        }
        return;
    case ACTION_SETTINGS:
        settings_show(app);
        return;
    case ACTION_NOTIFICATION_PREVIOUS:
        if (app->notification_count && app->notification_index + 1 < app->notification_count)
            ++app->notification_index;
        break;
    case ACTION_NOTIFICATION_NEXT:
        if (app->notification_index > 0)
            --app->notification_index;
        break;
    case ACTION_CRAB:
        if (app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
            app->crab_state == CRAB_SPEAKING) {
            crab_cancel(&app->crab_service);
            app->crab_state = CRAB_IDLE;
            app->event_until = GetTickCount64() + 1200;
            app_animate(app);
            return;
        }
        if (!app->config.crab_enabled) {
            wcscpy(app->notice, L"Enable Crab in Settings first.");
            settings_show(app);
            return;
        }
        if (app->config.crab_provider == CRAB_PROVIDER_NONE || !*app->config.crab_base_url ||
            !*app->config.crab_model) {
            wcscpy(app->notice, L"Choose Crab's API format, base URL, and model in Settings.");
            settings_show(app);
            return;
        }
        CrabEndpoint crab_endpoint;
        if (!crab_endpoint_parse(app->config.crab_provider, app->config.crab_base_url,
                                 &crab_endpoint)) {
            wcscpy(app->notice, L"Crab's base URL is invalid or insecure.");
            settings_show(app);
            return;
        }
        if (!crab_key_exists(app->config.crab_provider, app->config.crab_base_url)) {
            wcscpy(app->notice, L"Connect an Anthropic API key in Crab settings.");
            settings_show(app);
            return;
        }
        app->event_until = UINT64_MAX;
        app->crab_orb_until = GetTickCount64() + 700;
        app->crab_error = S_OK;
        app->crab_transcript[0] = L'\0';
        app->crab_answer[0] = L'\0';
        if (!crab_listen(&app->crab_service)) {
            app->crab_state = CRAB_ERROR;
            app->crab_error = HRESULT_FROM_WIN32(ERROR_BUSY);
            app->event_until = GetTickCount64() + 3000;
            app_animate(app);
            return;
        }
        app->crab_state = CRAB_LISTENING;
        app_animate(app);
        return;
    case ACTION_TRACK_INFO:
        return;
    case ACTION_NONE:
        return;
    }
    app->island.media_playing = app->media.playing;
    app->hover_open = false;
    app_animate(app);
    app_render(app);
    tray_tooltip(app);
}

static BOOL CALLBACK monitor_menu(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc;
    (void)rect;
    MonitorMenu *state = (MonitorMenu *)data;
    unsigned index = state->count++;
    if (state->target == index)
        state->selected = monitor;
    if (!state->menu)
        return TRUE;
    MONITORINFOEXW info = {.cbSize = sizeof(info)};
    GetMonitorInfoW(monitor, (MONITORINFO *)&info);
    wchar_t label[128];
    swprintf(label, 128, L"Display %u%ls (%ld x %ld)", index + 1,
             info.dwFlags & MONITORINFOF_PRIMARY ? L" - primary" : L"",
             info.rcMonitor.right - info.rcMonitor.left,
             info.rcMonitor.bottom - info.rcMonitor.top);
    AppendMenuW(state->menu, MF_STRING | (monitor == state->app->monitor ? MF_CHECKED : 0),
                MENU_MONITOR_BASE + index, label);
    return TRUE;
}

static void tray_menu(App *app) {
    HMENU menu = CreatePopupMenu();
    HMENU displays = CreatePopupMenu();
    if (!menu || !displays) {
        if (menu)
            DestroyMenu(menu);
        if (displays)
            DestroyMenu(displays);
        return;
    }
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"Customize DynamicWindows...");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_TOGGLE, L"Open island");
    AppendMenuW(menu, MF_STRING, MENU_MEDIA, L"Now Playing");
    AppendMenuW(menu, MF_STRING, MENU_TIMER, L"Countdown timer");
    AppendMenuW(menu, MF_STRING, MENU_SYSTEM, L"System controls");
    AppendMenuW(menu, MF_STRING, MENU_SEARCH, L"Windows search");
    AppendMenuW(menu, MF_STRING, MENU_CRAB,
                app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
                        app->crab_state == CRAB_SPEAKING
                    ? L"Cancel Crab"
                    : L"Start Crab");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    MonitorMenu state = {.app = app, .menu = displays, .target = UINT_MAX};
    EnumDisplayMonitors(NULL, NULL, monitor_menu, (LPARAM)&state);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)displays, L"Display");
    AppendMenuW(menu, MF_STRING | (app->hover_enabled ? MF_CHECKED : 0), MENU_HOVER,
                L"Expand on hover");
    AppendMenuW(menu, MF_STRING | (app->reduced_motion ? MF_CHECKED : 0), MENU_MOTION,
                L"Reduced motion");
    AppendMenuW(menu, MF_STRING | (app->quiet_fullscreen ? MF_CHECKED : 0), MENU_FULLSCREEN,
                L"Hide over full-screen apps");
    AppendMenuW(menu, MF_STRING, MENU_HIDE, app->hidden ? L"Show island" : L"Hide island");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_ABOUT, L"About DynamicWindows / License");
    AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Quit DynamicWindows");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(app->window);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                  app->window, NULL);
    DestroyMenu(menu);
    PostMessageW(app->window, WM_NULL, 0, 0);
    if (command >= MENU_MONITOR_BASE && command < MENU_MONITOR_BASE + state.count) {
        MonitorMenu selection = {.app = app, .target = command - MENU_MONITOR_BASE};
        EnumDisplayMonitors(NULL, NULL, monitor_menu, (LPARAM)&selection);
        if (selection.selected) {
            MONITORINFOEXW info = {.cbSize = sizeof(info)};
            if (GetMonitorInfoW(selection.selected, (MONITORINFO *)&info))
                wcscpy(app->config.monitor, info.szDevice);
            app_apply_config(app, &app->config, true);
        }
        return;
    }
    switch (command) {
    case MENU_TOGGLE:
    case MENU_MEDIA:
        app_open(app, ISLAND_MEDIA, true);
        break;
    case MENU_TIMER:
        app_open(app, ISLAND_TIMER, true);
        break;
    case MENU_SYSTEM:
        app_open(app, ISLAND_SYSTEM, true);
        break;
    case MENU_SEARCH:
        app_action(app, ACTION_SEARCH, 0);
        break;
    case MENU_CRAB:
        app_action(app, ACTION_CRAB, 0);
        break;
    case MENU_SETTINGS:
        settings_show(app);
        break;
    case MENU_HOVER:
        app->config.hover_expand = !app->config.hover_expand;
        app_apply_config(app, &app->config, true);
        break;
    case MENU_MOTION:
        app->config.reduced_motion = !app->config.reduced_motion;
        app_apply_config(app, &app->config, true);
        break;
    case MENU_FULLSCREEN:
        app->config.hide_fullscreen = !app->config.hide_fullscreen;
        app_apply_config(app, &app->config, true);
        break;
    case MENU_HIDE:
        toggle_visibility(app);
        break;
    case MENU_ABOUT:
        MessageBoxW(app->window,
                    L"DynamicWindows 0.1.0\n\nCopyright (C) 2026 DynamicWindows contributors.\n"
                    L"Licensed under the GNU General Public License, version 3.\n"
                    L"You may use, modify, and redistribute this program under those terms.\n"
                    L"This program comes with ABSOLUTELY NO WARRANTY.\n\n"
                    L"The full license is in the LICENSE file beside this executable.\n"
                    L"Source and license: github.com/SauloHS/DynamicWindows",
                    L"About DynamicWindows", MB_OK | MB_ICONINFORMATION);
        break;
    case MENU_EXIT:
        PostMessageW(app->window, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

static void tooltip_init(App *app) {
    app->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   app->window, NULL, app->instance, NULL);
    if (!app->tooltip)
        return;
    TOOLINFOW tool = {.cbSize = sizeof(tool),
                      .uFlags = TTF_TRACK | TTF_ABSOLUTE,
                      .hwnd = app->window,
                      .uId = 1,
                      .lpszText = app->tooltip_text};
    SendMessageW(app->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
    SendMessageW(app->tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
}

static void tooltip_hide(App *app) {
    if (!app->tooltip)
        return;
    TOOLINFOW tool = {.cbSize = sizeof(tool), .hwnd = app->window, .uId = 1};
    SendMessageW(app->tooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&tool);
}

static void toggle_visibility(App *app) {
    app->hidden = !app->hidden;
    app->event_until = 0;
    app->media_peek_until = 0;
    if (app->hidden)
        app_close(app);
    app->hover = ACTION_NONE;
    app->hover_blocked = true;
    tooltip_hide(app);
    app_animate(app);
}

static void tooltip_show(App *app, const UiHit *hit) {
    if (!app->tooltip || !hit || !hit->enabled)
        return;
    wcsncpy(app->tooltip_text, hit->label, 599);
    app->tooltip_text[599] = L'\0';
    TOOLINFOW tool = {
        .cbSize = sizeof(tool), .hwnd = app->window, .uId = 1, .lpszText = app->tooltip_text};
    SendMessageW(app->tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&tool);
    POINT point;
    GetCursorPos(&point);
    SendMessageW(app->tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(point.x + 12, point.y + 22));
    SendMessageW(app->tooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&tool);
}

static void pointer_state(App *app, uint64_t now) {
    bool preview = settings_is_preview(app);
    POINT point;
    GetCursorPos(&point);
    ScreenToClient(app->window, &point);
    double x = point.x / app->scale;
    double y = point.y / app->scale;
    bool inside = IsWindowVisible(app->window) && app->visibility.value > 0.9 &&
                  !app->fullscreen_hidden && app_inside(app, x, y);
    if (inside) {
        app->pointer_left = 0;
        if (!app->pointer_entered)
            app->pointer_entered = now;
        int hit = app_hit(app, x, y);
        Action action = hit >= 0 && app->hits[hit].enabled ? app->hits[hit].action : ACTION_NONE;
        if (action != app->hover) {
            app->hover = action;
            app->hover_started = now;
            tooltip_hide(app);
        }
        if (!preview && app->hover_enabled && !app->hover_blocked && !app->island.expanded &&
            app->island.alert == ALERT_NONE &&
            now - app->pointer_entered >= (uint64_t)app->config.hover_delay) {
            app_open(app,
                     app_primary(app) == ACTIVITY_TIMER                         ? ISLAND_TIMER
                     : app_primary(app) == ACTIVITY_NONE && app_headphones(app) ? ISLAND_SYSTEM
                                                                                : ISLAND_MEDIA,
                     false);
            tooltip_hide(app);
        } else if (hit >= 0 && now - app->hover_started >= 350) {
            tooltip_show(app, &app->hits[hit]);
        }
        return;
    }
    app->pointer_entered = 0;
    app->hover_blocked = false;
    app->hover = ACTION_NONE;
    tooltip_hide(app);
    if (!app->pointer_left)
        app->pointer_left = now;
    if (!preview && app->hover_open &&
        now - app->pointer_left >= (uint64_t)app->config.leave_delay && !app->dragging)
        app_close(app);
}

static void crab_receive(App *app, CrabMessage *message) {
    if (!message)
        return;
    app->crab_state = message->state;
    app->crab_error = message->error;
    if (*message->transcript)
        wcsncpy(app->crab_transcript, message->transcript, 767);
    if (*message->answer)
        wcsncpy(app->crab_answer, message->answer, 2047);
    free(message);
    if (app->crab_state == CRAB_ERROR) {
        app->crab_orb_until = GetTickCount64() + 700;
        app->event_until = GetTickCount64() + 5000;
    } else if (app->crab_state == CRAB_IDLE) {
        app->crab_orb_until = 0;
        app->event_until = GetTickCount64() + 2200;
    } else {
        app->event_until = UINT64_MAX;
    }
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, app->window, OBJID_CLIENT, CHILDID_SELF);
    app_animate(app);
    app_render(app);
}

static void notification_store(App *app, const AppNotification *notification) {
    if (app->notification_count < NOTIFICATION_HISTORY)
        ++app->notification_count;
    for (unsigned i = app->notification_count - 1; i > 0; --i)
        app->notifications[i] = app->notifications[i - 1];
    app->notifications[0] = *notification;
    app->notification_index = 0;
}

static void notification_receive(App *app, NotificationMessage *message) {
    if (!message)
        return;
    app->notification_service.access = message->access;
    app->notification_service.error = message->error;
    if (message->has_notification) {
        notification_store(app, &message->notification);
        app->notification_alert = message->notification;
        uint64_t now = GetTickCount64();
        island_show_alert(&app->island, ALERT_NOTIFICATION, now);
        if (app->island.alert == ALERT_NOTIFICATION) {
            app->island.alert_deadline = now + (uint64_t)app->config.notification_alert_ms;
            app->event_until = app->island.alert_deadline;
            NotifyWinEvent(EVENT_SYSTEM_ALERT, app->window, OBJID_CLIENT, CHILDID_SELF);
        }
    }
    free(message);
    app_animate(app);
    app_render(app);
}

static void headphones_receive(App *app, HeadphoneSnapshot *snapshot) {
    if (!snapshot)
        return;
    if (FAILED(snapshot->error) && app->headphones.initialized &&
        SUCCEEDED(app->headphones.error)) {
        free(snapshot);
        return;
    }
    HeadphoneEvent events[HEADPHONES_LIMIT * 2];
    unsigned count = headphones_changes(&app->headphones, snapshot, events, HEADPHONES_LIMIT * 2);
    app->headphones = *snapshot;
    free(snapshot);
    for (unsigned i = 0; i < count; ++i) {
        if (!app->config.headphone_alerts || events[i].change == HEADPHONE_BATTERY)
            continue;
        uint64_t now = GetTickCount64();
        island_show_alert(&app->island, ALERT_HEADPHONES, now);
        if (app->island.alert != ALERT_HEADPHONES)
            continue;
        app->headphone_event = events[i];
        app->island.alert_deadline = now + (uint64_t)app->config.headphone_alert_ms;
        app_event(app, false);
        app->event_until = app->island.alert_deadline;
        NotifyWinEvent(EVENT_SYSTEM_ALERT, app->window, OBJID_CLIENT, CHILDID_SELF);
        break;
    }
    app_animate(app);
}

static void system_poll(App *app, uint64_t now) {
    SystemSnapshot snapshot;
    platform_poll(&app->platform, &snapshot, now);
    if (app->system_sampled) {
        if (app->config.volume_alerts && snapshot.audio_available && app->system.audio_available &&
            (fabs(snapshot.volume - app->system.volume) > 0.005 ||
             snapshot.muted != app->system.muted)) {
            island_show_alert(&app->island, ALERT_VOLUME, now);
            if (app->island.alert == ALERT_VOLUME)
                app->island.alert_deadline = now + (uint64_t)app->config.volume_alert_ms;
            app_event(app, false);
        }
        if (app->config.power_alerts && snapshot.power_known && app->system.power_known &&
            snapshot.plugged_in != app->system.plugged_in) {
            island_show_alert(&app->island, ALERT_POWER, now);
            if (app->island.alert == ALERT_POWER)
                app->island.alert_deadline = now + (uint64_t)app->config.power_alert_ms;
            app_event(app, false);
        }
    }
    app->system = snapshot;
    app->system_sampled = true;
    for (int i = 9; i > 0; --i)
        app->peaks[i] = app->peaks[i - 1];
    app->peaks[0] = app->system.peak;
}

static void frame(App *app) {
    uint64_t now = GetTickCount64();
    LARGE_INTEGER clock;
    QueryPerformanceCounter(&clock);
    double seconds =
        app->last_clock.QuadPart
            ? (double)(clock.QuadPart - app->last_clock.QuadPart) / app->clock_frequency.QuadPart
            : 1.0 / app->config.frame_rate;
    app->last_clock = clock;
    app->last_frame = now;
    if (app->quit_at && now >= app->quit_at) {
        PostMessageW(app->window, WM_CLOSE, 0, 0);
        return;
    }
    if (island_tick(&app->island, now)) {
        if (app->island.expanded)
            island_open(&app->island, ISLAND_TIMER);
        if (!app->test_mode) {
            if (app->config.timer_sound)
                MessageBeep(MB_ICONINFORMATION);
            NotifyWinEvent(EVENT_SYSTEM_ALERT, app->window, OBJID_CLIENT, CHILDID_SELF);
        }
        app_event(app, false);
        tray_tooltip(app);
    }
    bool crab_visible = app->crab_state == CRAB_LISTENING || app->crab_state == CRAB_THINKING ||
                        app->crab_state == CRAB_SPEAKING || now < app->crab_orb_until;
    notifications_tick(&app->notification_service);
    if (now - app->last_poll >= 50) {
        system_poll(app, now);
        bool fullscreen = app->quiet_fullscreen && !settings_is_preview(app) &&
                          platform_fullscreen(app->window, app->monitor);
        if (fullscreen != app->fullscreen_hidden)
            app->fullscreen_hidden = fullscreen;
        if (!crab_visible)
            pointer_state(app, now);
        app->last_poll = now;
    }
    IslandSize size = app_target_size(app);
    double response = app->config.response_ms * (app->island.expanded ? 1 : 0.85);
    double damping = app->config.damping;
    bool moving = island_spring_tuned(&app->width, size.width, seconds, app->reduced_motion,
                                      response, damping);
    moving |= island_spring_tuned(&app->height, size.height, seconds, app->reduced_motion, response,
                                  damping);
    moving |= island_spring_tuned(&app->radius, size.radius, seconds, app->reduced_motion, response,
                                  damping);
    moving |= island_spring_tuned(&app->bubble, size.bubble, seconds, app->reduced_motion, response,
                                  damping);
    moving |= island_spring_tuned(&app->headphone_bubble, size.headphones, seconds,
                                  app->reduced_motion, response, damping);
    moving |= island_spring_tuned(&app->reveal, app->island.expanded ? 1 : 0, seconds,
                                  app->reduced_motion, response, 100);
    double smooth = 1 - exp(-island_clamp(seconds, 0, 0.1) * 16);
    if (!app->reduced_motion && crab_visible)
        app->crab_phase = fmod(app->crab_phase + seconds * 1.7, 6.28318530718);
    for (int i = 0; i < 5; ++i)
        app->wave_levels[i] += (app->peaks[i * 2] - app->wave_levels[i]) * smooth;
    bool idle_hidden = app->config.idle == IDLE_HIDDEN && app_primary(app) == ACTIVITY_NONE &&
                       !app_headphones(app) && app->island.alert == ALERT_NONE &&
                       !app->island.expanded && !settings_is_preview(app);
    bool event_visible = now < app->event_until;
    bool visible = !app->fullscreen_hidden &&
                   ((!app->hidden && !idle_hidden) || event_visible || crab_visible);
    moving |= island_spring_tuned(&app->visibility, visible ? 1 : 0, seconds, app->reduced_motion,
                                  300, 100);
    moving |= island_spring_tuned(&app->media_peek, now < app->media_peek_until ? 1 : 0, seconds,
                                  app->reduced_motion, 260, 100);
    double target_position = app_media_position(app);
    if (app->media.playing)
        app->media_display_position += seconds * app->media.rate;
    double correction = target_position - app->media_display_position;
    if (fabs(correction) > 3)
        app->media_display_position = target_position;
    else
        app->media_display_position += correction * (1 - exp(-seconds * 7));
    app->media_display_position =
        island_clamp(app->media_display_position, 0, fmax(0, app->media.duration));
    bool drawing = visible || app->visibility.value > 0.001;
    if ((IsWindowVisible(app->window) != FALSE) != drawing)
        ShowWindow(app->window, drawing ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (drawing)
        app_render(app);
    bool active_motion = (app->media.playing && app->config.show_media &&
                          ((!app->reduced_motion && app->config.waveform) ||
                           (app->island.expanded && app->config.media_progress))) ||
                         (!app->reduced_motion && crab_visible);
    unsigned interval = moving || (drawing && active_motion)
                            ? (unsigned)lround(1000.0 / app->config.frame_rate)
                        : drawing ? 50
                                  : 250;
    frame_interval(app, interval);
}

static void keyboard_focus(App *app, int direction) {
    if (app->hit_count == 0)
        return;
    int index = app->focus;
    for (int i = 0; i < app->hit_count; ++i) {
        index = (index + direction + app->hit_count) % app->hit_count;
        if (app->hits[index].enabled) {
            app->focus = index;
            NotifyWinEvent(EVENT_OBJECT_FOCUS, app->window, OBJID_CLIENT, index + 1);
            app_render(app);
            tooltip_show(app, &app->hits[index]);
            return;
        }
    }
}

static unsigned shortcut_modifiers(void) {
    return (GetKeyState(VK_CONTROL) < 0 ? MOD_CONTROL : 0) |
           (GetKeyState(VK_MENU) < 0 ? MOD_ALT : 0) | (GetKeyState(VK_SHIFT) < 0 ? MOD_SHIFT : 0) |
           (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 ? MOD_WIN : 0);
}

static void shortcut_action(App *app, ShortcutAction shortcut) {
    switch (shortcut) {
    case SHORTCUT_ISLAND:
        if (app->island.expanded && GetForegroundWindow() == app->window)
            app_close(app);
        else
            app_open(app, app->island.view, true);
        break;
    case SHORTCUT_TIMER:
        app_open(app, ISLAND_TIMER, true);
        break;
    case SHORTCUT_HIDE:
        toggle_visibility(app);
        break;
    case SHORTCUT_SETTINGS:
    case SHORTCUT_SETTINGS_LOCAL:
    case SHORTCUT_SETTINGS_ALT:
        settings_show(app);
        break;
    case SHORTCUT_SEARCH:
        app_action(app, ACTION_SEARCH, 0);
        break;
    case SHORTCUT_CLOSE:
        app_close(app);
        break;
    case SHORTCUT_MEDIA_VIEW:
    case SHORTCUT_TIMER_VIEW:
    case SHORTCUT_SYSTEM_VIEW:
        app_action(app, (Action)(ACTION_TAB_MEDIA + shortcut - SHORTCUT_MEDIA_VIEW), 0);
        break;
    case SHORTCUT_PLAY:
        if (app->media.can_toggle && app->island.view == ISLAND_MEDIA)
            app_action(app, ACTION_PLAY, 0);
        break;
    case SHORTCUT_LEFT:
    case SHORTCUT_RIGHT:
    case SHORTCUT_START:
    case SHORTCUT_END: {
        if (app->focus < 0 || app->focus >= app->hit_count || !app->hits[app->focus].enabled)
            return;
        Action action = app->hits[app->focus].action;
        double value;
        if (action == ACTION_VOLUME)
            value = app->system.volume + (shortcut == SHORTCUT_LEFT ? -0.02 : 0.02);
        else if (action == ACTION_SEEK && app->media.duration > 0)
            value = (app_media_position(app) + (shortcut == SHORTCUT_LEFT ? -5 : 5)) /
                    app->media.duration;
        else
            return;
        if (shortcut == SHORTCUT_START || shortcut == SHORTCUT_END)
            value = shortcut == SHORTCUT_START ? 0 : 1;
        app_action(app, action, value);
        break;
    }
    case SHORTCUT_COUNT:
        break;
    }
}

static void key_action(App *app, WPARAM key) {
    if (key == VK_TAB) {
        keyboard_focus(app, GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
        return;
    }
    if ((key == VK_RETURN || key == VK_SPACE) && app->focus >= 0 && app->focus < app->hit_count) {
        UiHit hit = app->hits[app->focus];
        if (hit.enabled && hit.action != ACTION_VOLUME && hit.action != ACTION_SEEK)
            app_action(app, hit.action, 0);
        return;
    }
    unsigned modifiers = shortcut_modifiers();
    for (int i = SHORTCUT_GLOBAL_COUNT; i < SHORTCUT_COUNT; ++i) {
        ShortcutBinding binding = app->config.shortcuts[i];
        if (binding.key == key && binding.modifiers == modifiers) {
            shortcut_action(app, (ShortcutAction)i);
            return;
        }
    }
}

static double slider_value(const App *app, Action action, double x) {
    for (int i = 0; i < app->hit_count; ++i) {
        if (app->hits[i].action != action)
            continue;
        double panel_scale = fmin(app->renderer.body.Width / 408, app->renderer.body.Height / 264);
        panel_scale = island_clamp(panel_scale, 0.1, 3.1);
        double left = app->hits[i].rect.X + 4 * panel_scale;
        double width = app->hits[i].rect.Width - 8 * panel_scale;
        return width > 0 ? island_clamp((x - left) / width, 0, 1) : 0;
    }
    return 0;
}

static void mouse_down(App *app, LPARAM position) {
    double x = GET_X_LPARAM(position) / app->scale;
    double y = GET_Y_LPARAM(position) / app->scale;
    int index = app_hit(app, x, y);
    if (index < 0 || !app->hits[index].enabled)
        return;
    tooltip_hide(app);
    UiHit hit = app->hits[index];
    app->hover_open = false;
    app->focus = -1;
    if (app->island.expanded) {
        if (GetForegroundWindow() != app->window)
            app->previous_foreground = GetForegroundWindow();
        SetForegroundWindow(app->window);
        SetFocus(app->window);
        outside_tracking(app, true);
    }
    app->pressed = hit.action;
    SetCapture(app->window);
    if (hit.action == ACTION_VOLUME || hit.action == ACTION_SEEK) {
        app->dragging = true;
        if (hit.action == ACTION_VOLUME)
            app_action(app, hit.action, slider_value(app, hit.action, x));
    }
    app_render(app);
}

static void mouse_up(App *app, LPARAM position) {
    double x = GET_X_LPARAM(position) / app->scale;
    double y = GET_Y_LPARAM(position) / app->scale;
    Action action = app->pressed;
    bool dragged = app->dragging;
    app->pressed = ACTION_NONE;
    app->dragging = false;
    if (GetCapture() == app->window)
        ReleaseCapture();
    if (dragged) {
        app_action(app, action, slider_value(app, action, x));
        return;
    }
    int hit = app_hit(app, x, y);
    if (hit >= 0 && app->hits[hit].enabled && app->hits[hit].action == action)
        app_action(app, action, 0);
    app_render(app);
}

static void receive_media(App *app, MediaSnapshot *snapshot) {
    uint64_t now = GetTickCount64();
    double new_position = snapshot->position;
    if (snapshot->playing && snapshot->sampled_at && now >= snapshot->sampled_at)
        new_position += (now - snapshot->sampled_at) / 1000.0 * snapshot->rate;
    bool track_changed = wcscmp(app->media.title, snapshot->title) != 0 ||
                         wcscmp(app->media.artist, snapshot->artist) != 0 ||
                         wcscmp(app->media.source, snapshot->source) != 0;
    bool changed = track_changed || app->media.available != snapshot->available ||
                   app->media.playing != snapshot->playing ||
                   fabs(app->media.duration - snapshot->duration) > 0.1 ||
                   (snapshot->timeline_valid && fabs(new_position - app_media_position(app)) > 2);
    app->media_target_position = new_position;
    app->media_position_at = now;
    if (track_changed || fabs(new_position - app->media_display_position) > 3)
        app->media_display_position = new_position;
    free(app->media.artwork);
    app->media = *snapshot;
    free(snapshot);
    if (app->media.artwork_changed)
        renderer_artwork(app, app->media.artwork, app->media.artwork_size);
    free(app->media.artwork);
    app->media.artwork = NULL;
    app->media.artwork_size = 0;
    app->island.media_available = app->media.available && app->config.show_media;
    app->island.media_playing = app->media.playing;
    if (FAILED(app->media.error))
        swprintf(app->notice, 160, L"Windows media request failed (0x%08lx).",
                 (unsigned long)app->media.error);
    else if (wcsncmp(app->notice, L"Windows media request failed", 27) == 0)
        app->notice[0] = L'\0';
    if (changed) {
        if (app->config.show_media)
            app_event(app, true);
        app_animate(app);
        tray_tooltip(app);
        NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, app->window, OBJID_CLIENT, CHILDID_SELF);
    }
    app_render(app);
}

static void shutdown_app(App *app) {
    KillTimer(app->window, FRAME_TIMER);
    if (app->frame_timer) {
        CancelWaitableTimer(app->frame_timer);
        CloseHandle(app->frame_timer);
        app->frame_timer = NULL;
    }
    settings_destroy(app);
    media_stop(&app->media_service);
    crab_stop(&app->crab_service);
    notifications_stop(&app->notification_service);
    headphones_stop(&app->headphone_service);
    MSG message;
    while (PeekMessageW(&message, app->window, WM_DYNAMICWINDOWS_CRAB, WM_DYNAMICWINDOWS_CRAB,
                        PM_REMOVE))
        free((CrabMessage *)message.lParam);
    while (PeekMessageW(&message, app->window, WM_DYNAMICWINDOWS_NOTIFICATION,
                        WM_DYNAMICWINDOWS_NOTIFICATION, PM_REMOVE))
        free((NotificationMessage *)message.lParam);
    while (PeekMessageW(&message, app->window, WM_DYNAMICWINDOWS_HEADPHONES,
                        WM_DYNAMICWINDOWS_HEADPHONES, PM_REMOVE))
        free((HeadphoneSnapshot *)message.lParam);
    while (PeekMessageW(&message, app->window, WM_DYNAMICWINDOWS_MEDIA, WM_DYNAMICWINDOWS_MEDIA,
                        PM_REMOVE))
        media_snapshot_free((MediaSnapshot *)message.lParam);
    accessibility_disconnect(app);
    platform_destroy(&app->platform);
    outside_tracking(app, false);
    for (int i = 0; i < SHORTCUT_GLOBAL_COUNT; ++i)
        if (app->hotkeys[i])
            UnregisterHotKey(app->window, i + 1);
    if (app->crab_hotkey)
        UnregisterHotKey(app->window, SHORTCUT_GLOBAL_COUNT + 1);
    if (app->tray_added)
        Shell_NotifyIconW(NIM_DELETE, &app->tray);
    if (app->tooltip)
        DestroyWindow(app->tooltip);
    renderer_destroy(&app->renderer);
    if (app->icon)
        DestroyIcon(app->icon);
    free(app->media.artwork);
    app->initialized = false;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        app = create->lpCreateParams;
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app);
    }
    if (!app)
        return DefWindowProcW(window, message, wparam, lparam);
    if (message == app->taskbar_created && app->taskbar_created) {
        tray_add(app);
        return 0;
    }
    switch (message) {
    case WM_NCHITTEST: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);
        return app_inside(app, point.x / app->scale, point.y / app->scale) ? HTCLIENT
                                                                           : HTTRANSPARENT;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, app->hover != ACTION_NONE ? IDC_HAND : IDC_ARROW));
        return TRUE;
    case WM_LBUTTONDOWN:
        mouse_down(app, lparam);
        return 0;
    case WM_LBUTTONUP:
        mouse_up(app, lparam);
        return 0;
    case WM_MOUSEMOVE: {
        Action previous = app->hover;
        pointer_state(app, GetTickCount64());
        TRACKMOUSEEVENT tracking = {
            .cbSize = sizeof(tracking), .dwFlags = TME_LEAVE, .hwndTrack = window};
        TrackMouseEvent(&tracking);
        if (previous != app->hover)
            app_render(app);
        if (app->dragging && app->pressed == ACTION_VOLUME)
            app_action(app, ACTION_VOLUME,
                       slider_value(app, ACTION_VOLUME, GET_X_LPARAM(lparam) / app->scale));
        return 0;
    }
    case WM_MOUSELEAVE:
        app->hover = ACTION_NONE;
        tooltip_hide(app);
        app_render(app);
        return 0;
    case WM_CAPTURECHANGED:
        app->dragging = false;
        app->pressed = ACTION_NONE;
        return 0;
    case WM_MOUSEWHEEL:
        if (app->system.audio_available) {
            double delta =
                (double)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * app->config.wheel_step / 100;
            app_action(app, ACTION_VOLUME, app->system.volume + delta);
        }
        return 0;
    case WM_CONTEXTMENU:
        tray_menu(app);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        key_action(app, wparam);
        return 0;
    case WM_SETFOCUS:
        NotifyWinEvent(EVENT_OBJECT_FOCUS, window, OBJID_CLIENT, CHILDID_SELF);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && app->initialized && !app->hover_open &&
            !app->dragging && app->island.expanded && app->config.close_on_blur &&
            !settings_is_preview(app))
            app_close(app);
        return 0;
    case WM_HOTKEY:
        if (wparam >= 1 && wparam <= SHORTCUT_GLOBAL_COUNT)
            shortcut_action(app, (ShortcutAction)(wparam - 1));
        else if (wparam == SHORTCUT_GLOBAL_COUNT + 1)
            app_action(app, ACTION_CRAB, 0);
        return 0;
    case WM_DYNAMICWINDOWS_OUTSIDE:
        if (app->island.expanded && !app->hover_open)
            app_close(app);
        return 0;
    case WM_TIMER:
        if (app->initialized)
            frame(app);
        return 0;
    case WM_DYNAMICWINDOWS_MEDIA:
        receive_media(app, (MediaSnapshot *)lparam);
        return 0;
    case WM_DYNAMICWINDOWS_ACCESS:
        if (wparam == ACTION_NONE) {
            app->focus = (int)lparam;
            SetForegroundWindow(window);
            SetFocus(window);
            app_render(app);
        } else {
            app_action(app, (Action)wparam, (double)lparam / 10000);
        }
        return 0;
    case WM_DYNAMICWINDOWS_CRAB:
        crab_receive(app, (CrabMessage *)lparam);
        return 0;
    case WM_DYNAMICWINDOWS_NOTIFICATION:
        notification_receive(app, (NotificationMessage *)lparam);
        return 0;
    case WM_DYNAMICWINDOWS_HEADPHONES:
        headphones_receive(app, (HeadphoneSnapshot *)lparam);
        return 0;
    case WM_DEVICECHANGE:
        headphones_wake(&app->headphone_service);
        app->platform.next_device_check = 0;
        return 0;
    case WM_DYNAMICWINDOWS_TRAY:
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP)
            tray_menu(app);
        else if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT)
            app_open(app, app->island.view, true);
        return 0;
    case WM_GETOBJECT:
        return accessibility_object(app, wparam, lparam);
    case WM_SETTINGCHANGE:
        motion_setting(app);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        position_window(app, configured_monitor(app));
        app_render(app);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        shutdown_app(app);
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static bool parse_options(Options *options) {
    *options = (Options){.view = ISLAND_MEDIA};
    int count;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return false;
    bool valid = true;
    for (int i = 1; i < count && valid; ++i) {
        if (wcscmp(arguments[i], L"--help") == 0 || wcscmp(arguments[i], L"-h") == 0)
            options->help = true;
        else if (wcscmp(arguments[i], L"--demo") == 0)
            options->demo = true;
        else if (wcscmp(arguments[i], L"--settings") == 0)
            options->settings = true;
        else if (wcscmp(arguments[i], L"--isolated") == 0)
            options->isolated = true;
        else if (wcscmp(arguments[i], L"--config") == 0 && i + 1 < count) {
            wcsncpy(options->config_path, arguments[++i], 1023);
        } else if (wcscmp(arguments[i], L"--reduced-motion") == 0)
            options->reduced = true;
        else if (wcscmp(arguments[i], L"--smoke-test") == 0 && i + 1 < count) {
            options->smoke = true;
            wcsncpy(options->smoke_dir, arguments[++i], MAX_PATH - 1);
        } else if (wcscmp(arguments[i], L"--view") == 0 && i + 1 < count) {
            const wchar_t *view = arguments[++i];
            options->expanded = true;
            if (wcscmp(view, L"timer") == 0)
                options->view = ISLAND_TIMER;
            else if (wcscmp(view, L"system") == 0)
                options->view = ISLAND_SYSTEM;
            else if (wcscmp(view, L"media") == 0)
                options->view = ISLAND_MEDIA;
            else
                valid = false;
        } else if (wcscmp(arguments[i], L"--run-for") == 0 && i + 1 < count) {
            wchar_t *end;
            unsigned long seconds = wcstoul(arguments[++i], &end, 10);
            valid = *end == L'\0' && seconds > 0 && seconds <= 3600;
            options->seconds = (unsigned)seconds;
        } else {
            valid = false;
        }
    }
    LocalFree(arguments);
    return valid;
}

static bool smoke_frame(App *app, const wchar_t *directory, const wchar_t *name) {
    IslandSize size = app_target_size(app);
    app->reveal = (IslandSpring){app->island.expanded ? 1 : 0, 0};
    app->visibility = (IslandSpring){1, 0};
    app->width = (IslandSpring){size.width, 0};
    app->height = (IslandSpring){size.height, 0};
    app->radius = (IslandSpring){size.radius, 0};
    app->bubble = (IslandSpring){size.bubble, 0};
    app->headphone_bubble = (IslandSpring){size.headphones, 0};
    if (!renderer_draw(app))
        return false;
    double x = app->renderer.body.X + app->renderer.body.Width / 2;
    double y = app->renderer.body.Y + app->renderer.body.Height / 2;
    if (!app_inside(app, x, y) || app_inside(app, 0, app->canvas_height - 1))
        return false;
    wchar_t path[1024];
    swprintf(path, 1024, L"%ls/%ls.png", directory, name);
    return renderer_save(app, path);
}

static int smoke_test(App *app, const wchar_t *directory) {
    DWORD attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 1;
    bool passed = smoke_frame(app, directory, L"idle");
    app->headphones = (HeadphoneSnapshot){.initialized = true, .count = 1};
    app->headphones.devices[0] =
        (HeadphoneDevice){.bluetooth = true, .headphones = true, .battery_percent = 80};
    wcscpy(app->headphones.devices[0].id, L"synthetic-earbuds");
    wcscpy(app->headphones.devices[0].name, L"Wireless earbuds");
    passed &= smoke_frame(app, directory, L"headphones-idle");
    passed &= app_target_size(app).width == app->config.idle_width + app->config.media_extra_width;
    passed &= app_target_size(app).height == app->config.compact_height;
    app->headphone_event = (HeadphoneEvent){HEADPHONE_CONNECTED, app->headphones.devices[0]};
    island_show_alert(&app->island, ALERT_HEADPHONES, 0);
    passed &= smoke_frame(app, directory, L"headphones-connected");
    passed &= app_target_size(app).radius == app->config.alert_height / 2;
    app->headphone_event.device.battery_percent = -1;
    passed &= smoke_frame(app, directory, L"headphones-battery-unavailable");
    app->headphones.devices[0].battery_percent = 0;
    app->island.alert = ALERT_NONE;
    passed &= smoke_frame(app, directory, L"headphones-battery-empty");
    app->config.headphone_status = false;
    passed &= smoke_frame(app, directory, L"headphones-status-disabled");
    app->config.headphone_status = true;
    app->crab_state = CRAB_LISTENING;
    passed &= app_target_size(app).width == 72;
    passed &= app_target_size(app).height == 72;
    passed &= app_target_size(app).radius == 36;
    passed &= smoke_frame(app, directory, L"crab-listening");
    app->crab_state = CRAB_THINKING;
    passed &= smoke_frame(app, directory, L"crab-thinking");
    app->crab_state = CRAB_SPEAKING;
    passed &= smoke_frame(app, directory, L"crab-speaking");
    app->crab_state = CRAB_ERROR;
    app->crab_error = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    app->crab_orb_until = GetTickCount64() + 700;
    passed &= smoke_frame(app, directory, L"crab-error-orb");
    app->crab_orb_until = 0;
    IslandSize crab_error_size = app_target_size(app);
    passed &= crab_error_size.width == app->config.alert_width;
    passed &= crab_error_size.height == app->config.alert_height;
    passed &= smoke_frame(app, directory, L"crab-error-text");
    app->crab_state = CRAB_DISABLED;
    island_open(&app->island, ISLAND_MEDIA);
    passed &= smoke_frame(app, directory, L"media-empty");
    app->media = (MediaSnapshot){.initialized = true,
                                 .available = true,
                                 .playing = true,
                                 .can_toggle = true,
                                 .can_previous = true,
                                 .can_next = true,
                                 .can_seek = true,
                                 .position = 72,
                                 .duration = 216,
                                 .sampled_at = GetTickCount64()};
    wcscpy(app->media.title, L"Midnight City");
    wcscpy(app->media.artist, L"M83");
    wcscpy(app->media.source, L"Demo media session");
    app->island.media_available = true;
    app->island.media_playing = true;
    app->system = (SystemSnapshot){.audio_available = true,
                                   .volume = 0.65f,
                                   .peak = 0.3f,
                                   .power_known = true,
                                   .plugged_in = true,
                                   .has_battery = true,
                                   .charging = true,
                                   .battery_percent = 78};
    wcscpy(app->system.output_name, L"Speakers (Windows audio)");
    for (int i = 0; i < 10; ++i)
        app->peaks[i] = 0.03f + (float)(i % 4) * 0.06f;
    island_close(&app->island);
    IslandSize media_headphones = app_target_size(app);
    passed &= media_headphones.width == app->config.idle_width + app->config.media_extra_width;
    passed &= media_headphones.bubble == 0;
    passed &= media_headphones.headphones == media_headphones.height;
    passed &= smoke_frame(app, directory, L"media-compact-headphones");
    passed &= island_timer_start(&app->island, 300000, GetTickCount64());
    app->island.preferred = ACTIVITY_MEDIA;
    IslandSize dual_headphones = app_target_size(app);
    passed &= dual_headphones.bubble == app->config.bubble_size;
    passed &= dual_headphones.headphones == dual_headphones.height;
    passed &= smoke_frame(app, directory, L"dual-activity-headphones");
    island_open(&app->island, ISLAND_MEDIA);
    passed &= smoke_frame(app, directory, L"media-expanded");
    passed &= app_target_size(app).headphones == 0;
    passed &= app->renderer.headphone_bubble.Width == 0;
    passed &= app->renderer.body.X >= 0;
    app->island.timer.remaining = 187000;
    island_open(&app->island, ISLAND_TIMER);
    passed &= smoke_frame(app, directory, L"timer-running");
    island_timer_reset(&app->island);
    passed &= smoke_frame(app, directory, L"timer-ready");
    island_open(&app->island, ISLAND_SYSTEM);
    passed &= smoke_frame(app, directory, L"system-expanded");
    island_close(&app->island);
    island_show_alert(&app->island, ALERT_VOLUME, GetTickCount64());
    passed &= smoke_frame(app, directory, L"volume-alert");
    island_show_alert(&app->island, ALERT_POWER, GetTickCount64());
    passed &= smoke_frame(app, directory, L"charging-alert");
    passed &= island_timer_start(&app->island, 1, 0);
    passed &= island_tick(&app->island, 1);
    passed &= smoke_frame(app, directory, L"timer-finished");
    IAccessible *accessible = accessibility_create(app);
    AppConfig defaults = app->config;
    config_defaults(&app->config);
    app->config.idle = IDLE_TEXT;
    wcscpy(app->config.idle_text, L"My island");
    app->config.idle_width = 320;
    app->config.compact_height = 64;
    app->config.compact_radius = 32;
    app->config.expanded_width = 620;
    app->config.expanded_height = 380;
    app->config.colors[TINT_BACKGROUND] = 0x0e1726;
    config_normalize(&app->config);
    position_window(app, app->monitor);
    passed &= renderer_style(app);
    app->island.timer.duration = (uint64_t)app->config.timer_seconds * 1000;
    island_timer_reset(&app->island);
    app->island.alert = ALERT_NONE;
    app->island.media_available = false;
    passed &= smoke_frame(app, directory, L"custom-idle");
    island_open(&app->island, ISLAND_TIMER);
    passed &= smoke_frame(app, directory, L"custom-expanded");
    app->config = defaults;
    position_window(app, app->monitor);
    passed &= renderer_style(app);
    island_close(&app->island);
    island_show_alert(&app->island, ALERT_TIMER, GetTickCount64());
    passed &= smoke_frame(app, directory, L"timer-finished");
    if (!accessible) {
        passed = false;
    } else {
        VARIANT child = {.vt = VT_I4, .lVal = CHILDID_SELF};
        BSTR name = NULL;
        LONG children = 0;
        passed &= SUCCEEDED(IAccessible_get_accName(accessible, child, &name)) && name != NULL;
        passed &= SUCCEEDED(IAccessible_get_accChildCount(accessible, &children)) && children > 0;
        SysFreeString(name);
        child.lVal = 1;
        VARIANT role;
        VariantInit(&role);
        passed &= SUCCEEDED(IAccessible_get_accRole(accessible, child, &role)) &&
                  role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PUSHBUTTON;
        VariantClear(&role);
    }
    FILE *report = NULL;
    wchar_t path[1024];
    swprintf(path, 1024, L"%ls/result.txt", directory);
    report = _wfopen(path, L"w");
    if (report) {
        fprintf(report,
                "%s: 23 native render states, layout, Crab error states, alpha hit-testing, MSAA "
                "names and roles\n",
                passed ? "PASS" : "FAIL");
        fclose(report);
    } else {
        passed = false;
    }
    return passed ? 0 : 1;
}

static bool create_app(App *app, const Options *options) {
    *app = (App){.instance = GetModuleHandleW(NULL),
                 .test_mode = options->smoke,
                 .demo = options->demo,
                 .motion_allowed = !options->reduced,
                 .scale = 1,
                 .hover_enabled = true,
                 .quiet_fullscreen = true,
                 .focus = -1};
    island_init(&app->island);
    config_defaults(&app->config);
    if (*options->config_path)
        GetFullPathNameW(options->config_path, 1024, app->config_path, NULL);
    else if (!options->smoke && !options->isolated)
        config_default_path(app->config_path, 1024);
    if (*app->config_path)
        config_load(&app->config, app->config_path);
    if (options->smoke || options->isolated)
        app->config.settings_on_start = false;
    if (options->reduced)
        app->config.reduced_motion = true;
    app->hover_enabled = app->config.hover_expand;
    app->quiet_fullscreen = app->config.hide_fullscreen;
    app->island.timer.duration = (uint64_t)app->config.timer_seconds * 1000;
    app->island.timer.remaining = app->island.timer.duration;
    QueryPerformanceFrequency(&app->clock_frequency);
    motion_setting(app);
    WNDCLASSEXW window = {.cbSize = sizeof(window),
                          .lpfnWndProc = window_proc,
                          .hInstance = app->instance,
                          .hCursor = LoadCursorW(NULL, IDC_ARROW),
                          .lpszClassName = window_class};
    if (!RegisterClassExW(&window))
        return false;
    app->window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, window_class,
                                  L"DynamicWindows", WS_POPUP, 0, 0, UI_WIDTH, UI_HEIGHT, NULL,
                                  NULL, app->instance, app);
    if (!app->window)
        return false;
    position_window(app, configured_monitor(app));
    if (!renderer_init(app))
        return false;
    IslandSize size = app_target_size(app);
    app->width.value = size.width;
    app->height.value = size.height;
    app->radius.value = size.radius;
    app->bubble.value = size.bubble;
    app->headphone_bubble.value = size.headphones;
    app->visibility.value = 1;
    app->icon = renderer_icon(app);
    SendMessageW(app->window, WM_SETICON, ICON_SMALL, (LPARAM)app->icon);
    SendMessageW(app->window, WM_SETICON, ICON_BIG, (LPARAM)app->icon);
    (void)platform_init(&app->platform);
    system_poll(app, GetTickCount64());
    tooltip_init(app);
    app->initialized = true;
    app_render(app);
    if (options->smoke)
        return true;
    if (!headphones_start(&app->headphone_service, app->window))
        app->headphones = (HeadphoneSnapshot){.initialized = true, .error = E_FAIL};
    if (!crab_start(&app->crab_service, app->window, app->config.crab_enabled,
                    app->config.crab_speak, app->config.crab_provider, app->config.crab_name,
                    app->config.crab_base_url, app->config.crab_model)) {
        app->crab_state = CRAB_ERROR;
        app->crab_error = E_FAIL;
    } else {
        app->crab_state = app->config.crab_enabled ? CRAB_IDLE : CRAB_DISABLED;
    }
    notifications_configure(&app->notification_service, app->window,
                            app->config.notifications_enabled, app->config.notification_content);
    if (options->demo) {
        app->media = (MediaSnapshot){.initialized = true,
                                     .available = true,
                                     .playing = true,
                                     .can_toggle = true,
                                     .position = 72,
                                     .duration = 216,
                                     .sampled_at = GetTickCount64()};
        wcscpy(app->media.title, L"Midnight City");
        wcscpy(app->media.artist, L"M83");
        wcscpy(app->media.source, L"Demo - simulated media");
        app->media.rate = 1;
        app->media_target_position = app->media.position;
        app->media_display_position = app->media.position;
        app->media_position_at = GetTickCount64();
        app->island.media_available = true;
        app->island.media_playing = true;
    } else if (!media_start(&app->media_service, app->window)) {
        app->media.error = E_FAIL;
        app->media.initialized = true;
    }
    app->taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    tray_add(app);
    if (!app_register_shortcuts(app))
        wcscpy(app->notice, L"A shortcut is already in use. Choose another binding in Shortcuts.");
    if (options->seconds)
        app->quit_at = GetTickCount64() + (uint64_t)options->seconds * 1000;
    app->frame_timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002, TIMER_ALL_ACCESS);
    if (!app->frame_timer)
        app->frame_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (!app->frame_timer)
        return false;
    app_animate(app);
    ShowWindow(app->window, SW_SHOWNOACTIVATE);
    if (options->expanded)
        app_open(app, options->view, true);
    if (options->settings || app->config.settings_on_start)
        settings_show(app);
    return true;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    Options options;
    if (!parse_options(&options)) {
        MessageBoxW(NULL, L"Invalid option. Use --help to see supported options.",
                    L"DynamicWindows", MB_ICONERROR);
        return 2;
    }
    if (options.help) {
        MessageBoxW(NULL,
                    L"DynamicWindows\n\n"
                    L"Ctrl+Alt+D: open/collapse island\nCtrl+Alt+T: open timer\n"
                    L"Ctrl+Alt+H: hide/show island\nCtrl+Alt+Space: start/cancel Crab\n"
                    L"Click/hover: expand\nRight-click: settings and quit\n"
                    L"Tab: move focus\n1/2/3: media/timer/system\nEscape: collapse\n"
                    L"Mouse wheel: volume\n\n"
                    L"--view media|timer|system\n--reduced-motion\n--demo (simulated media)\n"
                    L"--run-for SECONDS (automatic exit)\n--settings\n--config FILE\n"
                    L"--smoke-test DIRECTORY",
                    L"DynamicWindows", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    HANDLE mutex = NULL;
    if (!options.smoke) {
        mutex = CreateMutexW(NULL, FALSE, L"Local\\DynamicWindows.ActivityIsland");
        if (!mutex)
            return 1;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existing = FindWindowW(window_class, NULL);
            if (existing)
                PostMessageW(existing, WM_DYNAMICWINDOWS_ACCESS,
                             options.settings ? ACTION_SETTINGS : ACTION_EXPAND, 0);
            CloseHandle(mutex);
            return 0;
        }
    }
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }
    INITCOMMONCONTROLSEX controls = {.dwSize = sizeof(controls), .dwICC = ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    App app;
    if (!create_app(&app, &options)) {
        MessageBoxW(NULL, L"The native island could not be initialized.", L"DynamicWindows",
                    MB_ICONERROR);
        if (app.window)
            DestroyWindow(app.window);
        CoUninitialize();
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }
    int result = 0;
    if (options.smoke) {
        result = smoke_test(&app, options.smoke_dir);
        DestroyWindow(app.window);
    } else {
        MSG message;
        bool running = true;
        while (running) {
            DWORD wait = MsgWaitForMultipleObjectsEx(1, &app.frame_timer, INFINITE, QS_ALLINPUT,
                                                     MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                result = 1;
                break;
            }
            if (wait == WAIT_OBJECT_0 && app.initialized)
                frame(&app);
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    result = (int)message.wParam;
                    running = false;
                    break;
                }
                if (!settings_message(&app, &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }
    }
    CoUninitialize();
    if (mutex)
        CloseHandle(mutex);
    return result;
}
