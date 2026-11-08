#include "headphones.h"

#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const GUID enumerator_clsid = {
    0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID enumerator_iid = {
    0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const PROPERTYKEY name_key = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
static const PROPERTYKEY container_key = {
    {0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
static const PROPERTYKEY form_key = {
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};
static const DEVPROPKEY node_container_key = {
    {0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
static const DEVPROPKEY battery_key = {
    {0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};

int headphones_battery(unsigned long type, const void *value, unsigned long size) {
    if (!value)
        return -1;
    ULONG level = 0;
    if (type == DEVPROP_TYPE_BYTE && size == sizeof(BYTE))
        level = *(const BYTE *)value;
    else if (type == DEVPROP_TYPE_UINT16 && size == sizeof(USHORT)) {
        USHORT small;
        memcpy(&small, value, sizeof(small));
        level = small;
    } else if (type == DEVPROP_TYPE_UINT32 && size == sizeof(ULONG))
        memcpy(&level, value, sizeof(level));
    else
        return -1;
    return level <= 100 ? (int)level : -1;
}

bool headphones_same(const HeadphoneDevice *first, const HeadphoneDevice *second) {
    if (first->bluetooth && second->bluetooth && first->has_container && second->has_container)
        return IsEqualGUID(&first->container, &second->container) != FALSE;
    return *first->id && *second->id && _wcsicmp(first->id, second->id) == 0;
}

const HeadphoneDevice *headphones_select(const HeadphoneSnapshot *snapshot,
                                         const wchar_t *output_id) {
    const HeadphoneDevice *selected = NULL;
    for (unsigned i = 0; i < snapshot->count && i < HEADPHONES_LIMIT; ++i) {
        const HeadphoneDevice *device = &snapshot->devices[i];
        if (output_id && *output_id && _wcsicmp(device->id, output_id) == 0)
            return device;
        if (!selected || (device->bluetooth && !selected->bluetooth) ||
            (device->bluetooth && device->battery_percent >= 0 && selected->battery_percent < 0))
            selected = device;
    }
    return selected;
}

double headphones_progress(int battery_percent) {
    return battery_percent >= 0 && battery_percent <= 100 ? battery_percent / 100.0 : -1;
}

static bool find_device(const HeadphoneSnapshot *snapshot, const HeadphoneDevice *device,
                        HeadphoneDevice *match) {
    for (unsigned i = 0; i < snapshot->count && i < HEADPHONES_LIMIT; ++i) {
        if (!headphones_same(&snapshot->devices[i], device))
            continue;
        if (match)
            *match = snapshot->devices[i];
        return true;
    }
    return false;
}

unsigned headphones_changes(const HeadphoneSnapshot *previous, const HeadphoneSnapshot *current,
                            HeadphoneEvent *events, unsigned capacity) {
    if (!previous->initialized || !current->initialized || FAILED(previous->error) ||
        FAILED(current->error) || !events)
        return 0;
    unsigned count = 0;
    for (unsigned i = 0; i < current->count && i < HEADPHONES_LIMIT && count < capacity; ++i) {
        HeadphoneDevice old;
        const HeadphoneDevice *device = &current->devices[i];
        if (!find_device(previous, device, &old))
            events[count++] = (HeadphoneEvent){HEADPHONE_CONNECTED, *device};
        else if (device->bluetooth && device->battery_percent >= 0 &&
                 device->battery_percent != old.battery_percent)
            events[count++] = (HeadphoneEvent){HEADPHONE_BATTERY, *device};
    }
    for (unsigned i = 0; i < previous->count && i < HEADPHONES_LIMIT && count < capacity; ++i)
        if (!find_device(current, &previous->devices[i], NULL))
            events[count++] = (HeadphoneEvent){HEADPHONE_DISCONNECTED, previous->devices[i]};
    return count;
}

void headphones_detail(const HeadphoneDevice *device, bool show_battery, wchar_t *text,
                       size_t count) {
    if (device->bluetooth && show_battery && device->battery_percent >= 0)
        swprintf(text, count, L"Bluetooth · %d%% battery", device->battery_percent);
    else
        swprintf(text, count, L"%ls",
                 device->bluetooth
                     ? (show_battery ? L"Bluetooth · Battery unavailable" : L"Bluetooth audio")
                     : L"Wired / USB audio");
}

static bool endpoint_read(IMMDevice *endpoint, HeadphoneDevice *device) {
    *device = (HeadphoneDevice){.battery_percent = -1};
    wchar_t *id = NULL;
    IPropertyStore *store = NULL;
    if (FAILED(IMMDevice_GetId(endpoint, &id)))
        return false;
    wcsncpy(device->id, id, 259);
    CoTaskMemFree(id);
    if (FAILED(IMMDevice_OpenPropertyStore(endpoint, STGM_READ, &store)))
        return false;
    PROPVARIANT value = {0};
    if (SUCCEEDED(IPropertyStore_GetValue(store, &name_key, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal)
        wcsncpy(device->name, value.pwszVal, 159);
    PropVariantClear(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &container_key, &value)) && value.vt == VT_CLSID &&
        value.puuid) {
        device->container = *value.puuid;
        device->has_container = true;
    }
    PropVariantClear(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &form_key, &value)) && value.vt == VT_UI4)
        device->headphones = value.ulVal == Headphones || value.ulVal == Headset;
    PropVariantClear(&value);
    IPropertyStore_Release(store);
    if (!*device->name)
        wcscpy(device->name, L"Audio device");
    return true;
}

static bool bluetooth_node(DEVINST node) {
    wchar_t id[MAX_DEVICE_ID_LEN] = {0};
    for (unsigned depth = 0; depth < 12; ++depth) {
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS &&
            (_wcsnicmp(id, L"BTHENUM\\", 8) == 0 || _wcsnicmp(id, L"BTHLE", 5) == 0 ||
             _wcsnicmp(id, L"BTHHFENUM\\", 10) == 0))
            return true;
        DEVINST parent;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS || parent == node)
            break;
        node = parent;
    }
    return false;
}

static void bluetooth_read(HeadphoneSnapshot *snapshot) {
    ULONG length = 0;
    ULONG flags = CM_GETIDLIST_FILTER_PRESENT;
    if (CM_Get_Device_ID_List_SizeW(&length, NULL, flags) != CR_SUCCESS || !length ||
        length > 1048576)
        return;
    wchar_t *ids = calloc(length, sizeof(wchar_t));
    if (!ids)
        return;
    if (CM_Get_Device_ID_ListW(NULL, ids, length, flags) != CR_SUCCESS) {
        free(ids);
        return;
    }
    for (wchar_t *id = ids; *id; id += wcslen(id) + 1) {
        DEVINST node;
        if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue;
        GUID container;
        DEVPROPTYPE type = 0;
        ULONG size = sizeof(container);
        if (CM_Get_DevNode_PropertyW(node, &node_container_key, &type, (BYTE *)&container, &size,
                                     0) != CR_SUCCESS ||
            type != DEVPROP_TYPE_GUID || size != sizeof(container))
            continue;
        bool relevant = false;
        for (unsigned i = 0; i < snapshot->count; ++i)
            relevant |= snapshot->devices[i].has_container &&
                        IsEqualGUID(&snapshot->devices[i].container, &container);
        if (!relevant || !bluetooth_node(node))
            continue;
        BYTE bytes[16] = {0};
        size = sizeof(bytes);
        type = 0;
        int battery = -1;
        if (CM_Get_DevNode_PropertyW(node, &battery_key, &type, bytes, &size, 0) == CR_SUCCESS)
            battery = headphones_battery(type, bytes, size);
        for (unsigned i = 0; i < snapshot->count; ++i) {
            HeadphoneDevice *device = &snapshot->devices[i];
            if (!device->has_container || !IsEqualGUID(&device->container, &container))
                continue;
            device->bluetooth = true;
            if (battery >= 0)
                device->battery_percent = battery;
        }
    }
    free(ids);
}

static void snapshot_read(IMMDeviceEnumerator *enumerator, HeadphoneSnapshot *snapshot) {
    *snapshot = (HeadphoneSnapshot){.initialized = true};
    IMMDeviceCollection *collection = NULL;
    snapshot->error = IMMDeviceEnumerator_EnumAudioEndpoints(enumerator, eRender,
                                                             DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(snapshot->error))
        return;
    UINT count = 0;
    snapshot->error = IMMDeviceCollection_GetCount(collection, &count);
    for (UINT i = 0; SUCCEEDED(snapshot->error) && i < count && snapshot->count < HEADPHONES_LIMIT;
         ++i) {
        IMMDevice *endpoint = NULL;
        if (FAILED(IMMDeviceCollection_Item(collection, i, &endpoint)))
            continue;
        HeadphoneDevice device;
        if (endpoint_read(endpoint, &device) && !find_device(snapshot, &device, NULL))
            snapshot->devices[snapshot->count++] = device;
        IMMDevice_Release(endpoint);
    }
    IMMDeviceCollection_Release(collection);
    bluetooth_read(snapshot);
    unsigned kept = 0;
    for (unsigned i = 0; i < snapshot->count; ++i) {
        HeadphoneDevice *device = &snapshot->devices[i];
        if (!device->headphones && !device->bluetooth)
            continue;
        bool duplicate = false;
        for (unsigned j = 0; j < kept; ++j)
            duplicate |= headphones_same(&snapshot->devices[j], device);
        if (!duplicate)
            snapshot->devices[kept++] = *device;
    }
    snapshot->count = kept;
}

static void publish(HeadphoneService *service, const HeadphoneSnapshot *snapshot) {
    HeadphoneSnapshot *message = malloc(sizeof(*message));
    if (!message)
        return;
    *message = *snapshot;
    if (!PostMessageW(service->window, WM_DYNAMICWINDOWS_HEADPHONES, 0, (LPARAM)message))
        free(message);
}

static DWORD WINAPI headphones_thread(void *argument) {
    HeadphoneService *service = argument;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT result = initialized;
    IMMDeviceEnumerator *enumerator = NULL;
    if (SUCCEEDED(result))
        result = CoCreateInstance(&enumerator_clsid, NULL, CLSCTX_INPROC_SERVER, &enumerator_iid,
                                  (void **)&enumerator);
    HeadphoneSnapshot previous = {0};
    HANDLE events[] = {service->stop, service->wake};
    do {
        HeadphoneSnapshot snapshot = {.initialized = true, .error = result};
        if (enumerator)
            snapshot_read(enumerator, &snapshot);
        if (memcmp(&previous, &snapshot, sizeof(snapshot)) != 0) {
            publish(service, &snapshot);
            previous = snapshot;
        }
    } while (WaitForMultipleObjects(2, events, FALSE, 2000) != WAIT_OBJECT_0);
    if (enumerator)
        IMMDeviceEnumerator_Release(enumerator);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return 0;
}

bool headphones_start(HeadphoneService *service, HWND window) {
    *service = (HeadphoneService){.window = window};
    service->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    service->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (service->stop && service->wake)
        service->thread = CreateThread(NULL, 0, headphones_thread, service, 0, NULL);
    if (service->thread)
        return true;
    headphones_stop(service);
    return false;
}

void headphones_wake(HeadphoneService *service) {
    if (service->wake)
        SetEvent(service->wake);
}

void headphones_stop(HeadphoneService *service) {
    if (service->stop)
        SetEvent(service->stop);
    if (service->thread) {
        WaitForSingleObject(service->thread, INFINITE);
        CloseHandle(service->thread);
    }
    if (service->wake)
        CloseHandle(service->wake);
    if (service->stop)
        CloseHandle(service->stop);
    *service = (HeadphoneService){0};
}
