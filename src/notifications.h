#ifndef DYNAMICWINDOWS_NOTIFICATIONS_H
#define DYNAMICWINDOWS_NOTIFICATIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>

#define WM_DYNAMICWINDOWS_NOTIFICATION (WM_APP + 6)
#define NOTIFICATION_HISTORY 8
#define NOTIFICATION_SEEN 512

typedef enum {
    NOTIFICATIONS_OFF,
    NOTIFICATIONS_PACKAGE_REQUIRED,
    NOTIFICATIONS_REQUESTING,
    NOTIFICATIONS_ALLOWED,
    NOTIFICATIONS_DENIED,
    NOTIFICATIONS_UNAVAILABLE
} NotificationAccess;

typedef struct {
    UINT id;
    int64_t created;
    wchar_t source[160];
    wchar_t app_id[160];
    wchar_t title[256];
    wchar_t body[768];
} AppNotification;

typedef struct {
    UINT ids[NOTIFICATION_SEEN];
    int64_t times[NOTIFICATION_SEEN];
    unsigned count;
    unsigned next;
    bool initialized;
} NotificationSeen;

typedef struct {
    NotificationAccess access;
    HRESULT error;
    bool has_notification;
    AppNotification notification;
} NotificationMessage;

typedef struct {
    HWND window;
    HANDLE stop;
    HANDLE thread;
    void *access_operation;
    NotificationAccess access;
    HRESULT error;
    bool enabled;
    volatile LONG content;
} NotificationService;

void notifications_seen_reset(NotificationSeen *seen);
bool notifications_accept(NotificationSeen *seen, UINT id, int64_t created, bool baseline);
void notifications_clean(wchar_t *text, size_t count);
bool notifications_request(NotificationService *service, HWND window);
void notifications_configure(NotificationService *service, HWND window, bool enabled, bool content);
void notifications_tick(NotificationService *service);
void notifications_stop(NotificationService *service);
void notifications_status(const NotificationService *service, wchar_t *text, size_t count);

#endif
