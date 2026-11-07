#ifndef DYNAMICWINDOWS_PLATFORM_H
#define DYNAMICWINDOWS_PLATFORM_H

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

struct AudioMeter;

typedef struct {
    bool audio_available;
    float volume;
    float peak;
    bool muted;
    bool power_known;
    bool plugged_in;
    bool has_battery;
    bool charging;
    int battery_percent;
    wchar_t output_name[160];
} SystemSnapshot;

typedef struct {
    IMMDeviceEnumerator *enumerator;
    IMMDevice *device;
    IAudioEndpointVolume *volume;
    struct AudioMeter *meter;
    wchar_t *device_id;
    wchar_t output_name[160];
    uint64_t next_device_check;
} Platform;

bool platform_init(Platform *platform);
void platform_destroy(Platform *platform);
void platform_poll(Platform *platform, SystemSnapshot *snapshot, uint64_t now);
bool platform_set_volume(Platform *platform, double volume);
bool platform_toggle_mute(Platform *platform);
bool platform_fullscreen(HWND own_window, HMONITOR monitor);
bool platform_open_search(void);

#endif
