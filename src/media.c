#include "media.h"
#include "island.h"

#include <objidl.h>
#include <roapi.h>
#include <shcore.h>
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>
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

typedef BYTE MediaBoolean;
typedef struct {
    INT64 duration;
} MediaTime;

ABI_OBJECT(ManagerStatics);
ABI_OBJECT(Manager);
ABI_OBJECT(Session);
ABI_OBJECT(Properties);
ABI_OBJECT(PlaybackInfo);
ABI_OBJECT(DoubleReference);
ABI_OBJECT(Controls);
ABI_OBJECT(Timeline);
ABI_OBJECT(AsyncInfo);
ABI_OBJECT(StreamReference);
ABI_OBJECT(StreamContent);
ABI_ASYNC(AsyncManager, Manager *);
ABI_ASYNC(AsyncProperties, Properties *);
ABI_ASYNC(AsyncBoolean, MediaBoolean);
ABI_ASYNC(AsyncStream, StreamContent *);

struct ManagerStaticsVtbl {
    ABI_INSPECTABLE(ManagerStatics);
    HRESULT(STDMETHODCALLTYPE *RequestAsync)(ManagerStatics *, AsyncManager **);
};

struct ManagerVtbl {
    ABI_INSPECTABLE(Manager);
    HRESULT(STDMETHODCALLTYPE *GetCurrentSession)(Manager *, Session **);
};

struct SessionVtbl {
    ABI_INSPECTABLE(Session);
    HRESULT(STDMETHODCALLTYPE *Source)(Session *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *Properties)(Session *, AsyncProperties **);
    HRESULT(STDMETHODCALLTYPE *Timeline)(Session *, Timeline **);
    HRESULT(STDMETHODCALLTYPE *PlaybackInfo)(Session *, PlaybackInfo **);
    HRESULT(STDMETHODCALLTYPE *Play)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Pause)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Stop)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Record)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *FastForward)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Rewind)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Next)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Previous)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *ChannelUp)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *ChannelDown)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Toggle)(Session *, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Repeat)(Session *, INT32, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Rate)(Session *, DOUBLE, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Shuffle)(Session *, MediaBoolean, AsyncBoolean **);
    HRESULT(STDMETHODCALLTYPE *Seek)(Session *, INT64, AsyncBoolean **);
};

struct PropertiesVtbl {
    ABI_INSPECTABLE(Properties);
    HRESULT(STDMETHODCALLTYPE *Title)(Properties *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *Subtitle)(Properties *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *AlbumArtist)(Properties *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *Artist)(Properties *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *AlbumTitle)(Properties *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *TrackNumber)(Properties *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *Genres)(Properties *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *AlbumTrackCount)(Properties *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *PlaybackType)(Properties *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *Thumbnail)(Properties *, StreamReference **);
};

struct PlaybackInfoVtbl {
    ABI_INSPECTABLE(PlaybackInfo);
    HRESULT(STDMETHODCALLTYPE *Controls)(PlaybackInfo *, Controls **);
    HRESULT(STDMETHODCALLTYPE *Status)(PlaybackInfo *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *PlaybackType)(PlaybackInfo *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *RepeatMode)(PlaybackInfo *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *Rate)(PlaybackInfo *, DoubleReference **);
};

struct DoubleReferenceVtbl {
    ABI_INSPECTABLE(DoubleReference);
    HRESULT(STDMETHODCALLTYPE *Value)(DoubleReference *, double *);
};

struct ControlsVtbl {
    ABI_INSPECTABLE(Controls);
    HRESULT(STDMETHODCALLTYPE *Play)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Pause)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Stop)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Record)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *FastForward)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Rewind)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Next)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Previous)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *ChannelUp)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *ChannelDown)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Toggle)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Shuffle)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Repeat)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Rate)(Controls *, MediaBoolean *);
    HRESULT(STDMETHODCALLTYPE *Seek)(Controls *, MediaBoolean *);
};

struct TimelineVtbl {
    ABI_INSPECTABLE(Timeline);
    HRESULT(STDMETHODCALLTYPE *Start)(Timeline *, MediaTime *);
    HRESULT(STDMETHODCALLTYPE *End)(Timeline *, MediaTime *);
    HRESULT(STDMETHODCALLTYPE *MinSeek)(Timeline *, MediaTime *);
    HRESULT(STDMETHODCALLTYPE *MaxSeek)(Timeline *, MediaTime *);
    HRESULT(STDMETHODCALLTYPE *Position)(Timeline *, MediaTime *);
    HRESULT(STDMETHODCALLTYPE *LastUpdated)(Timeline *, MediaTime *);
};

struct AsyncInfoVtbl {
    ABI_INSPECTABLE(AsyncInfo);
    HRESULT(STDMETHODCALLTYPE *Id)(AsyncInfo *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *Status)(AsyncInfo *, INT32 *);
    HRESULT(STDMETHODCALLTYPE *Error)(AsyncInfo *, HRESULT *);
    HRESULT(STDMETHODCALLTYPE *Cancel)(AsyncInfo *);
    HRESULT(STDMETHODCALLTYPE *Close)(AsyncInfo *);
};

struct StreamReferenceVtbl {
    ABI_INSPECTABLE(StreamReference);
    HRESULT(STDMETHODCALLTYPE *OpenRead)(StreamReference *, AsyncStream **);
};

struct StreamContentVtbl {
    ABI_INSPECTABLE(StreamContent);
};

static const IID manager_statics_iid = {
    0x2050c4ee, 0x11a0, 0x57de, {0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45}};
static const IID async_info_iid = {
    0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

#define ABI_SLOT(type, member, slot)                                                               \
    _Static_assert(offsetof(type##Vtbl, member) == (slot) * sizeof(void *), "WinRT ABI slot")

ABI_SLOT(ManagerStatics, RequestAsync, 6);
ABI_SLOT(Manager, GetCurrentSession, 6);
ABI_SLOT(AsyncManager, GetResults, 8);
ABI_SLOT(AsyncProperties, GetResults, 8);
ABI_SLOT(AsyncBoolean, GetResults, 8);
ABI_SLOT(AsyncStream, GetResults, 8);
ABI_SLOT(AsyncInfo, Status, 7);
ABI_SLOT(Session, Properties, 7);
ABI_SLOT(Session, Next, 16);
ABI_SLOT(Session, Previous, 17);
ABI_SLOT(Session, Toggle, 20);
ABI_SLOT(Session, Seek, 24);
ABI_SLOT(Properties, Artist, 9);
ABI_SLOT(Properties, Thumbnail, 15);
ABI_SLOT(Controls, Toggle, 16);
ABI_SLOT(Controls, Seek, 20);
ABI_SLOT(Timeline, Position, 10);
ABI_SLOT(Timeline, LastUpdated, 11);
ABI_SLOT(PlaybackInfo, Rate, 10);
_Static_assert(sizeof(MediaBoolean) == 1, "WinRT Boolean width");
_Static_assert(sizeof(MediaTime) == 8, "WinRT TimeSpan width");

typedef struct {
    MediaService *service;
    Manager *manager;
    MediaSnapshot cached;
    INT64 timeline_updated;
    INT64 timeline_position;
    double clock_position;
    uint64_t clock_at;
    uint64_t next_properties;
    INT64 start;
    INT64 minimum;
    INT64 maximum;
    bool seek_range;
} MediaWorker;

static HRESULT await_operation(MediaService *service, IUnknown *operation, DWORD timeout) {
    AsyncInfo *info = NULL;
    HRESULT result = IUnknown_QueryInterface(operation, &async_info_iid, (void **)&info);
    if (FAILED(result))
        return result;
    uint64_t deadline = GetTickCount64() + timeout;
    INT32 status = 0;
    for (;;) {
        result = info->lpVtbl->Status(info, &status);
        if (FAILED(result))
            break;
        if (status == 1) {
            result = S_OK;
            break;
        }
        if (status == 2 || status == 3) {
            HRESULT error = E_ABORT;
            HRESULT error_result = info->lpVtbl->Error(info, &error);
            result = FAILED(error_result) ? error_result : FAILED(error) ? error : E_ABORT;
            break;
        }
        if (GetTickCount64() >= deadline ||
            WaitForSingleObject(service->stop, 20) == WAIT_OBJECT_0) {
            info->lpVtbl->Cancel(info);
            result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            break;
        }
    }
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

static void copy_string(HSTRING value, wchar_t *destination, size_t count) {
    UINT32 length = 0;
    const wchar_t *source = WindowsGetStringRawBuffer(value, &length);
    size_t copied = length < count - 1 ? length : count - 1;
    if (source && copied)
        wmemcpy(destination, source, copied);
    destination[copied] = L'\0';
    WindowsDeleteString(value);
}

static HRESULT manager_open(MediaWorker *worker) {
    static const wchar_t name[] =
        L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
    HSTRING class_name = NULL;
    HRESULT result = WindowsCreateString(name, (UINT32)wcslen(name), &class_name);
    if (FAILED(result))
        return result;
    ManagerStatics *factory = NULL;
    result = RoGetActivationFactory(class_name, &manager_statics_iid, (void **)&factory);
    WindowsDeleteString(class_name);
    if (FAILED(result))
        return result;
    AsyncManager *operation = NULL;
    result = factory->lpVtbl->RequestAsync(factory, &operation);
    factory->lpVtbl->Release(factory);
    if (SUCCEEDED(result) && operation) {
        result = await_operation(worker->service, (IUnknown *)operation, 4000);
        if (SUCCEEDED(result))
            result = operation->lpVtbl->GetResults(operation, &worker->manager);
    } else if (SUCCEEDED(result)) {
        result = E_UNEXPECTED;
    }
    operation_release((IUnknown *)operation);
    return result;
}

static BYTE *artwork_read(MediaService *service, Properties *properties, UINT *size) {
    *size = 0;
    StreamReference *reference = NULL;
    AsyncStream *operation = NULL;
    StreamContent *content = NULL;
    IStream *stream = NULL;
    BYTE *data = NULL;
    HRESULT result = properties->lpVtbl->Thumbnail(properties, &reference);
    if (FAILED(result) || !reference)
        return NULL;
    result = reference->lpVtbl->OpenRead(reference, &operation);
    reference->lpVtbl->Release(reference);
    if (FAILED(result) || !operation)
        goto done;
    result = await_operation(service, (IUnknown *)operation, 2000);
    if (FAILED(result))
        goto done;
    result = operation->lpVtbl->GetResults(operation, &content);
    if (FAILED(result) || !content)
        goto done;
    result =
        CreateStreamOverRandomAccessStream((IUnknown *)content, &IID_IStream, (void **)&stream);
    if (FAILED(result))
        goto done;
    STATSTG statistics = {0};
    result = IStream_Stat(stream, &statistics, STATFLAG_NONAME);
    if (FAILED(result) || statistics.cbSize.QuadPart == 0 ||
        statistics.cbSize.QuadPart > 8 * 1024 * 1024)
        goto done;
    UINT expected = (UINT)statistics.cbSize.QuadPart;
    data = malloc(expected);
    if (!data)
        goto done;
    LARGE_INTEGER start = {.QuadPart = 0};
    if (FAILED(IStream_Seek(stream, start, STREAM_SEEK_SET, NULL)))
        goto done;
    while (*size < expected) {
        ULONG read = 0;
        result = IStream_Read(stream, data + *size, expected - *size, &read);
        if (FAILED(result) || read == 0)
            break;
        *size += read;
        if (WaitForSingleObject(service->stop, 0) == WAIT_OBJECT_0)
            break;
    }
    if (*size != expected) {
        free(data);
        data = NULL;
        *size = 0;
    }
done:
    if (data && *size == 0) {
        free(data);
        data = NULL;
    }
    if (stream)
        IStream_Release(stream);
    if (content)
        content->lpVtbl->Release(content);
    operation_release((IUnknown *)operation);
    return data;
}

static HRESULT read_properties(MediaWorker *worker, Session *session, MediaSnapshot *snapshot) {
    AsyncProperties *operation = NULL;
    Properties *properties = NULL;
    HRESULT result = session->lpVtbl->Properties(session, &operation);
    if (FAILED(result) || !operation)
        return FAILED(result) ? result : E_UNEXPECTED;
    result = await_operation(worker->service, (IUnknown *)operation, 1500);
    if (SUCCEEDED(result))
        result = operation->lpVtbl->GetResults(operation, &properties);
    operation_release((IUnknown *)operation);
    if (FAILED(result) || !properties)
        return FAILED(result) ? result : E_UNEXPECTED;
    HSTRING title = NULL;
    HSTRING artist = NULL;
    if (SUCCEEDED(properties->lpVtbl->Title(properties, &title)))
        copy_string(title, snapshot->title, 256);
    if (SUCCEEDED(properties->lpVtbl->Artist(properties, &artist)))
        copy_string(artist, snapshot->artist, 256);
    if (!*snapshot->title)
        wcscpy(snapshot->title, L"Untitled media");
    bool changed = wcscmp(snapshot->title, worker->cached.title) != 0 ||
                   wcscmp(snapshot->artist, worker->cached.artist) != 0 ||
                   wcscmp(snapshot->source, worker->cached.source) != 0;
    if (changed) {
        snapshot->artwork_changed = true;
        snapshot->artwork = artwork_read(worker->service, properties, &snapshot->artwork_size);
    }
    properties->lpVtbl->Release(properties);
    return S_OK;
}

static void read_playback(Session *session, MediaSnapshot *snapshot) {
    PlaybackInfo *info = NULL;
    if (FAILED(session->lpVtbl->PlaybackInfo(session, &info)) || !info)
        return;
    INT32 status = 0;
    if (SUCCEEDED(info->lpVtbl->Status(info, &status))) {
        snapshot->playing = status == 4;
        snapshot->available = status == 4 || status == 5;
    }
    DoubleReference *rate = NULL;
    if (SUCCEEDED(info->lpVtbl->Rate(info, &rate)) && rate) {
        double value = 1;
        if (SUCCEEDED(rate->lpVtbl->Value(rate, &value)))
            snapshot->rate = island_clamp(value, 0.1, 8);
        rate->lpVtbl->Release(rate);
    }
    Controls *controls = NULL;
    if (SUCCEEDED(info->lpVtbl->Controls(info, &controls)) && controls) {
        MediaBoolean value = 0;
        if (SUCCEEDED(controls->lpVtbl->Toggle(controls, &value)))
            snapshot->can_toggle = value != 0;
        value = 0;
        if (SUCCEEDED(controls->lpVtbl->Next(controls, &value)))
            snapshot->can_next = value != 0;
        value = 0;
        if (SUCCEEDED(controls->lpVtbl->Previous(controls, &value)))
            snapshot->can_previous = value != 0;
        value = 0;
        if (SUCCEEDED(controls->lpVtbl->Seek(controls, &value)))
            snapshot->can_seek = value != 0;
        controls->lpVtbl->Release(controls);
    }
    info->lpVtbl->Release(info);
}

static void read_timeline(MediaWorker *worker, Session *session, MediaSnapshot *snapshot) {
    Timeline *timeline = NULL;
    worker->seek_range = false;
    snapshot->timeline_valid = false;
    snapshot->duration = 0;
    snapshot->position = 0;
    if (FAILED(session->lpVtbl->Timeline(session, &timeline)) || !timeline)
        return;
    MediaTime start = {0};
    MediaTime end = {0};
    MediaTime position = {0};
    MediaTime minimum = {0};
    MediaTime maximum = {0};
    MediaTime updated = {0};
    HRESULT updated_result = timeline->lpVtbl->LastUpdated(timeline, &updated);
    HRESULT start_result = timeline->lpVtbl->Start(timeline, &start);
    HRESULT end_result = timeline->lpVtbl->End(timeline, &end);
    HRESULT position_result = timeline->lpVtbl->Position(timeline, &position);
    HRESULT min_result = timeline->lpVtbl->MinSeek(timeline, &minimum);
    HRESULT max_result = timeline->lpVtbl->MaxSeek(timeline, &maximum);
    if (SUCCEEDED(start_result) && SUCCEEDED(end_result) && SUCCEEDED(position_result) &&
        end.duration > start.duration) {
        double duration = ((double)end.duration - (double)start.duration) / 10000000;
        if (duration > 0 && duration <= 604800) {
            snapshot->duration = duration;
            double raw_position = island_clamp(
                ((double)position.duration - (double)start.duration) / 10000000, 0, duration);
            uint64_t sampled = GetTickCount64();
            bool track_changed = wcscmp(snapshot->title, worker->cached.title) != 0 ||
                                 wcscmp(snapshot->artist, worker->cached.artist) != 0 ||
                                 wcscmp(snapshot->source, worker->cached.source) != 0;
            bool fresh =
                !worker->clock_at || track_changed ||
                position.duration != worker->timeline_position ||
                (SUCCEEDED(updated_result) && updated.duration != worker->timeline_updated);
            double current = worker->clock_position;
            if (worker->clock_at && worker->cached.playing && sampled >= worker->clock_at)
                current += (sampled - worker->clock_at) / 1000.0 * worker->cached.rate;
            if (fresh) {
                current = raw_position;
                if (snapshot->playing && SUCCEEDED(updated_result) && updated.duration > 0) {
                    FILETIME time;
                    GetSystemTimeAsFileTime(&time);
                    ULARGE_INTEGER utc = {.LowPart = time.dwLowDateTime,
                                          .HighPart = time.dwHighDateTime};
                    INT64 age = (INT64)utc.QuadPart - updated.duration;
                    if (age > 0 && age < INT64_C(864000000000))
                        current += age / 10000000.0 * snapshot->rate;
                }
            }
            snapshot->position = island_clamp(current, 0, duration);
            snapshot->sampled_at = sampled;
            snapshot->timeline_valid = true;
            worker->clock_position = snapshot->position;
            worker->clock_at = sampled;
            worker->timeline_position = position.duration;
            worker->timeline_updated = updated.duration;
            worker->start = start.duration;
            worker->minimum = SUCCEEDED(min_result) && minimum.duration > start.duration
                                  ? minimum.duration
                                  : start.duration;
            worker->maximum = SUCCEEDED(max_result) && maximum.duration < end.duration
                                  ? maximum.duration
                                  : end.duration;
            worker->seek_range = worker->maximum > worker->minimum;
        }
    }
    timeline->lpVtbl->Release(timeline);
    if (!worker->seek_range)
        snapshot->can_seek = false;
}

static HRESULT run_command(MediaWorker *worker, Session *session, MediaCommand command) {
    AsyncBoolean *operation = NULL;
    HRESULT result;
    if (command == MEDIA_TOGGLE)
        result = session->lpVtbl->Toggle(session, &operation);
    else if (command == MEDIA_PREVIOUS)
        result = session->lpVtbl->Previous(session, &operation);
    else if (command == MEDIA_NEXT)
        result = session->lpVtbl->Next(session, &operation);
    else {
        if (!worker->seek_range)
            return E_INVALIDARG;
        LONG64 relative = InterlockedCompareExchange64(&worker->service->seek_position, 0, 0);
        double absolute = (double)worker->start + (double)relative;
        INT64 target =
            (INT64)island_clamp(absolute, (double)worker->minimum, (double)worker->maximum);
        result = session->lpVtbl->Seek(session, target, &operation);
    }
    if (SUCCEEDED(result) && operation) {
        result = await_operation(worker->service, (IUnknown *)operation, 1500);
        if (SUCCEEDED(result)) {
            MediaBoolean accepted = 0;
            result = operation->lpVtbl->GetResults(operation, &accepted);
            if (SUCCEEDED(result) && !accepted)
                result = E_FAIL;
        }
    } else if (SUCCEEDED(result)) {
        result = E_UNEXPECTED;
    }
    operation_release((IUnknown *)operation);
    return result;
}

static void publish(MediaWorker *worker, MediaSnapshot *snapshot) {
    MediaSnapshot *message = malloc(sizeof(*message));
    if (!message) {
        free(snapshot->artwork);
        return;
    }
    *message = *snapshot;
    worker->cached = *snapshot;
    worker->cached.artwork = NULL;
    worker->cached.artwork_size = 0;
    worker->cached.artwork_changed = false;
    if (!PostMessageW(worker->service->window, WM_DYNAMICWINDOWS_MEDIA, 0, (LPARAM)message))
        media_snapshot_free(message);
}

static void session_poll(MediaWorker *worker) {
    MediaSnapshot snapshot = worker->cached;
    snapshot.error = S_OK;
    snapshot.initialized = true;
    snapshot.artwork = NULL;
    snapshot.artwork_size = 0;
    snapshot.artwork_changed = false;
    snapshot.can_toggle = false;
    snapshot.can_next = false;
    snapshot.can_previous = false;
    snapshot.can_seek = false;
    snapshot.playing = false;
    snapshot.sampled_at = 0;
    Session *session = NULL;
    HRESULT result = worker->manager->lpVtbl->GetCurrentSession(worker->manager, &session);
    if (FAILED(result) || !session) {
        snapshot = (MediaSnapshot){
            .initialized = true, .error = result, .artwork_changed = worker->cached.available};
        worker->next_properties = 0;
        worker->clock_at = 0;
        if (FAILED(result)) {
            worker->manager->lpVtbl->Release(worker->manager);
            worker->manager = NULL;
        }
        InterlockedExchange(&worker->service->commands, 0);
        publish(worker, &snapshot);
        return;
    }
    snapshot.available = true;
    HSTRING source = NULL;
    if (SUCCEEDED(session->lpVtbl->Source(session, &source)))
        copy_string(source, snapshot.source, 160);
    bool changed = wcscmp(snapshot.source, worker->cached.source) != 0;
    if (changed) {
        snapshot.title[0] = L'\0';
        snapshot.artist[0] = L'\0';
        snapshot.artwork_changed = true;
    }
    uint64_t now = GetTickCount64();
    if (changed || now >= worker->next_properties) {
        snapshot.error = read_properties(worker, session, &snapshot);
        worker->next_properties = now + 1000;
    }
    snapshot.rate = 1;
    read_playback(session, &snapshot);
    read_timeline(worker, session, &snapshot);
    LONG commands = InterlockedExchange(&worker->service->commands, 0);
    static const MediaCommand order[] = {MEDIA_PREVIOUS, MEDIA_NEXT, MEDIA_TOGGLE, MEDIA_SEEK};
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if (!(commands & order[i]))
            continue;
        result = run_command(worker, session, order[i]);
        if (FAILED(result))
            snapshot.error = result;
        worker->next_properties = 0;
    }
    if (!snapshot.sampled_at)
        snapshot.sampled_at = GetTickCount64();
    session->lpVtbl->Release(session);
    publish(worker, &snapshot);
}

static DWORD WINAPI media_thread(void *argument) {
    MediaService *service = argument;
    MediaWorker worker = {.service = service};
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) {
        MediaSnapshot snapshot = {.initialized = true, .error = initialized};
        publish(&worker, &snapshot);
        return 1;
    }
    HANDLE events[] = {service->stop, service->wake};
    while (WaitForSingleObject(service->stop, 0) != WAIT_OBJECT_0) {
        DWORD delay = 500;
        if (!worker.manager) {
            HRESULT result = manager_open(&worker);
            if (FAILED(result) || !worker.manager) {
                MediaSnapshot snapshot = {.initialized = true, .error = result};
                publish(&worker, &snapshot);
                delay = 5000;
            }
        }
        if (worker.manager)
            session_poll(&worker);
        if (WaitForMultipleObjects(2, events, FALSE, delay) == WAIT_OBJECT_0)
            break;
    }
    if (worker.manager)
        worker.manager->lpVtbl->Release(worker.manager);
    RoUninitialize();
    return 0;
}

bool media_start(MediaService *service, HWND window) {
    *service = (MediaService){.window = window};
    service->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    service->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!service->stop || !service->wake) {
        media_stop(service);
        return false;
    }
    service->thread = CreateThread(NULL, 0, media_thread, service, 0, NULL);
    if (!service->thread) {
        media_stop(service);
        return false;
    }
    return true;
}

void media_stop(MediaService *service) {
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
    *service = (MediaService){0};
}

void media_command(MediaService *service, MediaCommand command, double position) {
    if (!service->thread)
        return;
    if (command == MEDIA_SEEK) {
        position = island_clamp(position, 0, 604800);
        InterlockedExchange64(&service->seek_position, (LONG64)(position * 10000000));
    }
    InterlockedOr(&service->commands, command);
    SetEvent(service->wake);
}

void media_snapshot_free(MediaSnapshot *snapshot) {
    if (!snapshot)
        return;
    free(snapshot->artwork);
    free(snapshot);
}
