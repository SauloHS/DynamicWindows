#include "speech.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static const CLSID speech_enumerator_class = {
    0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const IID speech_enumerator_iid = {
    0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const IID speech_client_iid = {
    0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const IID speech_capture_iid = {
    0xc8adbd64, 0xe71e, 0x48a0, {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}};

static bool speech_cancelled(void *argument) {
    SpeechCancel *cancel = argument;
    return WaitForSingleObject(cancel->stop, 0) == WAIT_OBJECT_0 ||
           InterlockedCompareExchange(cancel->cancel, 0, 0) ||
           (cancel->deadline && GetTickCount64() >= cancel->deadline);
}

static HRESULT cancellation_error(const SpeechCancel *cancel) {
    return cancel->deadline && GetTickCount64() >= cancel->deadline
               ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
               : E_ABORT;
}

bool speech_activity_update(SpeechActivity *activity, const float *samples, size_t count) {
    size_t voiced = 0;
    for (size_t offset = 0; offset < count; offset += 160) {
        size_t end = offset + 160 < count ? offset + 160 : count;
        double energy = 0;
        for (size_t i = offset; i < end; ++i)
            energy += (double)samples[i] * samples[i];
        if (energy > (double)(end - offset) * 0.000025) {
            voiced += end - offset;
            activity->last_voice = activity->samples + end;
        }
    }
    activity->samples += count;
    activity->voiced_samples += voiced;
    return activity->voiced_samples >= SPEECH_SAMPLE_RATE / 5 &&
           activity->samples - activity->last_voice >= SPEECH_SAMPLE_RATE * 3 / 2;
}

HRESULT speech_path(const wchar_t *path, wchar_t *absolute, size_t count) {
    if (!path || !*path || !absolute || count < 2 || count > MAXDWORD)
        return E_INVALIDARG;
    wchar_t candidate[1024];
    if (path[0] == L'\\' || path[0] == L'/' || (iswalpha(path[0]) && path[1] == L':')) {
        if (wcslen(path) >= 1024)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        wcscpy(candidate, path);
    } else {
        DWORD length = GetModuleFileNameW(NULL, candidate, 1024);
        if (!length || length >= 1024)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        wchar_t *slash = wcsrchr(candidate, L'\\');
        if (!slash || (size_t)(slash - candidate) + 1 + wcslen(path) >= 1024)
            return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
        wcscpy(slash + 1, path);
    }
    DWORD length = GetFullPathNameW(candidate, (DWORD)count, absolute, NULL);
    if (!length)
        return HRESULT_FROM_WIN32(GetLastError());
    return length >= count ? HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE) : S_OK;
}

static void speech_log(int level, const char *text, void *argument) {
    (void)level;
    (void)text;
    (void)argument;
}

static HRESULT speech_load(SpeechBackend *backend, wchar_t *detail, size_t detail_count) {
    if (backend->library)
        return S_OK;
    wchar_t path[1024];
    HRESULT result = speech_path(L"speech\\whisper.dll", path, 1024);
    if (FAILED(result))
        return result;
    backend->library =
        LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!backend->library) {
        HRESULT error = HRESULT_FROM_WIN32(GetLastError());
        swprintf(detail, detail_count, L"Install local Whisper with setup-speech.ps1");
        return error;
    }
#define LOAD_API(field, symbol)                                                                    \
    do {                                                                                           \
        FARPROC address = GetProcAddress(backend->library, symbol);                                \
        _Static_assert(sizeof(address) == sizeof(backend->api.field), "Function pointer ABI");     \
        if (!address)                                                                              \
            goto invalid_runtime;                                                                  \
        memcpy(&backend->api.field, &address, sizeof(address));                                    \
    } while (0)
    LOAD_API(version, "whisper_version");
    LOAD_API(context_params, "whisper_context_default_params_by_ref");
    LOAD_API(free_context_params, "whisper_free_context_params");
    LOAD_API(initialize, "whisper_init_from_buffer_with_params");
    LOAD_API(full_params, "whisper_full_default_params_by_ref");
    LOAD_API(free_full_params, "whisper_free_params");
    LOAD_API(transcribe, "whisper_full");
    LOAD_API(segments, "whisper_full_n_segments");
    LOAD_API(segment_text, "whisper_full_get_segment_text");
    LOAD_API(free_context, "whisper_free");
    LOAD_API(log_set, "whisper_log_set");
#undef LOAD_API
    if (strcmp(backend->api.version(), WHISPER_RUNTIME_VERSION) != 0)
        goto invalid_runtime;
    backend->api.log_set(speech_log, NULL);
    return S_OK;
invalid_runtime:
    swprintf(detail, detail_count, L"Whisper runtime must be version 1.8.2");
    FreeLibrary(backend->library);
    backend->library = NULL;
    backend->api = (WhisperApi){0};
    return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
}

HRESULT speech_prepare(SpeechBackend *backend, const wchar_t *model_path, SpeechCancel *cancel,
                       wchar_t *detail, size_t detail_count) {
    wchar_t path[1024];
    HRESULT result = speech_path(model_path, path, 1024);
    if (FAILED(result)) {
        swprintf(detail, detail_count, L"Set a local speech model path");
        return result;
    }
    if (backend->context && wcscmp(path, backend->model_path) == 0)
        return speech_cancelled(cancel) ? cancellation_error(cancel) : S_OK;
    result = speech_load(backend, detail, detail_count);
    if (FAILED(result))
        return result;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        result = HRESULT_FROM_WIN32(GetLastError());
        swprintf(detail, detail_count, L"Local speech model file is missing");
        return result;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 4 || size.QuadPart > INT64_C(2147483648)) {
        CloseHandle(file);
        swprintf(detail, detail_count, L"Local speech model size is invalid");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    HANDLE mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    void *data = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (!data) {
        result = HRESULT_FROM_WIN32(GetLastError());
        if (mapping)
            CloseHandle(mapping);
        CloseHandle(file);
        return result;
    }
    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    if (magic != UINT32_C(0x67676d6c)) {
        swprintf(detail, detail_count, L"Select a Whisper ggml model file");
        result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    } else if (speech_cancelled(cancel)) {
        result = cancellation_error(cancel);
    } else {
        if (backend->context) {
            backend->api.free_context(backend->context);
            backend->context = NULL;
        }
        WhisperContextParams *parameters = backend->api.context_params();
        if (!parameters) {
            result = E_OUTOFMEMORY;
        } else {
            parameters->use_gpu = false;
            backend->context = backend->api.initialize(data, (size_t)size.QuadPart, *parameters);
            backend->api.free_context_params(parameters);
            if (!backend->context) {
                swprintf(detail, detail_count, L"Local speech model could not load");
                result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            } else {
                wcscpy(backend->model_path, path);
                if (speech_cancelled(cancel))
                    result = cancellation_error(cancel);
            }
        }
    }
    UnmapViewOfFile(data);
    CloseHandle(mapping);
    CloseHandle(file);
    return result;
}

HRESULT speech_capture(SpeechCancel *cancel, float **samples, size_t *count, wchar_t *detail,
                       size_t detail_count) {
    *samples = NULL;
    *count = 0;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioCaptureClient *capture = NULL;
    HANDLE ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    HRESULT result = ready ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(result))
        result = CoCreateInstance(&speech_enumerator_class, NULL, CLSCTX_INPROC_SERVER,
                                  &speech_enumerator_iid, (void **)&enumerator);
    if (SUCCEEDED(result))
        result = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eCapture, eCommunications,
                                                             &device);
    if (SUCCEEDED(result))
        result = IMMDevice_Activate(device, &speech_client_iid, CLSCTX_INPROC_SERVER, NULL,
                                    (void **)&client);
    WAVEFORMATEX format = {.wFormatTag = WAVE_FORMAT_PCM,
                           .nChannels = 1,
                           .nSamplesPerSec = SPEECH_SAMPLE_RATE,
                           .nAvgBytesPerSec = SPEECH_SAMPLE_RATE * 2,
                           .nBlockAlign = 2,
                           .wBitsPerSample = 16};
    if (SUCCEEDED(result))
        result = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
                                         AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                             AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                             AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                         1000000, 0, &format, NULL);
    if (SUCCEEDED(result))
        result = IAudioClient_SetEventHandle(client, ready);
    if (SUCCEEDED(result))
        result = IAudioClient_GetService(client, &speech_capture_iid, (void **)&capture);
    size_t capacity = SPEECH_SAMPLE_RATE * SPEECH_MAX_SECONDS;
    float *audio = SUCCEEDED(result) ? calloc(capacity, sizeof(*audio)) : NULL;
    if (SUCCEEDED(result) && !audio)
        result = E_OUTOFMEMORY;
    bool started = false;
    if (SUCCEEDED(result) && speech_cancelled(cancel))
        result = cancellation_error(cancel);
    if (SUCCEEDED(result)) {
        result = IAudioClient_Start(client);
        started = SUCCEEDED(result);
    }
    if (FAILED(result))
        swprintf(detail, detail_count, L"Microphone could not start");
    SpeechActivity activity = {0};
    uint64_t deadline = GetTickCount64() + SPEECH_MAX_SECONDS * 1000;
    bool complete = false;
    HANDLE events[] = {cancel->stop, ready};
    while (SUCCEEDED(result) && !complete && activity.samples < capacity &&
           GetTickCount64() < deadline) {
        if (speech_cancelled(cancel)) {
            result = cancellation_error(cancel);
            break;
        }
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, 20);
        if (wait == WAIT_FAILED) {
            result = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
        if (wait == WAIT_OBJECT_0) {
            result = E_ABORT;
            break;
        }
        UINT32 packet = 0;
        result = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
        while (SUCCEEDED(result) && packet && !complete && activity.samples < capacity) {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            result = IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL);
            if (FAILED(result))
                break;
            size_t remaining = capacity - activity.samples;
            size_t copied = frames < remaining ? frames : remaining;
            size_t offset = activity.samples;
            const int16_t *pcm = (const int16_t *)data;
            for (size_t i = 0; i < copied; ++i)
                audio[offset + i] = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0 : pcm[i] / 32768.0f;
            complete = speech_activity_update(&activity, audio + offset, copied);
            result = IAudioCaptureClient_ReleaseBuffer(capture, frames);
            if (SUCCEEDED(result))
                result = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
        }
    }
    if (started)
        IAudioClient_Stop(client);
    if (capture)
        IAudioCaptureClient_Release(capture);
    if (client)
        IAudioClient_Release(client);
    if (device)
        IMMDevice_Release(device);
    if (enumerator)
        IMMDeviceEnumerator_Release(enumerator);
    if (ready)
        CloseHandle(ready);
    if (SUCCEEDED(result) && speech_cancelled(cancel))
        result = cancellation_error(cancel);
    if (SUCCEEDED(result) && activity.voiced_samples < SPEECH_SAMPLE_RATE / 5) {
        swprintf(detail, detail_count, L"No speech heard from microphone");
        result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (FAILED(result)) {
        if (audio) {
            SecureZeroMemory(audio, capacity * sizeof(*audio));
            free(audio);
        }
        return result;
    }
    *samples = audio;
    *count = activity.samples;
    return S_OK;
}

HRESULT speech_transcribe(SpeechBackend *backend, SpeechCancel *cancel, const float *samples,
                          size_t count, wchar_t *text, size_t text_count, wchar_t *detail,
                          size_t detail_count) {
    if (!backend->context || !samples || !count ||
        count > SPEECH_SAMPLE_RATE * SPEECH_MAX_SECONDS || !text || !text_count)
        return E_INVALIDARG;
    text[0] = L'\0';
    if (speech_cancelled(cancel))
        return cancellation_error(cancel);
    WhisperFullParams *parameters = backend->api.full_params(0);
    if (!parameters)
        return E_OUTOFMEMORY;
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    parameters->n_threads = system.dwNumberOfProcessors > 4 ? 4 : (int)system.dwNumberOfProcessors;
    if (parameters->n_threads < 1)
        parameters->n_threads = 1;
    parameters->translate = false;
    parameters->no_context = true;
    parameters->no_timestamps = true;
    parameters->single_segment = true;
    parameters->print_special = false;
    parameters->print_progress = false;
    parameters->print_realtime = false;
    parameters->print_timestamps = false;
    parameters->language = "auto";
    parameters->detect_language = false;
    parameters->max_tokens = 256;
    parameters->greedy.best_of = 1;
    parameters->temperature_inc = 0;
    parameters->abort_callback = speech_cancelled;
    parameters->abort_callback_user_data = cancel;
    int decoded = backend->api.transcribe(backend->context, *parameters, samples, (int)count);
    backend->api.free_full_params(parameters);
    if (speech_cancelled(cancel))
        return cancellation_error(cancel);
    if (decoded) {
        swprintf(detail, detail_count, L"Local speech transcription failed");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    int segments = backend->api.segments(backend->context);
    size_t used = 0;
    for (int i = 0; i < segments; ++i) {
        const char *segment = backend->api.segment_text(backend->context, i);
        if (!segment)
            continue;
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, segment, -1, NULL, 0);
        if (length < 1 || (size_t)length > text_count - used ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, segment, -1, text + used, length)) {
            swprintf(detail, detail_count, L"Speech transcript is too long or invalid");
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        used += (size_t)length - 1;
    }
    while (used && iswspace(text[used - 1]))
        text[--used] = L'\0';
    wchar_t *start = text;
    while (iswspace(*start))
        ++start;
    if (start != text)
        memmove(text, start, (wcslen(start) + 1) * sizeof(*text));
    if (!*text) {
        swprintf(detail, detail_count, L"Local speech heard no recognizable words");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

void speech_destroy(SpeechBackend *backend) {
    if (backend->context)
        backend->api.free_context(backend->context);
    if (backend->library)
        FreeLibrary(backend->library);
    *backend = (SpeechBackend){0};
}
