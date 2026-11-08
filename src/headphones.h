#ifndef DYNAMICWINDOWS_HEADPHONES_H
#define DYNAMICWINDOWS_HEADPHONES_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#define WM_DYNAMICWINDOWS_HEADPHONES (WM_APP + 5)
#define HEADPHONES_LIMIT 16

typedef struct {
    wchar_t id[260];
    wchar_t name[160];
    GUID container;
    bool has_container;
    bool bluetooth;
    bool headphones;
    int battery_percent;
} HeadphoneDevice;

typedef struct {
    bool initialized;
    HRESULT error;
    unsigned count;
    HeadphoneDevice devices[HEADPHONES_LIMIT];
} HeadphoneSnapshot;

typedef enum { HEADPHONE_CONNECTED, HEADPHONE_DISCONNECTED, HEADPHONE_BATTERY } HeadphoneChange;

typedef struct {
    HeadphoneChange change;
    HeadphoneDevice device;
} HeadphoneEvent;

typedef struct {
    HWND window;
    HANDLE stop;
    HANDLE wake;
    HANDLE thread;
} HeadphoneService;

int headphones_battery(unsigned long type, const void *value, unsigned long size);
bool headphones_same(const HeadphoneDevice *first, const HeadphoneDevice *second);
const HeadphoneDevice *headphones_select(const HeadphoneSnapshot *snapshot,
                                         const wchar_t *output_id);
double headphones_progress(int battery_percent);
unsigned headphones_changes(const HeadphoneSnapshot *previous, const HeadphoneSnapshot *current,
                            HeadphoneEvent *events, unsigned capacity);
void headphones_detail(const HeadphoneDevice *device, bool show_battery, wchar_t *text,
                       size_t count);
bool headphones_start(HeadphoneService *service, HWND window);
void headphones_wake(HeadphoneService *service);
void headphones_stop(HeadphoneService *service);

#endif
