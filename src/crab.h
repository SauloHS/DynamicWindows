#ifndef DYNAMICWINDOWS_CRAB_H
#define DYNAMICWINDOWS_CRAB_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define WM_DYNAMICWINDOWS_CRAB (WM_APP + 7)
#define CRAB_LEGACY_CREDENTIAL_TARGET L"DynamicWindows/Crab/AnthropicApiKey"

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
    CrabProvider provider;
    bool secure;
    unsigned short port;
    wchar_t host[256];
    wchar_t path[1024];
    wchar_t normalized[1280];
} CrabEndpoint;

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
    CrabProvider provider;
    wchar_t wake_name[64];
    wchar_t base_url[512];
    wchar_t model[96];
} CrabService;

bool crab_wake_match(const wchar_t *text, const wchar_t *wake_name, wchar_t *question,
                     size_t count);
bool crab_json_string(const wchar_t *value, char *json, size_t count);
bool crab_response_text(CrabProvider provider, const char *json, wchar_t *answer, size_t count);
bool crab_endpoint_parse(CrabProvider provider, const wchar_t *base_url, CrabEndpoint *endpoint);
bool crab_key_exists(CrabProvider provider, const wchar_t *base_url);
bool crab_key_prompt(HWND parent, CrabProvider provider, const wchar_t *base_url);
bool crab_key_delete(CrabProvider provider, const wchar_t *base_url);
bool crab_start(CrabService *service, HWND window, bool enabled, bool speak, CrabProvider provider,
                const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model);
void crab_configure(CrabService *service, bool enabled, bool speak, CrabProvider provider,
                    const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model);
bool crab_listen(CrabService *service);
void crab_cancel(CrabService *service);
void crab_stop(CrabService *service);
const wchar_t *crab_state_text(CrabState state);
const wchar_t *crab_provider_text(CrabProvider provider);

#endif
