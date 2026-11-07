#include <objidl.h>
#include <roapi.h>
#include <shobjidl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>
#include <winstring.h>

#define OBJECT(name)                                                                               \
    typedef struct name name;                                                                      \
    typedef struct name##Vtbl name##Vtbl;                                                          \
    struct name {                                                                                  \
        const name##Vtbl *lpVtbl;                                                                  \
    }
#define UNKNOWN(name)                                                                              \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(name *, REFIID, void **);                           \
    ULONG(STDMETHODCALLTYPE *AddRef)(name *);                                                      \
    ULONG(STDMETHODCALLTYPE *Release)(name *)
#define INSPECTABLE(name)                                                                          \
    UNKNOWN(name);                                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetIids)(name *, ULONG *, IID **);                                  \
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(name *, HSTRING *);                            \
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(name *, TrustLevel *)
#define BOOLEAN(name, property)                                                                    \
    HRESULT(STDMETHODCALLTYPE *get_##property)(name *, BYTE *);                                    \
    HRESULT(STDMETHODCALLTYPE *put_##property)(name *, BYTE)
#define STRING(name, property)                                                                     \
    HRESULT(STDMETHODCALLTYPE *get_##property)(name *, HSTRING *);                                 \
    HRESULT(STDMETHODCALLTYPE *put_##property)(name *, HSTRING)

OBJECT(Transport);
OBJECT(Interop);
OBJECT(Updater);
OBJECT(Music);
OBJECT(ButtonArgs);
OBJECT(ButtonHandler);

typedef struct {
    INT64 value;
} EventToken;

struct InteropVtbl {
    INSPECTABLE(Interop);
    HRESULT(STDMETHODCALLTYPE *GetForWindow)(Interop *, HWND, REFIID, void **);
};
struct TransportVtbl {
    INSPECTABLE(Transport);
    HRESULT(STDMETHODCALLTYPE *get_Status)(Transport *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *put_Status)(Transport *, INT32);
    HRESULT(STDMETHODCALLTYPE *get_Updater)(Transport *, Updater **);
    HRESULT(STDMETHODCALLTYPE *get_SoundLevel)(Transport *, INT32 *);
    BOOLEAN(Transport, Enabled);
    BOOLEAN(Transport, Play);
    BOOLEAN(Transport, Stop);
    BOOLEAN(Transport, Pause);
    BOOLEAN(Transport, Record);
    BOOLEAN(Transport, FastForward);
    BOOLEAN(Transport, Rewind);
    BOOLEAN(Transport, Previous);
    BOOLEAN(Transport, Next);
    BOOLEAN(Transport, ChannelUp);
    BOOLEAN(Transport, ChannelDown);
    HRESULT(STDMETHODCALLTYPE *add_Button)(Transport *, ButtonHandler *, EventToken *);
    HRESULT(STDMETHODCALLTYPE *remove_Button)(Transport *, EventToken);
};
struct UpdaterVtbl {
    INSPECTABLE(Updater);
    HRESULT(STDMETHODCALLTYPE *get_Type)(Updater *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *put_Type)(Updater *, INT32);
    STRING(Updater, AppMediaId);
    HRESULT(STDMETHODCALLTYPE *get_Thumbnail)(Updater *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *put_Thumbnail)(Updater *, IInspectable *);
    HRESULT(STDMETHODCALLTYPE *get_Music)(Updater *, Music **);
    HRESULT(STDMETHODCALLTYPE *get_Video)(Updater *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *get_Image)(Updater *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *CopyFile)(Updater *, INT32, IInspectable *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *Clear)(Updater *);
    HRESULT(STDMETHODCALLTYPE *Update)(Updater *);
};
struct MusicVtbl {
    INSPECTABLE(Music);
    STRING(Music, Title);
    STRING(Music, AlbumArtist);
    STRING(Music, Artist);
};
struct ButtonArgsVtbl {
    INSPECTABLE(ButtonArgs);
    HRESULT(STDMETHODCALLTYPE *Button)(ButtonArgs *, INT32 *);
};
struct ButtonHandlerVtbl {
    UNKNOWN(ButtonHandler);
    HRESULT(STDMETHODCALLTYPE *Invoke)(ButtonHandler *, Transport *, ButtonArgs *);
};

typedef struct {
    ButtonHandler client;
    volatile LONG references;
    HWND window;
} Handler;

static const IID interop_iid = {
    0xddb0472d, 0xc911, 0x4a1f, {0x86, 0xd9, 0xdc, 0x3d, 0x71, 0xa9, 0x5f, 0x5a}};
static const IID transport_iid = {
    0x99fa3ff4, 0x1742, 0x42a6, {0x90, 0x2e, 0x08, 0x7d, 0x41, 0xf9, 0x65, 0xec}};
static const IID handler_iid = {
    0x0557e996, 0x7b23, 0x5bae, {0xaa, 0x81, 0xea, 0x0d, 0x67, 0x11, 0x43, 0xa4}};

_Static_assert(offsetof(InteropVtbl, GetForWindow) == 6 * sizeof(void *), "Interop ABI");
_Static_assert(offsetof(TransportVtbl, add_Button) == 32 * sizeof(void *), "Transport ABI");
_Static_assert(offsetof(UpdaterVtbl, Update) == 17 * sizeof(void *), "Updater ABI");
_Static_assert(offsetof(MusicVtbl, put_Artist) == 11 * sizeof(void *), "Music ABI");

static HRESULT STDMETHODCALLTYPE handler_query(ButtonHandler *client, REFIID iid, void **result) {
    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &handler_iid))
        return E_NOINTERFACE;
    *result = client;
    client->lpVtbl->AddRef(client);
    return S_OK;
}
static ULONG STDMETHODCALLTYPE handler_add_ref(ButtonHandler *client) {
    return (ULONG)InterlockedIncrement(&((Handler *)client)->references);
}
static ULONG STDMETHODCALLTYPE handler_release(ButtonHandler *client) {
    Handler *handler = (Handler *)client;
    LONG references = InterlockedDecrement(&handler->references);
    if (references == 0)
        free(handler);
    return (ULONG)references;
}
static HRESULT STDMETHODCALLTYPE handler_invoke(ButtonHandler *client, Transport *sender,
                                                ButtonArgs *arguments) {
    (void)sender;
    INT32 button = 0;
    HRESULT result = arguments->lpVtbl->Button(arguments, &button);
    if (SUCCEEDED(result))
        PostMessageW(((Handler *)client)->window, WM_APP + 1, button, 0);
    return result;
}
static const ButtonHandlerVtbl handler_vtable = {handler_query, handler_add_ref, handler_release,
                                                 handler_invoke};

static LRESULT CALLBACK publisher_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Transport *transport = (Transport *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_APP + 1 && transport) {
        INT32 status = wparam == 1 ? 4 : 3;
        HRESULT result = transport->lpVtbl->put_Status(transport, status);
        printf("Button %u, status %d, result 0x%08lx\n", (unsigned)wparam, status,
               (unsigned long)result);
        fflush(stdout);
        return 0;
    }
    if (message == WM_TIMER || message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int wmain(int count, wchar_t **arguments);

int wmain(int count, wchar_t **arguments) {
    unsigned seconds = count > 1 ? (unsigned)wcstoul(arguments[1], NULL, 10) : 30;
    HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(result))
        return 1;
    SetCurrentProcessExplicitAppUserModelID(L"DynamicWindows.IntegrationTest");
    WNDCLASSEXW window_class = {.cbSize = sizeof(window_class),
                                .lpfnWndProc = publisher_window,
                                .hInstance = GetModuleHandleW(NULL),
                                .lpszClassName = L"DynamicWindows.MediaTest"};
    RegisterClassExW(&window_class);
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, window_class.lpszClassName,
                                  L"DynamicWindows test media publisher", WS_OVERLAPPED, 0, 0, 320,
                                  160, NULL, NULL, window_class.hInstance, NULL);
    HSTRING class_name = NULL;
    Interop *interop = NULL;
    Transport *transport = NULL;
    Updater *updater = NULL;
    Music *music = NULL;
    Handler *handler = NULL;
    EventToken token = {0};
    bool registered = false;
    const wchar_t *runtime = L"Windows.Media.SystemMediaTransportControls";
    result = WindowsCreateString(runtime, (UINT32)wcslen(runtime), &class_name);
    if (SUCCEEDED(result))
        result = RoGetActivationFactory(class_name, &interop_iid, (void **)&interop);
    WindowsDeleteString(class_name);
    if (SUCCEEDED(result))
        result =
            interop->lpVtbl->GetForWindow(interop, window, &transport_iid, (void **)&transport);
    if (FAILED(result))
        goto done;
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)transport);
    result = transport->lpVtbl->put_Enabled(transport, 1);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->put_Play(transport, 1);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->put_Pause(transport, 1);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->put_Previous(transport, 1);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->put_Next(transport, 1);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->get_Updater(transport, &updater);
    if (SUCCEEDED(result))
        result = updater->lpVtbl->put_Type(updater, 1);
    if (SUCCEEDED(result))
        result = updater->lpVtbl->get_Music(updater, &music);
    HSTRING title = NULL;
    HSTRING artist = NULL;
    WindowsCreateString(L"C Integration Track", 19, &title);
    WindowsCreateString(L"Native C test", 13, &artist);
    if (SUCCEEDED(result))
        result = music->lpVtbl->put_Title(music, title);
    if (SUCCEEDED(result))
        result = music->lpVtbl->put_Artist(music, artist);
    WindowsDeleteString(title);
    WindowsDeleteString(artist);
    if (SUCCEEDED(result))
        result = updater->lpVtbl->Update(updater);
    if (FAILED(result))
        goto done;
    handler = calloc(1, sizeof(*handler));
    if (!handler) {
        result = E_OUTOFMEMORY;
        goto done;
    }
    handler->client.lpVtbl = &handler_vtable;
    handler->references = 1;
    handler->window = window;
    result = transport->lpVtbl->add_Button(transport, &handler->client, &token);
    registered = SUCCEEDED(result);
    if (SUCCEEDED(result))
        result = transport->lpVtbl->put_Status(transport, 3);
    if (FAILED(result))
        goto done;
    SetTimer(window, 1, seconds * 1000, NULL);
    printf("READY: C Integration Track\n");
    fflush(stdout);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
done:
    if (FAILED(result))
        fprintf(stderr, "Media publisher failed: 0x%08lx\n", (unsigned long)result);
    if (registered)
        transport->lpVtbl->remove_Button(transport, token);
    if (handler)
        handler->client.lpVtbl->Release(&handler->client);
    if (music)
        music->lpVtbl->Release(music);
    if (updater)
        updater->lpVtbl->Release(updater);
    if (transport) {
        transport->lpVtbl->put_Enabled(transport, 0);
        transport->lpVtbl->Release(transport);
    }
    if (interop)
        interop->lpVtbl->Release(interop);
    if (IsWindow(window))
        DestroyWindow(window);
    RoUninitialize();
    return FAILED(result) ? 1 : 0;
}
