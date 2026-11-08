#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static unsigned checks;

#define CHECK(value)                                                                               \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(value)) {                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void test_defaults(void) {
    AppConfig config;
    config_defaults(&config);
    CHECK(config.idle == IDLE_BLANK);
    CHECK(config.expanded_width == 408);
    CHECK(config.compact_height == 28);
    CHECK(!config.settings_on_start);
    CHECK(config.response_ms == 420);
    CHECK(config.frame_rate == 120);
    CHECK(config.colors[TINT_BACKGROUND] == 0x050506);
    CHECK(config.headphone_alerts);
    CHECK(config.headphone_status);
    CHECK(config.headphone_battery);
    CHECK(config.notification_content);
    CHECK(!config.notifications_enabled);
    CHECK(config.notification_alert_ms == 5000);
    CHECK(!config.crab_enabled);
    CHECK(config.crab_speak);
    CHECK(wcscmp(config.crab_name, L"Crab") == 0);
    CHECK(wcscmp(config.crab_model, L"claude-opus-5-5") == 0);
    CHECK(config_shortcuts_valid(&config));
    config.crab_shortcut = config.shortcuts[SHORTCUT_SEARCH];
    CHECK(!config_shortcuts_valid(&config));
    config.crab_shortcut = (ShortcutBinding){VK_SPACE, MOD_CONTROL | MOD_ALT};

    CHECK(wcscmp(config.idle_font, L"Segoe UI") == 0);
    double width;
    double height;
    config_canvas(&config, &width, &height);
    CHECK(width == 512);
    CHECK(height == 316);
}

static void test_limits(void) {
    AppConfig config;
    config_defaults(&config);
    config.idle = (IdleContent)99;
    config.idle_width = NAN;
    config.media_width = -1;
    config.compact_height = 5000;
    config.expanded_width = 10;
    config.expanded_height = INFINITY;
    config.compact_radius = 600;
    config.expanded_radius = -1;
    config.x_offset = 32000;
    config.y_offset = -32000;
    config.opacity = 0;
    config.response_ms = 0;
    config.damping = 0;
    config.frame_rate = 1000;
    config.timer_seconds = 100000;
    config.notification_alert_ms = 99999;
    config.colors[TINT_ACCENT] = 0xffffffff;
    config_normalize(&config);
    CHECK(config.idle == IDLE_HIDDEN);
    CHECK(config.idle_width == 80);
    CHECK(config.media_width == 160);
    CHECK(config.compact_height == 160);
    CHECK(config.compact_radius == 80);
    CHECK(config.expanded_width == 408);
    CHECK(config.expanded_height == 800);
    CHECK(config.expanded_radius == 0);
    CHECK(config.x_offset == 16000);
    CHECK(config.y_offset == -16000);
    CHECK(config.opacity == 10);
    CHECK(config.response_ms == 150);
    CHECK(config.damping == 50);
    CHECK(config.frame_rate == 144);
    CHECK(config.timer_seconds == 86400);
    CHECK(config.notification_alert_ms == 15000);
    CHECK(config.colors[TINT_ACCENT] == 0xffffff);
}

static void test_placement(void) {
    AppConfig config;
    config_defaults(&config);
    double width;
    double height;
    config_canvas(&config, &width, &height);
    ConfigRect work = {-1920, 40, 1920, 1040};
    ConfigRect rect = config_place(&config, work, 1.5, width, height);
    CHECK(rect.x == -1344);
    CHECK(rect.y == 40);
    CHECK(rect.width == 768);
    CHECK(rect.height == 474);
    config.anchor = 8;
    config.x_offset = 16000;
    config.y_offset = 16000;
    rect = config_place(&config, work, 1.5, width, height);
    double margin = (width - config_reference_width(&config)) / 2;
    CHECK(rect.x + (margin + config_reference_width(&config)) * 1.5 <= work.x + work.width);
    CHECK(rect.y + (16 + config.expanded_height) * 1.5 <= work.y + work.height);
    config.anchor = 0;
    config.x_offset = -16000;
    config.y_offset = -16000;
    rect = config_place(&config, work, 1.5, width, height);
    CHECK(rect.x + margin * 1.5 >= work.x);
    CHECK(rect.y + 16 * 1.5 >= work.y);
    config.media_width = 1000;
    config.media_extra_width = 73;
    config.timer_width = 1000;
    config_canvas(&config, &width, &height);
    CHECK(width > 1200);
    CHECK(config_reference_width(&config) == 1113);
    config.timer_height = 120;
    CHECK(config_reference_width(&config) == 1160);
}

static void test_persistence(void) {
    wchar_t directory[MAX_PATH];
    wchar_t path[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, directory) != 0);
    CHECK(GetTempFileNameW(directory, L"dwc", 0, path) != 0);
    AppConfig saved;
    config_defaults(&saved);
    saved.idle = IDLE_TEXT;
    wcscpy(saved.idle_text, L"Custom island \x00e7 \x03a9");
    wcscpy(saved.idle_font, L"Consolas");
    wcscpy(saved.monitor, L"\\\\.\\DISPLAY2");
    saved.idle_width = 327.5;
    saved.compact_height = 67;
    saved.response_ms = 630;
    saved.settings_on_start = false;
    saved.hide_fullscreen = false;
    saved.media_source = false;
    saved.headphone_alerts = false;
    saved.headphone_status = false;
    saved.headphone_battery = false;
    saved.headphone_alert_ms = 7500;
    saved.notification_content = false;
    saved.notifications_enabled = true;
    saved.notification_alert_ms = 6400;
    wcscpy(saved.crab_name, L"Siri");
    wcscpy(saved.crab_model, L"claude-opus-5-5");
    saved.crab_enabled = true;
    saved.crab_speak = false;
    saved.crab_shortcut = (ShortcutBinding){'K', MOD_CONTROL | MOD_ALT};
    saved.colors[TINT_ACCENT] = 0x123abc;
    CHECK(config_save(&saved, path));
    AppConfig loaded;
    CHECK(config_load(&loaded, path));
    CHECK(loaded.idle == saved.idle);
    CHECK(wcscmp(loaded.idle_text, saved.idle_text) == 0);
    CHECK(wcscmp(loaded.idle_font, saved.idle_font) == 0);
    CHECK(wcscmp(loaded.monitor, saved.monitor) == 0);
    CHECK(loaded.idle_width == saved.idle_width);
    CHECK(loaded.compact_height == saved.compact_height);
    CHECK(loaded.response_ms == saved.response_ms);
    CHECK(!loaded.notification_content);
    CHECK(loaded.notifications_enabled);
    CHECK(loaded.notification_alert_ms == 6400);
    CHECK(loaded.crab_enabled);
    CHECK(!loaded.crab_speak);
    CHECK(wcscmp(loaded.crab_name, L"Siri") == 0);
    CHECK(wcscmp(loaded.crab_model, L"claude-opus-5-5") == 0);
    CHECK(loaded.crab_shortcut.key == 'K');
    CHECK(loaded.crab_shortcut.modifiers == (MOD_CONTROL | MOD_ALT));
    CHECK(loaded.colors[TINT_ACCENT] == saved.colors[TINT_ACCENT]);
    CHECK(!loaded.settings_on_start);
    CHECK(!loaded.hide_fullscreen);
    CHECK(!loaded.media_source);
    CHECK(!loaded.headphone_alerts);
    CHECK(!loaded.headphone_status);
    CHECK(!loaded.headphone_battery);
    CHECK(loaded.headphone_alert_ms == 7500);
    CHECK(WritePrivateProfileStringW(L"DynamicWindows", L"idle_width", L"nan", path));
    CHECK(WritePrivateProfileStringW(L"DynamicWindows", L"frame_rate", L"99999999999999999", path));
    CHECK(WritePrivateProfileStringW(L"DynamicWindows", L"opacity", L"garbage", path));
    CHECK(WritePrivateProfileStringW(L"DynamicWindows", L"color_6", L"bad-value", path));
    CHECK(config_load(&loaded, path));
    CHECK(loaded.idle_width == 80);
    CHECK(loaded.frame_rate == 120);
    CHECK(loaded.opacity == 100);
    CHECK(loaded.colors[TINT_ACCENT] == 0x9dd8ff);
    CHECK(DeleteFileW(path));
    CHECK(!config_load(&loaded, path));
    CHECK(loaded.idle == IDLE_BLANK);
    CHECK(!config_save(&saved, L""));
    CHECK(GetTempFileNameW(directory, L"dwc", 0, path) != 0);
    CHECK(config_load(&loaded, path));
    CHECK(wcscmp(loaded.idle_text, L"DynamicWindows") == 0);
    CHECK(wcscmp(loaded.idle_font, L"Segoe UI") == 0);
    CHECK(DeleteFileW(path));
}

int main(void) {
    test_defaults();
    test_limits();
    test_placement();
    test_persistence();
    printf("%u configuration checks passed\n", checks);
    return 0;
}
