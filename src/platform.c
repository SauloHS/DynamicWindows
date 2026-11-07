#include "platform.h"
#include "island.h"

#include <shellapi.h>
#include <wchar.h>

struct AudioMeter;
typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(struct AudioMeter *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(struct AudioMeter *);
    ULONG(STDMETHODCALLTYPE *Release)(struct AudioMeter *);
    HRESULT(STDMETHODCALLTYPE *GetPeakValue)(struct AudioMeter *, float *);
    HRESULT(STDMETHODCALLTYPE *GetMeteringChannelCount)(struct AudioMeter *, UINT *);
    HRESULT(STDMETHODCALLTYPE *GetChannelsPeakValues)(struct AudioMeter *, UINT, float *);
    HRESULT(STDMETHODCALLTYPE *QueryHardwareSupport)(struct AudioMeter *, DWORD *);
} AudioMeterVtbl;

typedef struct AudioMeter {
    const AudioMeterVtbl *lpVtbl;
} AudioMeter;

static const GUID enumerator_clsid = {
    0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID enumerator_iid = {
    0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const GUID volume_iid = {
    0x5cdf2c82, 0x841e, 0x4546, {0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a}};
static const GUID meter_iid = {
    0xc02216f6, 0x8c67, 0x4b5b, {0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64}};
static const PROPERTYKEY friendly_name_key = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

static void release_device(Platform *platform) {
    if (platform->meter)
        platform->meter->lpVtbl->Release(platform->meter);
    if (platform->volume)
        IAudioEndpointVolume_Release(platform->volume);
    if (platform->device)
        IMMDevice_Release(platform->device);
    CoTaskMemFree(platform->device_id);
    platform->meter = NULL;
    platform->volume = NULL;
    platform->device = NULL;
    platform->device_id = NULL;
    platform->output_name[0] = L'\0';
}

bool platform_init(Platform *platform) {
    *platform = (Platform){0};
    return SUCCEEDED(CoCreateInstance(&enumerator_clsid, NULL, CLSCTX_INPROC_SERVER,
                                      &enumerator_iid, (void **)&platform->enumerator));
}

void platform_destroy(Platform *platform) {
    release_device(platform);
    if (platform->enumerator)
        IMMDeviceEnumerator_Release(platform->enumerator);
    platform->enumerator = NULL;
}

static void refresh_device(Platform *platform) {
    if (!platform->enumerator)
        return;
    IMMDevice *device = NULL;
    wchar_t *id = NULL;
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(platform->enumerator, eRender,
                                                           eMultimedia, &device))) {
        release_device(platform);
        return;
    }
    if (FAILED(IMMDevice_GetId(device, &id))) {
        IMMDevice_Release(device);
        release_device(platform);
        return;
    }
    if (platform->device_id && platform->volume && wcscmp(platform->device_id, id) == 0) {
        CoTaskMemFree(id);
        IMMDevice_Release(device);
        return;
    }
    release_device(platform);
    platform->device = device;
    platform->device_id = id;
    (void)IMMDevice_Activate(device, &volume_iid, CLSCTX_INPROC_SERVER, NULL,
                             (void **)&platform->volume);
    (void)IMMDevice_Activate(device, &meter_iid, CLSCTX_INPROC_SERVER, NULL,
                             (void **)&platform->meter);
    wcscpy(platform->output_name, L"Default Windows output");
    IPropertyStore *store = NULL;
    if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store))) {
        PROPVARIANT value = {0};
        if (SUCCEEDED(IPropertyStore_GetValue(store, &friendly_name_key, &value)) &&
            value.vt == VT_LPWSTR && value.pwszVal) {
            wcsncpy(platform->output_name, value.pwszVal, 159);
            platform->output_name[159] = L'\0';
        }
        PropVariantClear(&value);
        IPropertyStore_Release(store);
    }
}

void platform_poll(Platform *platform, SystemSnapshot *snapshot, uint64_t now) {
    *snapshot = (SystemSnapshot){.battery_percent = -1};
    if (now >= platform->next_device_check) {
        refresh_device(platform);
        platform->next_device_check = now + 2000;
    }
    if (platform->volume) {
        float volume = 0;
        BOOL muted = FALSE;
        HRESULT volume_result =
            IAudioEndpointVolume_GetMasterVolumeLevelScalar(platform->volume, &volume);
        HRESULT mute_result = IAudioEndpointVolume_GetMute(platform->volume, &muted);
        if (SUCCEEDED(volume_result) && SUCCEEDED(mute_result)) {
            snapshot->audio_available = true;
            snapshot->volume = (float)island_clamp(volume, 0, 1);
            snapshot->muted = muted != FALSE;
            wcsncpy(snapshot->output_name, platform->output_name, 159);
            if (platform->meter)
                (void)platform->meter->lpVtbl->GetPeakValue(platform->meter, &snapshot->peak);
            snapshot->peak = (float)island_clamp(snapshot->peak, 0, 1);
        } else {
            release_device(platform);
            platform->next_device_check = now;
        }
    }
    SYSTEM_POWER_STATUS power = {0};
    if (!GetSystemPowerStatus(&power))
        return;
    snapshot->power_known = power.ACLineStatus != 255;
    snapshot->plugged_in = power.ACLineStatus == 1;
    snapshot->has_battery = power.BatteryFlag != 255 && (power.BatteryFlag & 128) == 0;
    snapshot->charging = snapshot->has_battery && (power.BatteryFlag & 8) != 0;
    if (snapshot->has_battery && power.BatteryLifePercent <= 100)
        snapshot->battery_percent = power.BatteryLifePercent;
}

bool platform_set_volume(Platform *platform, double volume) {
    if (!platform->volume)
        return false;
    return SUCCEEDED(IAudioEndpointVolume_SetMasterVolumeLevelScalar(
        platform->volume, (float)island_clamp(volume, 0, 1), NULL));
}

bool platform_toggle_mute(Platform *platform) {
    if (!platform->volume)
        return false;
    BOOL muted = FALSE;
    if (FAILED(IAudioEndpointVolume_GetMute(platform->volume, &muted)))
        return false;
    return SUCCEEDED(IAudioEndpointVolume_SetMute(platform->volume, !muted, NULL));
}

bool platform_fullscreen(HWND own_window, HMONITOR monitor) {
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == own_window || IsIconic(foreground))
        return false;
    wchar_t class_name[64] = {0};
    GetClassNameW(foreground, class_name, 64);
    if (wcscmp(class_name, L"Progman") == 0 || wcscmp(class_name, L"WorkerW") == 0 ||
        wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
        wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0)
        return false;
    if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST) != monitor)
        return false;
    MONITORINFO info = {.cbSize = sizeof(info)};
    RECT rect;
    if (!GetMonitorInfoW(monitor, &info) || !GetWindowRect(foreground, &rect))
        return false;
    return rect.left <= info.rcMonitor.left + 1 && rect.top <= info.rcMonitor.top + 1 &&
           rect.right >= info.rcMonitor.right - 1 && rect.bottom >= info.rcMonitor.bottom - 1;
}

bool platform_open_search(void) {
    INPUT keys[4] = {0};
    for (int i = 0; i < 4; ++i)
        keys[i].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_LWIN;
    keys[1].ki.wVk = 'S';
    keys[2].ki.wVk = 'S';
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3].ki.wVk = VK_LWIN;
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(4, keys, sizeof(INPUT)) == 4;
}
