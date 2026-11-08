#include "notifications.h"

#include <appmodel.h>
#include <roapi.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <winstring.h>

#define ABI_OBJECT(name)                                                                           \
    typedef struct name name;                                                                      \
    typedef struct name##Vtbl name##Vtbl;                                                          \
    struct name {                                                                                  \
        const name##Vtbl *lpVtbl;                                                                  \
    }

#define ABI_INSPECTABLE(name)                                                                      \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(name *, REFIID, void **);                           \
    ULONG(STDMETHODCALLTYPE *AddRef)(name *);                                                      \
    ULONG(STDMETHODCALLTYPE *Release)(name *);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetIids)(name *, ULONG *, IID **);                                  \
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(name *, HSTRING *);                            \
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(name *, TrustLevel *)

#define ABI_ASYNC(name, result)                                                                    \
    ABI_OBJECT(name);                                                                              \
    struct name##Vtbl {                                                                            \
        ABI_INSPECTABLE(name);                                                                     \
        HRESULT(STDMETHODCALLTYPE *put_Completed)(name *, IUnknown *);                             \
        HRESULT(STDMETHODCALLTYPE *get_Completed)(name *, IUnknown **);                            \
        HRESULT(STDMETHODCALLTYPE *GetResults)(name *, result *);                                  \
    }

typedef BYTE NotificationBoolean;
typedef INT32 ListenerAccessStatus;
typedef struct {
    INT64 ticks;
} NotificationDateTime;
typedef struct {
    FLOAT width;
    FLOAT height;
} NotificationSize;

ABI_OBJECT(ListenerStatics);
ABI_OBJECT(Listener);
ABI_OBJECT(AsyncInfo);
ABI_OBJECT(UserView);
ABI_OBJECT(UserNotification);
ABI_OBJECT(Notification);
ABI_OBJECT(NotificationVisual);
ABI_OBJECT(NotificationBinding);
ABI_OBJECT(NotificationText);
ABI_OBJECT(TextView);
ABI_OBJECT(AppInfo);
ABI_OBJECT(AppDisplayInfo);
ABI_OBJECT(KnownBindingsStatics);
ABI_ASYNC(AsyncAccess, ListenerAccessStatus);
ABI_ASYNC(AsyncNotifications, UserView *);

struct ListenerStaticsVtbl {
    ABI_INSPECTABLE(ListenerStatics);
    HRESULT(STDMETHODCALLTYPE *Current)(ListenerStatics *, Listener **);
};

struct ListenerVtbl {
    ABI_INSPECTABLE(Listener);
    HRESULT(STDMETHODCALLTYPE *RequestAccessAsync)(Listener *, AsyncAccess **);
    HRESULT(STDMETHODCALLTYPE *GetAccessStatus)(Listener *, ListenerAccessStatus *);
    HRESULT(STDMETHODCALLTYPE *AddChanged)(Listener *, IUnknown *, INT64 *);
    HRESULT(STDMETHODCALLTYPE *RemoveChanged)(Listener *, INT64);
    HRESULT(STDMETHODCALLTYPE *GetNotificationsAsync)(Listener *, UINT32, AsyncNotifications **);
    HRESULT(STDMETHODCALLTYPE *GetNotification)(Listener *, UINT32, UserNotification **);
    HRESULT(STDMETHODCALLTYPE *ClearNotifications)(Listener *);
    HRESULT(STDMETHODCALLTYPE *RemoveNotification)(Listener *, UINT32);
};

struct AsyncInfoVtbl {
    ABI_INSPECTABLE(AsyncInfo);
    HRESULT(STDMETHODCALLTYPE *Id)(AsyncInfo *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *Status)(AsyncInfo *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *Error)(AsyncInfo *, HRESULT *);
    HRESULT(STDMETHODCALLTYPE *Cancel)(AsyncInfo *);
    HRESULT(STDMETHODCALLTYPE *Close)(AsyncInfo *);
};

struct UserViewVtbl {
    ABI_INSPECTABLE(UserView);
    HRESULT(STDMETHODCALLTYPE *GetAt)(UserView *, UINT32, UserNotification **);
    HRESULT(STDMETHODCALLTYPE *Size)(UserView *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *IndexOf)(UserView *, UserNotification *, UINT32 *,
                                        NotificationBoolean *);
    HRESULT(STDMETHODCALLTYPE *GetMany)(UserView *, UINT32, UINT32, UserNotification **, UINT32 *);
};

struct UserNotificationVtbl {
    ABI_INSPECTABLE(UserNotification);
    HRESULT(STDMETHODCALLTYPE *GetNotification)(UserNotification *, Notification **);
    HRESULT(STDMETHODCALLTYPE *GetAppInfo)(UserNotification *, AppInfo **);
    HRESULT(STDMETHODCALLTYPE *GetId)(UserNotification *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *GetCreationTime)(UserNotification *, NotificationDateTime *);
};

struct NotificationVtbl {
    ABI_INSPECTABLE(Notification);
    HRESULT(STDMETHODCALLTYPE *GetExpiration)(Notification *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *SetExpiration)(Notification *, IInspectable *);
    HRESULT(STDMETHODCALLTYPE *GetVisual)(Notification *, NotificationVisual **);
    HRESULT(STDMETHODCALLTYPE *SetVisual)(Notification *, NotificationVisual *);
};

struct NotificationVisualVtbl {
    ABI_INSPECTABLE(NotificationVisual);
    HRESULT(STDMETHODCALLTYPE *GetLanguage)(NotificationVisual *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *SetLanguage)(NotificationVisual *, HSTRING);
    HRESULT(STDMETHODCALLTYPE *GetBindings)(NotificationVisual *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *GetBinding)(NotificationVisual *, HSTRING, NotificationBinding **);
};

struct NotificationBindingVtbl {
    ABI_INSPECTABLE(NotificationBinding);
    HRESULT(STDMETHODCALLTYPE *GetTemplate)(NotificationBinding *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *SetTemplate)(NotificationBinding *, HSTRING);
    HRESULT(STDMETHODCALLTYPE *GetLanguage)(NotificationBinding *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *SetLanguage)(NotificationBinding *, HSTRING);
    HRESULT(STDMETHODCALLTYPE *GetHints)(NotificationBinding *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *GetTextElements)(NotificationBinding *, TextView **);
};

struct NotificationTextVtbl {
    ABI_INSPECTABLE(NotificationText);
    HRESULT(STDMETHODCALLTYPE *GetText)(NotificationText *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *SetText)(NotificationText *, HSTRING);
    HRESULT(STDMETHODCALLTYPE *GetLanguage)(NotificationText *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *SetLanguage)(NotificationText *, HSTRING);
};

struct TextViewVtbl {
    ABI_INSPECTABLE(TextView);
    HRESULT(STDMETHODCALLTYPE *GetAt)(TextView *, UINT32, NotificationText **);
    HRESULT(STDMETHODCALLTYPE *Size)(TextView *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *IndexOf)(TextView *, NotificationText *, UINT32 *,
                                        NotificationBoolean *);
    HRESULT(STDMETHODCALLTYPE *GetMany)(TextView *, UINT32, UINT32, NotificationText **, UINT32 *);
};

struct AppInfoVtbl {
    ABI_INSPECTABLE(AppInfo);
    HRESULT(STDMETHODCALLTYPE *GetId)(AppInfo *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *GetUserModelId)(AppInfo *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *GetDisplayInfo)(AppInfo *, AppDisplayInfo **);
    HRESULT(STDMETHODCALLTYPE *GetPackageFamilyName)(AppInfo *, HSTRING *);
};

struct AppDisplayInfoVtbl {
    ABI_INSPECTABLE(AppDisplayInfo);
    HRESULT(STDMETHODCALLTYPE *GetDisplayName)(AppDisplayInfo *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(AppDisplayInfo *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *GetLogo)(AppDisplayInfo *, NotificationSize, IInspectable **);
};

struct KnownBindingsStaticsVtbl {
    ABI_INSPECTABLE(KnownBindingsStatics);
    HRESULT(STDMETHODCALLTYPE *ToastGeneric)(KnownBindingsStatics *, HSTRING *);
};

static const IID listener_statics_iid = {
    0xff6123cf, 0x4386, 0x4aa3, {0xb7, 0x3d, 0xb8, 0x04, 0xe5, 0xb6, 0x3b, 0x23}};
static const IID known_bindings_iid = {
    0x79427bae, 0xa8b7, 0x4d58, {0x89, 0xea, 0x76, 0xa7, 0xb7, 0xbc, 0xcd, 0xed}};
static const IID async_info_iid = {
    0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

#define ABI_SLOT(type, member, slot)                                                               \
    _Static_assert(offsetof(type##Vtbl, member) == (slot) * sizeof(void *), "WinRT ABI slot")

ABI_SLOT(ListenerStatics, Current, 6);
ABI_SLOT(Listener, RequestAccessAsync, 6);
ABI_SLOT(Listener, GetNotificationsAsync, 10);
ABI_SLOT(AsyncAccess, GetResults, 8);
ABI_SLOT(AsyncNotifications, GetResults, 8);
ABI_SLOT(AsyncInfo, Status, 7);
ABI_SLOT(UserView, GetAt, 6);
ABI_SLOT(UserView, Size, 7);
ABI_SLOT(UserNotification, GetNotification, 6);
ABI_SLOT(UserNotification, GetAppInfo, 7);
ABI_SLOT(UserNotification, GetId, 8);
ABI_SLOT(UserNotification, GetCreationTime, 9);
ABI_SLOT(Notification, GetVisual, 8);
ABI_SLOT(NotificationVisual, GetBinding, 9);
ABI_SLOT(NotificationBinding, GetTextElements, 11);
ABI_SLOT(NotificationText, GetText, 6);
ABI_SLOT(TextView, GetAt, 6);
ABI_SLOT(TextView, Size, 7);
ABI_SLOT(AppInfo, GetDisplayInfo, 8);
ABI_SLOT(AppDisplayInfo, GetDisplayName, 6);
ABI_SLOT(KnownBindingsStatics, ToastGeneric, 6);
_Static_assert(sizeof(NotificationBoolean) == 1, "WinRT Boolean width");
_Static_assert(sizeof(NotificationDateTime) == 8, "WinRT DateTime width");

static const wchar_t listener_class[] =
    L"Windows.UI.Notifications.Management.UserNotificationListener";
static const wchar_t bindings_class[] = L"Windows.UI.Notifications.KnownNotificationBindings";

typedef struct {
    NotificationService *service;
    Listener *listener;
    HSTRING toast_generic;
    NotificationSeen seen;
} NotificationWorker;

void notifications_seen_reset(NotificationSeen *seen) {
    *seen = (NotificationSeen){0};
}

bool notifications_accept(NotificationSeen *seen, UINT id, int64_t created, bool baseline) {
    if (!seen)
        return false;
    for (unsigned i = 0; i < seen->count; ++i)
        if (seen->ids[i] == id && seen->times[i] == created)
            return false;
    unsigned index;
    if (seen->count < NOTIFICATION_SEEN) {
        index = seen->count++;
    } else {
        index = seen->next;
        seen->next = (seen->next + 1) % NOTIFICATION_SEEN;
    }
    seen->ids[index] = id;
    seen->times[index] = created;
    return seen->initialized && !baseline;
}

void notifications_clean(wchar_t *text, size_t count) {
    if (!text || !count)
        return;
    size_t read = 0;
    size_t written = 0;
    bool spacing = false;
    while (text[read] && written + 1 < count) {
        wchar_t character = text[read++];
        if (character < L' ' || character == 0x7f || character == 0x2028 || character == 0x2029) {
            spacing = written > 0;
            continue;
        }
        if (iswspace(character)) {
            spacing = written > 0;
            continue;
        }
        if (spacing && written + 1 < count)
            text[written++] = L' ';
        spacing = false;
        text[written++] = character;
    }
    while (written && iswspace(text[written - 1]))
        --written;
    text[written] = L'\0';
}

static void copy_hstring(HSTRING value, wchar_t *text, size_t count) {
    if (!value || !text || !count)
        return;
    UINT32 length = 0;
    const wchar_t *source = WindowsGetStringRawBuffer(value, &length);
    size_t copied = source && length < count - 1 ? length : source ? count - 1 : 0;
    if (copied)
        wmemcpy(text, source, copied);
    text[copied] = L'\0';
    notifications_clean(text, count);
}

static bool package_identity(void) {
    UINT32 length = 0;
    LONG result = GetCurrentPackageFullName(&length, NULL);
    return result == ERROR_INSUFFICIENT_BUFFER;
}

static void post_message(NotificationService *service, NotificationAccess access, HRESULT error,
                         const AppNotification *notification) {
    NotificationMessage *message = calloc(1, sizeof(*message));
    if (!message)
        return;
    message->access = access;
    message->error = error;
    if (notification) {
        message->has_notification = true;
        message->notification = *notification;
    }
    if (!PostMessageW(service->window, WM_DYNAMICWINDOWS_NOTIFICATION, 0, (LPARAM)message))
        free(message);
}

static HRESULT activation_factory(const wchar_t *name, REFIID iid, void **factory) {
    HSTRING class_name = NULL;
    HRESULT result = WindowsCreateString(name, (UINT32)wcslen(name), &class_name);
    if (FAILED(result))
        return result;
    result = RoGetActivationFactory(class_name, iid, factory);
    WindowsDeleteString(class_name);
    return result;
}

static HRESULT async_status(IUnknown *operation, INT32 *status, HRESULT *error) {
    AsyncInfo *info = NULL;
    HRESULT result = IUnknown_QueryInterface(operation, &async_info_iid, (void **)&info);
    if (FAILED(result))
        return result;
    result = info->lpVtbl->Status(info, status);
    if (SUCCEEDED(result) && *status == 3)
        result = info->lpVtbl->Error(info, error);
    info->lpVtbl->Release(info);
    return result;
}

static void operation_release(IUnknown *operation) {
    if (!operation)
        return;
    AsyncInfo *info = NULL;
    if (SUCCEEDED(IUnknown_QueryInterface(operation, &async_info_iid, (void **)&info))) {
        INT32 status = 0;
        if (SUCCEEDED(info->lpVtbl->Status(info, &status)) && status != 0)
            info->lpVtbl->Close(info);
        info->lpVtbl->Release(info);
    }
    IUnknown_Release(operation);
}

static HRESULT worker_open(NotificationWorker *worker) {
    ListenerStatics *factory = NULL;
    HRESULT result = activation_factory(listener_class, &listener_statics_iid, (void **)&factory);
    if (FAILED(result))
        return result;
    result = factory->lpVtbl->Current(factory, &worker->listener);
    factory->lpVtbl->Release(factory);
    if (FAILED(result) || !worker->listener)
        return FAILED(result) ? result : E_UNEXPECTED;
    KnownBindingsStatics *bindings = NULL;
    result = activation_factory(bindings_class, &known_bindings_iid, (void **)&bindings);
    if (SUCCEEDED(result) && bindings)
        result = bindings->lpVtbl->ToastGeneric(bindings, &worker->toast_generic);
    if (bindings)
        bindings->lpVtbl->Release(bindings);
    return result;
}

static void read_app(UserNotification *item, AppNotification *notification) {
    AppInfo *info = NULL;
    if (FAILED(item->lpVtbl->GetAppInfo(item, &info)) || !info)
        return;
    HSTRING value = NULL;
    if (SUCCEEDED(info->lpVtbl->GetUserModelId(info, &value)) && value) {
        copy_hstring(value, notification->app_id, 160);
        WindowsDeleteString(value);
    }
    AppDisplayInfo *display = NULL;
    if (SUCCEEDED(info->lpVtbl->GetDisplayInfo(info, &display)) && display) {
        value = NULL;
        if (SUCCEEDED(display->lpVtbl->GetDisplayName(display, &value)) && value) {
            copy_hstring(value, notification->source, 160);
            WindowsDeleteString(value);
        }
        display->lpVtbl->Release(display);
    }
    info->lpVtbl->Release(info);
}

static void append_body(wchar_t *body, size_t count, const wchar_t *value) {
    size_t used = wcslen(body);
    if (used && used + 2 < count) {
        body[used++] = L' ';
        body[used] = L'\0';
    }
    if (used + 1 < count)
        wcsncat(body, value, count - used - 1);
}

static void read_text(NotificationWorker *worker, UserNotification *item,
                      AppNotification *notification) {
    if (!worker->toast_generic)
        return;
    Notification *toast = NULL;
    NotificationVisual *visual = NULL;
    NotificationBinding *binding = NULL;
    TextView *texts = NULL;
    if (FAILED(item->lpVtbl->GetNotification(item, &toast)) || !toast)
        goto done;
    if (FAILED(toast->lpVtbl->GetVisual(toast, &visual)) || !visual)
        goto done;
    if (FAILED(visual->lpVtbl->GetBinding(visual, worker->toast_generic, &binding)) || !binding)
        goto done;
    if (FAILED(binding->lpVtbl->GetTextElements(binding, &texts)) || !texts)
        goto done;
    UINT32 count = 0;
    if (FAILED(texts->lpVtbl->Size(texts, &count)))
        goto done;
    for (UINT32 i = 0; i < count && i < 16; ++i) {
        NotificationText *element = NULL;
        HSTRING value = NULL;
        wchar_t text[384] = {0};
        if (SUCCEEDED(texts->lpVtbl->GetAt(texts, i, &element)) && element &&
            SUCCEEDED(element->lpVtbl->GetText(element, &value)) && value)
            copy_hstring(value, text, sizeof(text) / sizeof(text[0]));
        if (value)
            WindowsDeleteString(value);
        if (element)
            element->lpVtbl->Release(element);
        if (!*text)
            continue;
        if (!*notification->title)
            wcsncpy(notification->title, text, 255);
        else
            append_body(notification->body, 768, text);
    }
done:
    if (texts)
        texts->lpVtbl->Release(texts);
    if (binding)
        binding->lpVtbl->Release(binding);
    if (visual)
        visual->lpVtbl->Release(visual);
    if (toast)
        toast->lpVtbl->Release(toast);
}

static bool read_notification(NotificationWorker *worker, UserNotification *item,
                              AppNotification *notification) {
    *notification = (AppNotification){0};
    NotificationDateTime created = {0};
    if (FAILED(item->lpVtbl->GetId(item, &notification->id)) ||
        FAILED(item->lpVtbl->GetCreationTime(item, &created)))
        return false;
    notification->created = created.ticks;
    read_app(item, notification);
    if (InterlockedCompareExchange(&worker->service->content, 0, 0) != 0)
        read_text(worker, item, notification);
    if (!*notification->source)
        wcscpy(notification->source, L"Windows app");
    if (!*notification->title)
        wcscpy(notification->title, L"New notification");
    return true;
}

static HRESULT poll(NotificationWorker *worker, bool baseline) {
    AsyncNotifications *operation = NULL;
    HRESULT result =
        worker->listener->lpVtbl->GetNotificationsAsync(worker->listener, 1, &operation);
    if (FAILED(result) || !operation)
        return FAILED(result) ? result : E_UNEXPECTED;
    INT32 status = 0;
    HRESULT error = S_OK;
    uint64_t deadline = GetTickCount64() + 3000;
    do {
        result = async_status((IUnknown *)operation, &status, &error);
        if (FAILED(result) || status != 0)
            break;
    } while (GetTickCount64() < deadline &&
             WaitForSingleObject(worker->service->stop, 20) != WAIT_OBJECT_0);
    if (SUCCEEDED(result) && status == 1) {
        UserView *view = NULL;
        result = operation->lpVtbl->GetResults(operation, &view);
        UINT32 count = 0;
        if (SUCCEEDED(result) && view)
            result = view->lpVtbl->Size(view, &count);
        for (UINT32 i = 0; SUCCEEDED(result) && i < count; ++i) {
            UserNotification *item = NULL;
            AppNotification notification;
            if (SUCCEEDED(view->lpVtbl->GetAt(view, i, &item)) && item &&
                read_notification(worker, item, &notification) &&
                notifications_accept(&worker->seen, notification.id, notification.created,
                                     baseline))
                post_message(worker->service, NOTIFICATIONS_ALLOWED, S_OK, &notification);
            if (item)
                item->lpVtbl->Release(item);
        }
        if (view)
            view->lpVtbl->Release(view);
    } else if (SUCCEEDED(result) && status == 3) {
        result = FAILED(error) ? error : E_FAIL;
    } else if (SUCCEEDED(result)) {
        result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    operation_release((IUnknown *)operation);
    worker->seen.initialized = true;
    return result;
}

static DWORD WINAPI notification_thread(void *argument) {
    NotificationService *service = argument;
    NotificationWorker worker = {.service = service};
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    HRESULT result = initialized;
    if (SUCCEEDED(result))
        result = worker_open(&worker);
    ListenerAccessStatus access = 0;
    if (SUCCEEDED(result))
        result = worker.listener->lpVtbl->GetAccessStatus(worker.listener, &access);
    if (SUCCEEDED(result) && access != 1)
        result = E_ACCESSDENIED;
    if (SUCCEEDED(result)) {
        post_message(service, NOTIFICATIONS_ALLOWED, S_OK, NULL);
        bool baseline = true;
        while (WaitForSingleObject(service->stop, 0) != WAIT_OBJECT_0) {
            result = poll(&worker, baseline);
            baseline = false;
            if (FAILED(result)) {
                post_message(service, NOTIFICATIONS_UNAVAILABLE, result, NULL);
                break;
            }
            if (WaitForSingleObject(service->stop, 750) == WAIT_OBJECT_0)
                break;
        }
    } else {
        post_message(service,
                     result == E_ACCESSDENIED ? NOTIFICATIONS_DENIED : NOTIFICATIONS_UNAVAILABLE,
                     result, NULL);
    }
    if (worker.toast_generic)
        WindowsDeleteString(worker.toast_generic);
    if (worker.listener)
        worker.listener->lpVtbl->Release(worker.listener);
    if (SUCCEEDED(initialized))
        RoUninitialize();
    return 0;
}

static void service_thread_stop(NotificationService *service) {
    if (service->stop)
        SetEvent(service->stop);
    if (service->thread) {
        WaitForSingleObject(service->thread, INFINITE);
        CloseHandle(service->thread);
    }
    if (service->stop)
        CloseHandle(service->stop);
    service->thread = NULL;
    service->stop = NULL;
}

static void start_listener(NotificationService *service) {
    service_thread_stop(service);
    service->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (service->stop)
        service->thread = CreateThread(NULL, 0, notification_thread, service, 0, NULL);
    if (!service->thread) {
        service_thread_stop(service);
        service->access = NOTIFICATIONS_UNAVAILABLE;
        service->error = HRESULT_FROM_WIN32(GetLastError());
        post_message(service, service->access, service->error, NULL);
    }
}

void notifications_configure(NotificationService *service, HWND window, bool enabled,
                             bool content) {
    if (!service->window)
        service->window = window;
    InterlockedExchange(&service->content, content ? 1 : 0);
    service->enabled = enabled;
    if (!enabled) {
        service_thread_stop(service);
        service->access = NOTIFICATIONS_OFF;
        service->error = S_OK;
        return;
    }
    if (!package_identity()) {
        service_thread_stop(service);
        service->access = NOTIFICATIONS_PACKAGE_REQUIRED;
        service->error = HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE);
        return;
    }
    ListenerAccessStatus status = 0;
    ListenerStatics *factory = NULL;
    Listener *listener = NULL;
    HRESULT result = activation_factory(listener_class, &listener_statics_iid, (void **)&factory);
    if (SUCCEEDED(result))
        result = factory->lpVtbl->Current(factory, &listener);
    if (SUCCEEDED(result) && listener)
        result = listener->lpVtbl->GetAccessStatus(listener, &status);
    if (listener)
        listener->lpVtbl->Release(listener);
    if (factory)
        factory->lpVtbl->Release(factory);
    service->error = result;
    service->access = FAILED(result) ? NOTIFICATIONS_UNAVAILABLE
                      : status == 1  ? NOTIFICATIONS_ALLOWED
                      : status == 2  ? NOTIFICATIONS_DENIED
                                     : NOTIFICATIONS_OFF;
    if (service->access == NOTIFICATIONS_ALLOWED)
        start_listener(service);
}

bool notifications_request(NotificationService *service, HWND window) {
    if (!service)
        return false;
    if (!service->window)
        service->window = window;
    if (!package_identity()) {
        service->access = NOTIFICATIONS_PACKAGE_REQUIRED;
        service->error = HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE);
        return false;
    }
    if (service->access_operation)
        return true;
    ListenerAccessStatus status = 0;
    ListenerStatics *factory = NULL;
    Listener *listener = NULL;
    AsyncAccess *operation = NULL;
    HRESULT result = activation_factory(listener_class, &listener_statics_iid, (void **)&factory);
    if (SUCCEEDED(result))
        result = factory->lpVtbl->Current(factory, &listener);
    if (SUCCEEDED(result) && listener)
        result = listener->lpVtbl->GetAccessStatus(listener, &status);
    if (SUCCEEDED(result) && status == 1) {
        service->access = NOTIFICATIONS_ALLOWED;
        service->error = S_OK;
        if (service->enabled)
            start_listener(service);
        listener->lpVtbl->Release(listener);
        factory->lpVtbl->Release(factory);
        return true;
    }
    if (SUCCEEDED(result) && status == 2)
        result = E_ACCESSDENIED;
    if (SUCCEEDED(result))
        result = listener->lpVtbl->RequestAccessAsync(listener, &operation);
    if (listener)
        listener->lpVtbl->Release(listener);
    if (factory)
        factory->lpVtbl->Release(factory);
    if (FAILED(result) || !operation) {
        operation_release((IUnknown *)operation);
        service->access =
            result == E_ACCESSDENIED ? NOTIFICATIONS_DENIED : NOTIFICATIONS_UNAVAILABLE;
        service->error = FAILED(result) ? result : E_UNEXPECTED;
        return false;
    }
    service->access_operation = operation;
    service->access = NOTIFICATIONS_REQUESTING;
    service->error = S_OK;
    return true;
}

void notifications_tick(NotificationService *service) {
    AsyncAccess *operation = service ? service->access_operation : NULL;
    if (!operation)
        return;
    INT32 status = 0;
    HRESULT error = S_OK;
    HRESULT result = async_status((IUnknown *)operation, &status, &error);
    if (SUCCEEDED(result) && status == 0)
        return;
    if (FAILED(result)) {
        operation_release((IUnknown *)operation);
        service->access_operation = NULL;
        service->error = result;
        service->access = NOTIFICATIONS_UNAVAILABLE;
        post_message(service, service->access, result, NULL);
        return;
    }
    ListenerAccessStatus access = 0;
    if (status == 1)
        result = operation->lpVtbl->GetResults(operation, &access);
    else if (status == 3)
        result = FAILED(error) ? error : E_FAIL;
    else
        result = E_ABORT;
    operation_release((IUnknown *)operation);
    service->access_operation = NULL;
    service->error = result;
    service->access = FAILED(result) ? NOTIFICATIONS_UNAVAILABLE
                      : access == 1  ? NOTIFICATIONS_ALLOWED
                      : access == 2  ? NOTIFICATIONS_DENIED
                                     : NOTIFICATIONS_OFF;
    post_message(service, service->access, service->error, NULL);
    if (service->access == NOTIFICATIONS_ALLOWED && service->enabled)
        start_listener(service);
}

void notifications_stop(NotificationService *service) {
    if (!service)
        return;
    service_thread_stop(service);
    if (service->access_operation) {
        AsyncInfo *info = NULL;
        if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)service->access_operation,
                                              &async_info_iid, (void **)&info))) {
            info->lpVtbl->Cancel(info);
            for (unsigned i = 0; i < 100; ++i) {
                INT32 status = 0;
                if (FAILED(info->lpVtbl->Status(info, &status)) || status != 0)
                    break;
                Sleep(10);
            }
            info->lpVtbl->Release(info);
        }
        operation_release((IUnknown *)service->access_operation);
    }
    *service = (NotificationService){0};
}

void notifications_status(const NotificationService *service, wchar_t *text, size_t count) {
    const wchar_t *status = L"Notifications are off.";
    if (!service) {
        swprintf(text, count, L"%ls", status);
        return;
    }
    switch (service->access) {
    case NOTIFICATIONS_PACKAGE_REQUIRED:
        status = L"Install the MSIX build to enable notification access.";
        break;
    case NOTIFICATIONS_REQUESTING:
        status = L"Waiting for Windows notification permission.";
        break;
    case NOTIFICATIONS_ALLOWED:
        status = L"Windows notification access is allowed.";
        break;
    case NOTIFICATIONS_DENIED:
        status = L"Notification access is denied. Re-enable it in Windows Settings.";
        break;
    case NOTIFICATIONS_UNAVAILABLE:
        status = L"Windows notification listener is unavailable.";
        break;
    case NOTIFICATIONS_OFF:
        break;
    }
    swprintf(text, count, L"%ls", status);
}
