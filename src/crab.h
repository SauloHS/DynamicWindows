#ifndef DYNAMICWINDOWS_CRAB_H
#define DYNAMICWINDOWS_CRAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define WM_DYNAMICWINDOWS_CRAB (WM_APP + 7)
#define CRAB_CREDENTIAL_TARGET L"DynamicWindows/Crab/AnthropicApiKey"

typedef enum {
    CRAB_DISABLED,
    CRAB_IDLE,
    CRAB_LISTENING,
    CRAB_THINKING,
    CRAB_SPEAKING,
    CRAB_ERROR
} CrabState;

typedef struct {
    CrabState state;
    HRESULT error;
    wchar_t transcript[768];
    wchar_t answer[2048];
} CrabMessage;

typedef struct {
    HWND window;
    HANDLE stop;
    HANDLE wake;
    HANDLE thread;
    volatile LONG enabled;
    volatile LONG speak;
    volatile LONG listening;
    volatile LONG busy;
    volatile LONG cancel_request;
    CRITICAL_SECTION lock;
    bool lock_ready;
    void *active_request;
    wchar_t wake_name[64];
    wchar_t model[96];
} CrabService;

bool crab_wake_match(const wchar_t *text, const wchar_t *wake_name, wchar_t *question,
                     size_t count);
bool crab_json_string(const wchar_t *value, char *json, size_t count);
bool crab_response_text(const char *json, wchar_t *answer, size_t count);
bool crab_key_exists(void);
bool crab_key_prompt(HWND parent);
bool crab_key_delete(void);
bool crab_start(CrabService *service, HWND window, bool enabled, bool speak,
                const wchar_t *wake_name, const wchar_t *model);
void crab_configure(CrabService *service, bool enabled, bool speak, const wchar_t *wake_name,
                    const wchar_t *model);
bool crab_listen(CrabService *service);
void crab_cancel(CrabService *service);
void crab_stop(CrabService *service);
const wchar_t *crab_state_text(CrabState state);

#endif
