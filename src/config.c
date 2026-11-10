#include "config.h"
#include "island.h"

#include <math.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <windows.h>

#define INI_SECTION L"DynamicWindows"
#define CONFIG_VERSION 1
#define WIDE_VALUE(value) L##value
#define CONFIG_KEY(field) WIDE_VALUE(#field)

void config_defaults(AppConfig *config) {
    *config = (AppConfig){.idle = IDLE_BLANK,
                          .idle_font_size = 14,
                          .clock_24h = true,
                          .camera_dot = false,
                          .idle_width = 80,
                          .media_width = 232,
                          .timer_width = 196,
                          .compact_height = 28,
                          .media_height = 45,
                          .timer_height = 45,
                          .expanded_width = 408,
                          .expanded_height = 264,
                          .compact_radius = 14,
                          .expanded_radius = 30,
                          .bubble_size = 44,
                          .bubble_gap = 8,
                          .alert_width = 312,
                          .alert_height = 76,
                          .anchor = 1,
                          .y_offset = 16,
                          .opacity = 100,
                          .border_width = 0,
                          .shadow = 100,
                          .colors = {0x050506, 0xf5f5f7, 0xa6a6ad, 0x303035, 0x222226, 0x34343b,
                                     0x9dd8ff, 0xffb74d, 0x87dfab},
                          .response_ms = 420,
                          .damping = 78,
                          .frame_rate = 120,
                          .hover_delay = 350,
                          .leave_delay = 650,
                          .volume_alert_ms = 1800,
                          .power_alert_ms = 4000,
                          .wheel_step = 2,
                          .timer_seconds = 300,
                          .show_media = true,
                          .show_timer = true,
                          .dual_activity = true,
                          .volume_alerts = true,
                          .power_alerts = true,
                          .hover_expand = true,
                          .hide_fullscreen = true,
                          .close_on_blur = true,
                          .timer_sound = true,
                          .waveform = true,
                          .artwork = true,
                          .media_source = true,
                          .media_progress = true,
                          .search_button = true,
                          .settings_on_start = false,
                          .change_preview_ms = 3000,
                          .media_extra_width = 36,
                          .artwork_background = true,
                          .headphone_alerts = true,
                          .headphone_status = true,
                          .headphone_battery = true,
                          .headphone_alert_ms = 4000,
                          .notification_content = true,
                          .notification_alert_ms = 5000,
                          .crab_provider = CRAB_PROVIDER_NONE,
                          .crab_speak = true};
    config->crab_shortcut = (ShortcutBinding){VK_SPACE, MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_ISLAND] = (ShortcutBinding){'D', MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_TIMER] = (ShortcutBinding){'T', MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_HIDE] = (ShortcutBinding){'H', MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_SETTINGS] = (ShortcutBinding){'C', MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_SEARCH] = (ShortcutBinding){'F', MOD_CONTROL | MOD_ALT};
    config->shortcuts[SHORTCUT_CLOSE] = (ShortcutBinding){VK_ESCAPE, 0};
    config->shortcuts[SHORTCUT_MEDIA_VIEW] = (ShortcutBinding){'1', 0};
    config->shortcuts[SHORTCUT_TIMER_VIEW] = (ShortcutBinding){'2', 0};
    config->shortcuts[SHORTCUT_SYSTEM_VIEW] = (ShortcutBinding){'3', 0};
    config->shortcuts[SHORTCUT_SETTINGS_LOCAL] = (ShortcutBinding){VK_F2, 0};
    config->shortcuts[SHORTCUT_SETTINGS_ALT] = (ShortcutBinding){VK_OEM_COMMA, MOD_CONTROL};
    config->shortcuts[SHORTCUT_PLAY] = (ShortcutBinding){VK_SPACE, 0};
    config->shortcuts[SHORTCUT_LEFT] = (ShortcutBinding){VK_LEFT, 0};
    config->shortcuts[SHORTCUT_RIGHT] = (ShortcutBinding){VK_RIGHT, 0};
    config->shortcuts[SHORTCUT_START] = (ShortcutBinding){VK_HOME, 0};
    config->shortcuts[SHORTCUT_END] = (ShortcutBinding){VK_END, 0};
    wcscpy(config->idle_text, L"DynamicWindows");
    wcscpy(config->idle_font, L"Segoe UI");
    wcscpy(config->crab_name, L"Crab");
    wcscpy(config->crab_speech_model, L"speech\\ggml-tiny.bin");
}

static void trim_text(wchar_t *text) {
    wchar_t *start = text;
    while (iswspace(*start))
        ++start;
    size_t length = wcslen(start);
    while (length && iswspace(start[length - 1]))
        --length;
    if (start != text)
        memmove(text, start, length * sizeof(wchar_t));
    text[length] = L'\0';
}

static int integer_clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

void config_normalize(AppConfig *config) {
    config->idle = (IdleContent)integer_clamp((int)config->idle, IDLE_CLOCK, IDLE_HIDDEN);
    config->idle_text[127] = L'\0';
    config->idle_font[31] = L'\0';
    config->crab_provider = (CrabProvider)integer_clamp(
        (int)config->crab_provider, CRAB_PROVIDER_NONE, CRAB_PROVIDER_OPENAI_COMPATIBLE);
    config->crab_name[63] = L'\0';
    config->crab_base_url[511] = L'\0';
    config->crab_model[95] = L'\0';
    config->crab_speech_model[511] = L'\0';
    config->monitor[31] = L'\0';
    if (!*config->idle_font)
        wcscpy(config->idle_font, L"Segoe UI");
    trim_text(config->crab_name);
    trim_text(config->crab_base_url);
    trim_text(config->crab_model);
    trim_text(config->crab_speech_model);
    if (!*config->crab_name)
        wcscpy(config->crab_name, L"Crab");
    if (config->crab_provider == CRAB_PROVIDER_ANTHROPIC && !*config->crab_base_url)
        wcscpy(config->crab_base_url, L"https://api.anthropic.com");
    if (config->crab_provider != CRAB_PROVIDER_NONE && !*config->crab_base_url)
        config->crab_provider = CRAB_PROVIDER_NONE;

    for (int i = 0; i < 127 && config->idle_text[i]; ++i)
        if (config->idle_text[i] < L' ')
            config->idle_text[i] = L' ';
    for (int i = 0; i < 63 && config->crab_name[i]; ++i)
        if (config->crab_name[i] < L' ')
            config->crab_name[i] = L' ';
    for (int i = 0; i < 95 && config->crab_model[i]; ++i)
        if (config->crab_model[i] < L' ')
            config->crab_model[i] = L' ';
    config->idle_font_size = integer_clamp(config->idle_font_size, 8, 48);
    config->idle_width = island_clamp(config->idle_width, 80, 1200);
    config->media_width = island_clamp(config->media_width, 160, 1200);
    config->timer_width = island_clamp(config->timer_width, 160, 1200);
    config->compact_height = island_clamp(config->compact_height, 28, 160);
    config->media_height = island_clamp(config->media_height, 28, 160);
    config->timer_height = island_clamp(config->timer_height, 28, 160);
    config->expanded_width = island_clamp(config->expanded_width, 408, 1200);
    config->expanded_height = island_clamp(config->expanded_height, 264, 800);
    config->compact_radius = island_clamp(config->compact_radius, 0, config->compact_height / 2);
    config->expanded_radius = island_clamp(
        config->expanded_radius, 0, fmin(config->expanded_width, config->expanded_height) / 2);
    config->bubble_size = island_clamp(config->bubble_size, 28, 128);
    config->bubble_gap = island_clamp(config->bubble_gap, 0, 64);
    config->alert_width = island_clamp(config->alert_width, 280, 1200);
    config->alert_height = island_clamp(config->alert_height, 76, 240);
    config->anchor = integer_clamp(config->anchor, 0, 8);
    config->x_offset = integer_clamp(config->x_offset, -16000, 16000);
    config->y_offset = integer_clamp(config->y_offset, -16000, 16000);
    config->opacity = integer_clamp(config->opacity, 10, 100);
    config->border_width = island_clamp(config->border_width, 0, 6);
    config->shadow = integer_clamp(config->shadow, 0, 100);
    for (int i = 0; i < TINT_COUNT; ++i)
        config->colors[i] &= 0xffffff;
    config->response_ms = integer_clamp(config->response_ms, 150, 1800);
    config->damping = integer_clamp(config->damping, 50, 100);
    config->frame_rate = integer_clamp(config->frame_rate, 30, 144);
    config->hover_delay = integer_clamp(config->hover_delay, 0, 3000);
    config->leave_delay = integer_clamp(config->leave_delay, 0, 5000);
    config->volume_alert_ms = integer_clamp(config->volume_alert_ms, 250, 15000);
    config->power_alert_ms = integer_clamp(config->power_alert_ms, 250, 15000);
    config->wheel_step = integer_clamp(config->wheel_step, 1, 20);
    config->timer_seconds = integer_clamp(config->timer_seconds, 1, 86400);
    config->change_preview_ms = integer_clamp(config->change_preview_ms, 500, 15000);
    config->media_extra_width = integer_clamp(config->media_extra_width, 0, 200);
    config->headphone_alert_ms = integer_clamp(config->headphone_alert_ms, 250, 15000);
    config->notification_alert_ms = integer_clamp(config->notification_alert_ms, 500, 15000);
}

bool config_default_path(wchar_t *path, size_t count) {
    wchar_t directory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, directory)))
        return false;
    return swprintf(path, count, L"%ls\\DynamicWindows\\settings.ini", directory) > 0;
}

static double read_number(const wchar_t *path, const wchar_t *section, const wchar_t *name,
                          double fallback) {
    wchar_t value[80];
    if (!GetPrivateProfileStringW(section, name, L"", value, 80, path))
        return fallback;
    wchar_t *end = NULL;
    double number = wcstod(value, &end);
    return end != value && *end == L'\0' && isfinite(number) && number >= -2147483647.0 &&
                   number <= 2147483647.0
               ? number
               : fallback;
}

static bool write_number(const wchar_t *path, const wchar_t *name, double value) {
    wchar_t text[80];
    swprintf(text, 80, L"%.4f", value);
    return WritePrivateProfileStringW(INI_SECTION, name, text, path) != FALSE;
}

#define NUMBER_FIELDS(X)                                                                           \
    X(idle_font_size)                                                                              \
    X(clock_24h)                                                                                   \
    X(clock_seconds)                                                                               \
    X(camera_dot)                                                                                  \
    X(idle_width)                                                                                  \
    X(media_width)                                                                                 \
    X(timer_width)                                                                                 \
    X(compact_height)                                                                              \
    X(media_height)                                                                                \
    X(timer_height)                                                                                \
    X(expanded_width)                                                                              \
    X(expanded_height)                                                                             \
    X(compact_radius)                                                                              \
    X(expanded_radius)                                                                             \
    X(bubble_size)                                                                                 \
    X(bubble_gap)                                                                                  \
    X(alert_width)                                                                                 \
    X(alert_height)                                                                                \
    X(anchor)                                                                                      \
    X(x_offset)                                                                                    \
    X(y_offset)                                                                                    \
    X(opacity)                                                                                     \
    X(border_width)                                                                                \
    X(shadow)                                                                                      \
    X(response_ms)                                                                                 \
    X(damping)                                                                                     \
    X(frame_rate)                                                                                  \
    X(reduced_motion)                                                                              \
    X(hover_delay)                                                                                 \
    X(leave_delay)                                                                                 \
    X(volume_alert_ms)                                                                             \
    X(power_alert_ms)                                                                              \
    X(wheel_step)                                                                                  \
    X(timer_seconds)                                                                               \
    X(show_media)                                                                                  \
    X(show_timer)                                                                                  \
    X(dual_activity)                                                                               \
    X(volume_alerts)                                                                               \
    X(power_alerts)                                                                                \
    X(hover_expand)                                                                                \
    X(hide_fullscreen)                                                                             \
    X(close_on_blur)                                                                               \
    X(timer_sound)                                                                                 \
    X(waveform)                                                                                    \
    X(artwork)                                                                                     \
    X(media_source)                                                                                \
    X(media_progress)                                                                              \
    X(search_button)                                                                               \
    X(settings_on_start)                                                                           \
    X(change_preview_ms)                                                                           \
    X(media_extra_width)                                                                           \
    X(artwork_background)                                                                          \
    X(headphone_alerts)                                                                            \
    X(headphone_status)                                                                            \
    X(headphone_battery)                                                                           \
    X(headphone_alert_ms)                                                                          \
    X(notifications_enabled)                                                                       \
    X(notification_content)                                                                        \
    X(notification_alert_ms)                                                                       \
    X(crab_enabled)                                                                                \
    X(crab_speak)                                                                                  \
    X(crab_provider)

typedef struct {
    const wchar_t *name;
    unsigned key;
} ShortcutKey;

static const ShortcutKey shortcut_keys[] = {{L"Escape", VK_ESCAPE},   {L"Space", VK_SPACE},
                                            {L"Left", VK_LEFT},       {L"Right", VK_RIGHT},
                                            {L"Up", VK_UP},           {L"Down", VK_DOWN},
                                            {L"Home", VK_HOME},       {L"End", VK_END},
                                            {L"Insert", VK_INSERT},   {L"Delete", VK_DELETE},
                                            {L"PageUp", VK_PRIOR},    {L"PageDown", VK_NEXT},
                                            {L"Comma", VK_OEM_COMMA}, {L"Period", VK_OEM_PERIOD},
                                            {L"Minus", VK_OEM_MINUS}, {L"Backspace", VK_BACK}};

bool config_shortcut_parse(const wchar_t *text, ShortcutBinding *binding) {
    if (!text || !binding || wcslen(text) >= 128)
        return false;
    if (!*text || _wcsicmp(text, L"None") == 0) {
        *binding = (ShortcutBinding){0};
        return true;
    }
    wchar_t value[128];
    wcscpy(value, text);
    ShortcutBinding result = {0};
    wchar_t *part = value;
    while (part && *part) {
        wchar_t *next = wcschr(part, L'+');
        if (next)
            *next++ = L'\0';
        while (*part == L' ')
            ++part;
        size_t length = wcslen(part);
        while (length && part[length - 1] == L' ')
            part[--length] = L'\0';
        unsigned modifier = _wcsicmp(part, L"Ctrl") == 0    ? MOD_CONTROL
                            : _wcsicmp(part, L"Alt") == 0   ? MOD_ALT
                            : _wcsicmp(part, L"Shift") == 0 ? MOD_SHIFT
                            : _wcsicmp(part, L"Win") == 0   ? MOD_WIN
                                                            : 0;
        if (modifier) {
            if (result.key || (result.modifiers & modifier))
                return false;
            result.modifiers |= modifier;
        } else {
            if (result.key || !length || next)
                return false;
            if (length == 1 &&
                ((part[0] >= L'A' && part[0] <= L'Z') || (part[0] >= L'a' && part[0] <= L'z') ||
                 (part[0] >= L'0' && part[0] <= L'9')))
                result.key = (unsigned)towupper(part[0]);
            else if ((part[0] == L'F' || part[0] == L'f') && length <= 3) {
                wchar_t *end = NULL;
                long number = wcstol(part + 1, &end, 10);
                if (*end == L'\0' && number >= 1 && number <= 24)
                    result.key = VK_F1 + (unsigned)number - 1;
            } else {
                for (size_t i = 0; i < sizeof(shortcut_keys) / sizeof(shortcut_keys[0]); ++i)
                    if (_wcsicmp(part, shortcut_keys[i].name) == 0) {
                        result.key = shortcut_keys[i].key;
                        break;
                    }
            }
            if (!result.key)
                return false;
        }
        part = next;
    }
    if (!result.key)
        return false;
    *binding = result;
    return true;
}

void config_shortcut_text(ShortcutBinding binding, wchar_t *text, size_t count) {
    if (!binding.key) {
        swprintf(text, count, L"None");
        return;
    }
    wchar_t key[32] = {0};
    if (binding.key >= VK_F1 && binding.key <= VK_F24)
        swprintf(key, 32, L"F%u", binding.key - VK_F1 + 1);
    else if ((binding.key >= 'A' && binding.key <= 'Z') ||
             (binding.key >= '0' && binding.key <= '9'))
        swprintf(key, 32, L"%lc", (wint_t)binding.key);
    else
        for (size_t i = 0; i < sizeof(shortcut_keys) / sizeof(shortcut_keys[0]); ++i)
            if (shortcut_keys[i].key == binding.key) {
                wcscpy(key, shortcut_keys[i].name);
                break;
            }
    if (!*key)
        wcscpy(key, L"None");
    swprintf(text, count, L"%ls%ls%ls%ls%ls", binding.modifiers & MOD_CONTROL ? L"Ctrl+" : L"",
             binding.modifiers & MOD_ALT ? L"Alt+" : L"",
             binding.modifiers & MOD_SHIFT ? L"Shift+" : L"",
             binding.modifiers & MOD_WIN ? L"Win+" : L"", key);
}

bool config_shortcuts_valid(const AppConfig *config) {
    for (int i = 0; i < SHORTCUT_COUNT; ++i) {
        ShortcutBinding binding = config->shortcuts[i];
        if (binding.modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN))
            return false;
        if (!binding.key)
            continue;
        wchar_t text[128];
        config_shortcut_text(binding, text, 128);
        ShortcutBinding parsed;
        if (!config_shortcut_parse(text, &parsed) || parsed.key != binding.key)
            return false;
        for (int j = i + 1; j < SHORTCUT_COUNT; ++j) {
            if ((i < SHORTCUT_GLOBAL_COUNT) != (j < SHORTCUT_GLOBAL_COUNT))
                continue;
            if (binding.key == config->shortcuts[j].key &&
                binding.modifiers == config->shortcuts[j].modifiers)
                return false;
        }
        if (i < SHORTCUT_GLOBAL_COUNT && binding.key == config->crab_shortcut.key &&
            binding.modifiers == config->crab_shortcut.modifiers)
            return false;
    }
    if (config->crab_shortcut.modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN))
        return false;
    if (config->crab_shortcut.key) {
        wchar_t text[128];
        ShortcutBinding parsed;
        config_shortcut_text(config->crab_shortcut, text, 128);
        if (!config_shortcut_parse(text, &parsed) || parsed.key != config->crab_shortcut.key)
            return false;
    }
    return true;
}

bool config_load(AppConfig *config, const wchar_t *path) {
    config_defaults(config);
    if (!path || !*path)
        return false;
    wchar_t legacy[MAX_PATH];
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        wchar_t default_path[MAX_PATH];
        wchar_t directory[MAX_PATH];
        if (!config_default_path(default_path, MAX_PATH) || _wcsicmp(path, default_path) != 0 ||
            FAILED(
                SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, directory)))
            return false;
        swprintf(legacy, MAX_PATH, L"%ls\\MacNotch\\settings.ini", directory);
        if (GetFileAttributesW(legacy) == INVALID_FILE_ATTRIBUTES)
            return false;
        path = legacy;
    }
    wchar_t version[32];
    const wchar_t *section =
        GetPrivateProfileStringW(INI_SECTION, L"version", L"", version, 32, path) ? INI_SECTION
                                                                                  : L"MacNotch";
    wchar_t provider_value[32];
    bool crab_provider_present =
        GetPrivateProfileStringW(section, L"crab_provider", L"", provider_value, 32, path) != 0;
    config->idle = (IdleContent)read_number(path, section, L"idle", config->idle);
#define LOAD(field) config->field = read_number(path, section, CONFIG_KEY(field), config->field);
    NUMBER_FIELDS(LOAD)
#undef LOAD
    config->media_height = read_number(path, section, L"media_height", config->compact_height);
    config->timer_height = read_number(path, section, L"timer_height", config->compact_height);
    GetPrivateProfileStringW(section, L"idle_text", config->idle_text, config->idle_text, 128,
                             path);
    GetPrivateProfileStringW(section, L"idle_font", config->idle_font, config->idle_font, 32, path);
    GetPrivateProfileStringW(section, L"monitor", L"", config->monitor, 32, path);
    GetPrivateProfileStringW(section, L"crab_name", config->crab_name, config->crab_name, 64, path);
    GetPrivateProfileStringW(section, L"crab_base_url", config->crab_base_url,
                             config->crab_base_url, 512, path);
    GetPrivateProfileStringW(section, L"crab_model", config->crab_model, config->crab_model, 96,
                             path);
    GetPrivateProfileStringW(section, L"crab_speech_model", config->crab_speech_model,
                             config->crab_speech_model, 512, path);
    if (!crab_provider_present && *config->crab_model) {
        config->crab_provider = CRAB_PROVIDER_ANTHROPIC;
        if (!*config->crab_base_url)
            wcscpy(config->crab_base_url, L"https://api.anthropic.com");
    }
    for (int i = 0; i < TINT_COUNT; ++i) {
        wchar_t key[32];
        wchar_t value[32];
        swprintf(key, 32, L"color_%d", i);
        if (GetPrivateProfileStringW(section, key, L"", value, 32, path)) {
            wchar_t *end;
            unsigned long color = wcstoul(value, &end, 16);
            if (end != value && *end == L'\0' && color <= 0xffffff)
                config->colors[i] = (uint32_t)color;
        }
    }
    for (int i = 0; i < SHORTCUT_COUNT; ++i) {
        wchar_t key[32];
        wchar_t value[128];
        swprintf(key, 32, L"shortcut_%d", i);
        if (GetPrivateProfileStringW(section, key, L"", value, 128, path)) {
            ShortcutBinding binding;
            if (config_shortcut_parse(value, &binding))
                config->shortcuts[i] = binding;
        }
    }
    wchar_t crab_shortcut[128];
    if (GetPrivateProfileStringW(section, L"crab_shortcut", L"", crab_shortcut, 128, path)) {
        ShortcutBinding binding;
        if (config_shortcut_parse(crab_shortcut, &binding))
            config->crab_shortcut = binding;
    }
    if (!config_shortcuts_valid(config)) {
        AppConfig defaults;
        config_defaults(&defaults);
        if (config->crab_shortcut.key) {
            for (int i = 0; i < SHORTCUT_GLOBAL_COUNT; ++i)
                if (config->shortcuts[i].key == config->crab_shortcut.key &&
                    config->shortcuts[i].modifiers == config->crab_shortcut.modifiers)
                    config->crab_shortcut = (ShortcutBinding){0};
        }
        if (!config_shortcuts_valid(config)) {
            memcpy(config->shortcuts, defaults.shortcuts, sizeof(config->shortcuts));
            config->crab_shortcut = defaults.crab_shortcut;
        }
    }
    config_normalize(config);
    return true;
}

bool config_save(const AppConfig *input, const wchar_t *path) {
    if (!path || !*path || wcslen(path) > 950)
        return false;
    AppConfig config = *input;
    config_normalize(&config);
    wchar_t parent[1024];
    wcscpy(parent, path);
    wchar_t *separator = wcsrchr(parent, L'\\');
    wchar_t *forward = wcsrchr(parent, L'/');
    if (forward && (!separator || forward > separator))
        separator = forward;
    if (separator) {
        *separator = L'\0';
        int result = SHCreateDirectoryExW(NULL, parent, NULL);
        if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS &&
            result != ERROR_FILE_EXISTS)
            return false;
    }
    wchar_t temporary[1024];
    swprintf(temporary, 1024, L"%ls.%lu.tmp", path, GetCurrentProcessId());
    HANDLE file =
        CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    WORD marker = 0xfeff;
    DWORD written = 0;
    bool saved =
        WriteFile(file, &marker, sizeof(marker), &written, NULL) && written == sizeof(marker);
    CloseHandle(file);
    saved = write_number(temporary, L"version", CONFIG_VERSION) && saved;
    saved = write_number(temporary, L"idle", config.idle) && saved;
#define SAVE(field) saved = write_number(temporary, CONFIG_KEY(field), config.field) && saved;
    NUMBER_FIELDS(SAVE)
#undef SAVE
    saved =
        WritePrivateProfileStringW(INI_SECTION, L"idle_text", config.idle_text, temporary) && saved;
    saved =
        WritePrivateProfileStringW(INI_SECTION, L"idle_font", config.idle_font, temporary) && saved;
    saved = WritePrivateProfileStringW(INI_SECTION, L"monitor", config.monitor, temporary) && saved;
    saved =
        WritePrivateProfileStringW(INI_SECTION, L"crab_name", config.crab_name, temporary) && saved;
    saved = WritePrivateProfileStringW(INI_SECTION, L"crab_base_url", config.crab_base_url,
                                       temporary) &&
            saved;
    saved = WritePrivateProfileStringW(INI_SECTION, L"crab_model", config.crab_model, temporary) &&
            saved;
    saved = WritePrivateProfileStringW(INI_SECTION, L"crab_speech_model", config.crab_speech_model,
                                       temporary) &&
            saved;
    for (int i = 0; i < TINT_COUNT; ++i) {
        wchar_t key[32];
        wchar_t value[32];
        swprintf(key, 32, L"color_%d", i);
        swprintf(value, 32, L"%06lx", (unsigned long)config.colors[i]);
        saved = WritePrivateProfileStringW(INI_SECTION, key, value, temporary) && saved;
    }
    for (int i = 0; i < SHORTCUT_COUNT; ++i) {
        wchar_t key[32];
        wchar_t value[128];
        swprintf(key, 32, L"shortcut_%d", i);
        config_shortcut_text(config.shortcuts[i], value, 128);
        saved = WritePrivateProfileStringW(INI_SECTION, key, value, temporary) && saved;
    }
    wchar_t crab_shortcut[128];
    config_shortcut_text(config.crab_shortcut, crab_shortcut, 128);
    saved = WritePrivateProfileStringW(INI_SECTION, L"crab_shortcut", crab_shortcut, temporary) &&
            saved;
    (void)WritePrivateProfileStringW(NULL, NULL, NULL, temporary);
    if (saved)
        saved = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
                FALSE;
    if (!saved)
        DeleteFileW(temporary);
    return saved;
}

double config_reference_width(const AppConfig *config) {
    double media = fmax(config->media_width, config->idle_width + config->media_extra_width);
    double compact = fmax(config->idle_width, fmax(media, config->timer_width));
    double side = config->bubble_gap + config->bubble_size;
    double headphones = config->headphone_status ? config->bubble_gap + fmax(config->compact_height,
                                                                             config->timer_height)
                                                 : 0;
    return fmax(config->expanded_width, fmax(compact + side + headphones, config->alert_width));
}

void config_canvas(const AppConfig *config, double *width, double *height) {
    *width = fmax(512, ceil(config_reference_width(config) * 1.12 + 40));
    *height = fmax(316, ceil(fmax(config->expanded_height,
                                  fmax(config->compact_height, config->alert_height)) *
                                 1.12 +
                             20));
}

ConfigRect config_place(const AppConfig *config, ConfigRect work, double scale, double canvas_width,
                        double canvas_height) {
    int horizontal = config->anchor % 3;
    int vertical = config->anchor / 3;
    double reference_width = config_reference_width(config);
    double left_margin = (canvas_width - reference_width) / 2;
    double top_margin = 16;
    double x = horizontal == 0   ? work.x - left_margin * scale
               : horizontal == 1 ? work.x + (work.width - canvas_width * scale) / 2
                                 : work.x + work.width - (left_margin + reference_width) * scale;
    double y =
        vertical == 0 ? work.y - top_margin * scale
        : vertical == 1
            ? work.y + (work.height - config->expanded_height * scale) / 2 - top_margin * scale
            : work.y + work.height - (config->expanded_height + top_margin) * scale;
    x += config->x_offset * scale;
    y += config->y_offset * scale;
    x = island_clamp(x, work.x + 8 - left_margin * scale,
                     work.x + work.width - 8 - (left_margin + reference_width) * scale);
    y = island_clamp(y, work.y - top_margin * scale,
                     work.y + work.height - (config->expanded_height + top_margin) * scale);
    return (ConfigRect){x, y, canvas_width * scale, canvas_height * scale};
}
