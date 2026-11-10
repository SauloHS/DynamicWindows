#ifndef DYNAMICWINDOWS_SPEECH_H
#define DYNAMICWINDOWS_SPEECH_H

#include "whisper_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define SPEECH_SAMPLE_RATE 16000
#define SPEECH_MAX_SECONDS 12

typedef struct {
    size_t samples;
    size_t last_voice;
    size_t voiced_samples;
} SpeechActivity;

typedef struct {
    HANDLE stop;
    volatile LONG *cancel;
    uint64_t deadline;
} SpeechCancel;

typedef struct {
    HMODULE library;
    WhisperApi api;
    WhisperContext *context;
    wchar_t model_path[1024];
} SpeechBackend;

bool speech_activity_update(SpeechActivity *activity, const float *samples, size_t count);
HRESULT speech_path(const wchar_t *path, wchar_t *absolute, size_t count);
HRESULT speech_prepare(SpeechBackend *backend, const wchar_t *model_path, SpeechCancel *cancel,
                       wchar_t *detail, size_t detail_count);
HRESULT speech_capture(SpeechCancel *cancel, float **samples, size_t *count, wchar_t *detail,
                       size_t detail_count);
HRESULT speech_transcribe(SpeechBackend *backend, SpeechCancel *cancel, const float *samples,
                          size_t count, wchar_t *text, size_t text_count, wchar_t *detail,
                          size_t detail_count);
void speech_destroy(SpeechBackend *backend);

#endif
