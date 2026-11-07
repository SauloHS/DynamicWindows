#ifndef DYNAMICWINDOWS_MEDIA_H
#define DYNAMICWINDOWS_MEDIA_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#define WM_DYNAMICWINDOWS_MEDIA (WM_APP + 1)

typedef enum { MEDIA_TOGGLE = 1, MEDIA_PREVIOUS = 2, MEDIA_NEXT = 4, MEDIA_SEEK = 8 } MediaCommand;

typedef struct {
    bool initialized;
    bool available;
    bool playing;
    bool can_toggle;
    bool can_previous;
    bool can_next;
    bool can_seek;
    double position;
    double duration;
    double rate;
    bool timeline_valid;
    uint64_t sampled_at;
    HRESULT error;
    wchar_t title[256];
    wchar_t artist[256];
    wchar_t source[160];
    BYTE *artwork;
    UINT artwork_size;
    bool artwork_changed;
} MediaSnapshot;

typedef struct {
    HWND window;
    HANDLE stop;
    HANDLE wake;
    HANDLE thread;
    volatile LONG commands;
    volatile LONG64 seek_position;
} MediaService;

bool media_start(MediaService *service, HWND window);
void media_stop(MediaService *service);
void media_command(MediaService *service, MediaCommand command, double position);
void media_snapshot_free(MediaSnapshot *snapshot);

#endif
