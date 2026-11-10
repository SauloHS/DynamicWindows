#ifndef DYNAMICWINDOWS_WHISPER_API_H
#define DYNAMICWINDOWS_WHISPER_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHISPER_RUNTIME_VERSION "1.8.2"

typedef struct WhisperContext WhisperContext;
typedef struct WhisperState WhisperState;
typedef struct WhisperTokenData WhisperTokenData;
typedef struct WhisperGrammarElement WhisperGrammarElement;

typedef struct {
    int n_text_layer;
    int n_head;
} WhisperAhead;

typedef struct {
    size_t n_heads;
    const WhisperAhead *heads;
} WhisperAheads;

typedef struct {
    bool use_gpu;
    bool flash_attn;
    int gpu_device;
    bool dtw_token_timestamps;
    int dtw_aheads_preset;
    int dtw_n_top;
    WhisperAheads dtw_aheads;
    size_t dtw_mem_size;
} WhisperContextParams;

typedef struct {
    float threshold;
    int min_speech_duration_ms;
    int min_silence_duration_ms;
    float max_speech_duration_s;
    int speech_pad_ms;
    float samples_overlap;
} WhisperVadParams;

typedef struct {
    int strategy;
    int n_threads;
    int n_max_text_ctx;
    int offset_ms;
    int duration_ms;
    bool translate;
    bool no_context;
    bool no_timestamps;
    bool single_segment;
    bool print_special;
    bool print_progress;
    bool print_realtime;
    bool print_timestamps;
    bool token_timestamps;
    float thold_pt;
    float thold_ptsum;
    int max_len;
    bool split_on_word;
    int max_tokens;
    bool debug_mode;
    int audio_ctx;
    bool tdrz_enable;
    const char *suppress_regex;
    const char *initial_prompt;
    bool carry_initial_prompt;
    const int32_t *prompt_tokens;
    int prompt_n_tokens;
    const char *language;
    bool detect_language;
    bool suppress_blank;
    bool suppress_nst;
    float temperature;
    float max_initial_ts;
    float length_penalty;
    float temperature_inc;
    float entropy_thold;
    float logprob_thold;
    float no_speech_thold;
    struct {
        int best_of;
    } greedy;
    struct {
        int beam_size;
        float patience;
    } beam_search;
    void (*new_segment_callback)(WhisperContext *, WhisperState *, int, void *);
    void *new_segment_callback_user_data;
    void (*progress_callback)(WhisperContext *, WhisperState *, int, void *);
    void *progress_callback_user_data;
    bool (*encoder_begin_callback)(WhisperContext *, WhisperState *, void *);
    void *encoder_begin_callback_user_data;
    bool (*abort_callback)(void *);
    void *abort_callback_user_data;
    void (*logits_filter_callback)(WhisperContext *, WhisperState *, const WhisperTokenData *, int,
                                   float *, void *);
    void *logits_filter_callback_user_data;
    const WhisperGrammarElement **grammar_rules;
    size_t n_grammar_rules;
    size_t i_start_rule;
    float grammar_penalty;
    bool vad;
    const char *vad_model_path;
    WhisperVadParams vad_params;
} WhisperFullParams;

typedef struct {
    const char *(*version)(void);
    WhisperContextParams *(*context_params)(void);
    void (*free_context_params)(WhisperContextParams *);
    WhisperContext *(*initialize)(void *, size_t, WhisperContextParams);
    WhisperFullParams *(*full_params)(int);
    void (*free_full_params)(WhisperFullParams *);
    int (*transcribe)(WhisperContext *, WhisperFullParams, const float *, int);
    int (*segments)(WhisperContext *);
    const char *(*segment_text)(WhisperContext *, int);
    void (*free_context)(WhisperContext *);
    void (*log_set)(void (*)(int, const char *, void *), void *);
} WhisperApi;

_Static_assert(sizeof(bool) == 1, "Whisper Boolean ABI");
_Static_assert(sizeof(WhisperContextParams) == 48, "Whisper context ABI");
_Static_assert(offsetof(WhisperFullParams, abort_callback) == 208, "Whisper cancellation ABI");
_Static_assert(sizeof(WhisperFullParams) == 304, "Whisper decoding ABI");

#endif
