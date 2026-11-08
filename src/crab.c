#include "crab.h"

#include <roapi.h>
#include <sapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <wincred.h>
#include <winhttp.h>
#include <winstring.h>

#define CRAB_MAX_RESPONSE (1024 * 1024)

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

typedef INT32 CrabAsyncStatus;
typedef INT32 CrabRecognitionStatus;
ABI_OBJECT(CrabRecognizer);
ABI_OBJECT(CrabCompilationOperation);
ABI_OBJECT(CrabCompilationResult);
ABI_OBJECT(CrabRecognitionOperation);
ABI_OBJECT(CrabRecognitionResult);
ABI_OBJECT(CrabAsyncInfo);
ABI_OBJECT(CrabClosable);

struct CrabRecognizerVtbl {
    ABI_INSPECTABLE(CrabRecognizer);
    HRESULT(STDMETHODCALLTYPE *CurrentLanguage)(CrabRecognizer *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *Constraints)(CrabRecognizer *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *Timeouts)(CrabRecognizer *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *UIOptions)(CrabRecognizer *, IInspectable **);
    HRESULT(STDMETHODCALLTYPE *CompileConstraints)(CrabRecognizer *, CrabCompilationOperation **);
    HRESULT(STDMETHODCALLTYPE *Recognize)(CrabRecognizer *, CrabRecognitionOperation **);
};

struct CrabCompilationOperationVtbl {
    ABI_INSPECTABLE(CrabCompilationOperation);
    HRESULT(STDMETHODCALLTYPE *PutCompleted)(CrabCompilationOperation *, IUnknown *);
    HRESULT(STDMETHODCALLTYPE *GetCompleted)(CrabCompilationOperation *, IUnknown **);
    HRESULT(STDMETHODCALLTYPE *GetResults)(CrabCompilationOperation *, CrabCompilationResult **);
};

struct CrabCompilationResultVtbl {
    ABI_INSPECTABLE(CrabCompilationResult);
    HRESULT(STDMETHODCALLTYPE *Status)(CrabCompilationResult *, CrabRecognitionStatus *);
};

struct CrabRecognitionOperationVtbl {
    ABI_INSPECTABLE(CrabRecognitionOperation);
    HRESULT(STDMETHODCALLTYPE *PutCompleted)(CrabRecognitionOperation *, IUnknown *);
    HRESULT(STDMETHODCALLTYPE *GetCompleted)(CrabRecognitionOperation *, IUnknown **);
    HRESULT(STDMETHODCALLTYPE *GetResults)(CrabRecognitionOperation *, CrabRecognitionResult **);
};

struct CrabRecognitionResultVtbl {
    ABI_INSPECTABLE(CrabRecognitionResult);
    HRESULT(STDMETHODCALLTYPE *Status)(CrabRecognitionResult *, CrabRecognitionStatus *);
    HRESULT(STDMETHODCALLTYPE *Text)(CrabRecognitionResult *, HSTRING *);
};

struct CrabAsyncInfoVtbl {
    ABI_INSPECTABLE(CrabAsyncInfo);
    HRESULT(STDMETHODCALLTYPE *Id)(CrabAsyncInfo *, UINT32 *);
    HRESULT(STDMETHODCALLTYPE *Status)(CrabAsyncInfo *, CrabAsyncStatus *);
    HRESULT(STDMETHODCALLTYPE *Error)(CrabAsyncInfo *, HRESULT *);
    HRESULT(STDMETHODCALLTYPE *Cancel)(CrabAsyncInfo *);
    HRESULT(STDMETHODCALLTYPE *Close)(CrabAsyncInfo *);
};

struct CrabClosableVtbl {
    ABI_INSPECTABLE(CrabClosable);
    HRESULT(STDMETHODCALLTYPE *Close)(CrabClosable *);
};

static const IID crab_recognizer_iid = {
    0x0bc3c9cb, 0xc26a, 0x40f2, {0xae, 0xb5, 0x80, 0x96, 0xb2, 0xe4, 0x80, 0x73}};
static const IID crab_async_info_iid = {
    0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID crab_closable_iid = {
    0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};

#define ABI_SLOT(type, member, slot)                                                               \
    _Static_assert(offsetof(type##Vtbl, member) == (slot) * sizeof(void *), "WinRT ABI slot")
ABI_SLOT(CrabRecognizer, CompileConstraints, 10);
ABI_SLOT(CrabRecognizer, Recognize, 11);
ABI_SLOT(CrabCompilationOperation, GetResults, 8);
ABI_SLOT(CrabCompilationResult, Status, 6);
ABI_SLOT(CrabRecognitionOperation, GetResults, 8);
ABI_SLOT(CrabRecognitionResult, Status, 6);
ABI_SLOT(CrabRecognitionResult, Text, 7);
ABI_SLOT(CrabAsyncInfo, Status, 7);
ABI_SLOT(CrabAsyncInfo, Cancel, 9);
ABI_SLOT(CrabClosable, Close, 6);

static const wchar_t user_agent[] = L"DynamicWindows/0.1 Crab";

static bool endpoint_loopback(const wchar_t *host) {
    return _wcsicmp(host, L"localhost") == 0 || _wcsicmp(host, L"127.0.0.1") == 0 ||
           _wcsicmp(host, L"[::1]") == 0 || _wcsicmp(host, L"::1") == 0;
}

bool crab_endpoint_parse(CrabProvider provider, const wchar_t *base_url, CrabEndpoint *endpoint) {
    if (!endpoint || provider <= CRAB_PROVIDER_NONE || provider > CRAB_PROVIDER_OPENAI_COMPATIBLE ||
        !base_url || !*base_url || wcslen(base_url) >= 512)
        return false;
    *endpoint = (CrabEndpoint){.provider = provider};
    URL_COMPONENTSW parts = {.dwStructSize = sizeof(parts),
                             .dwSchemeLength = (DWORD)-1,
                             .dwHostNameLength = (DWORD)-1,
                             .dwUserNameLength = (DWORD)-1,
                             .dwPasswordLength = (DWORD)-1,
                             .dwUrlPathLength = (DWORD)-1,
                             .dwExtraInfoLength = (DWORD)-1};
    wchar_t url[512];
    wcscpy(url, base_url);
    size_t length = wcslen(url);
    while (length > 0 && url[length - 1] == L'/')
        url[--length] = L'\0';
    if (!WinHttpCrackUrl(url, 0, 0, &parts) || !parts.lpszHostName || !parts.dwHostNameLength ||
        parts.dwHostNameLength >= 256 || parts.dwUserNameLength || parts.dwPasswordLength ||
        parts.dwExtraInfoLength)
        return false;
    endpoint->secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    if (!endpoint->secure && parts.nScheme != INTERNET_SCHEME_HTTP)
        return false;
    wcsncpy(endpoint->host, parts.lpszHostName, parts.dwHostNameLength);
    endpoint->host[parts.dwHostNameLength] = L'\0';
    if (!endpoint->secure && !endpoint_loopback(endpoint->host))
        return false;
    endpoint->port = parts.nPort;
    wchar_t base_path[768] = {0};
    if (parts.lpszUrlPath && parts.dwUrlPathLength) {
        if (parts.dwUrlPathLength >= 768)
            return false;
        wcsncpy(base_path, parts.lpszUrlPath, parts.dwUrlPathLength);
    }
    const wchar_t *suffix =
        provider == CRAB_PROVIDER_ANTHROPIC ? L"/v1/messages" : L"/chat/completions";
    size_t path_length = wcslen(base_path);
    if (provider == CRAB_PROVIDER_ANTHROPIC && path_length >= 3 &&
        _wcsicmp(base_path + path_length - 3, L"/v1") == 0)
        suffix = L"/messages";
    if (provider == CRAB_PROVIDER_OPENAI_COMPATIBLE &&
        !(path_length >= 3 && _wcsicmp(base_path + path_length - 3, L"/v1") == 0))
        suffix = L"/v1/chat/completions";
    if (provider == CRAB_PROVIDER_ANTHROPIC && path_length >= 12 &&
        _wcsicmp(base_path + path_length - 12, L"/v1/messages") == 0)
        suffix = L"";
    if (provider == CRAB_PROVIDER_OPENAI_COMPATIBLE && path_length >= 17 &&
        _wcsicmp(base_path + path_length - 17, L"/chat/completions") == 0)
        suffix = L"";
    if (swprintf(endpoint->path, 1024, L"%ls%ls", *base_path ? base_path : L"", suffix) <= 0)
        return false;
    bool default_port = (endpoint->secure && endpoint->port == INTERNET_DEFAULT_HTTPS_PORT) ||
                        (!endpoint->secure && endpoint->port == INTERNET_DEFAULT_HTTP_PORT);
    if (default_port)
        swprintf(endpoint->normalized, 1280, L"%ls://%ls%ls", endpoint->secure ? L"https" : L"http",
                 endpoint->host, *base_path ? base_path : L"");
    else
        swprintf(endpoint->normalized, 1280, L"%ls://%ls:%u%ls",
                 endpoint->secure ? L"https" : L"http", endpoint->host, endpoint->port,
                 *base_path ? base_path : L"");
    return true;
}

static bool credential_target(CrabProvider provider, const wchar_t *base_url, wchar_t *target,
                              size_t count) {
    CrabEndpoint endpoint;
    if (!crab_endpoint_parse(provider, base_url, &endpoint))
        return false;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const wchar_t *cursor = endpoint.normalized; *cursor; ++cursor) {
        wchar_t value = towlower(*cursor);
        hash ^= (uint16_t)value;
        hash *= UINT64_C(1099511628211);
    }
    return swprintf(target, count, L"DynamicWindows/Crab/%d/%016llx", (int)provider,
                    (unsigned long long)hash) > 0;
}

static bool word_boundary(wchar_t value) {
    return !value || iswspace(value) || iswpunct(value);
}

bool crab_wake_match(const wchar_t *text, const wchar_t *wake_name, wchar_t *question,
                     size_t count) {
    if (!text || !wake_name || !*wake_name || !question || count == 0)
        return false;
    while (iswspace(*text))
        ++text;
    size_t wake_length = wcslen(wake_name);
    if (_wcsnicmp(text, wake_name, wake_length) != 0 || !word_boundary(text[wake_length]))
        return false;
    text += wake_length;
    while (*text && (iswspace(*text) || iswpunct(*text)))
        ++text;
    if (!*text)
        return false;
    wcsncpy(question, text, count - 1);
    question[count - 1] = L'\0';
    size_t length = wcslen(question);
    while (length && iswspace(question[length - 1]))
        question[--length] = L'\0';
    return length > 0;
}

static bool append_bytes(char *output, size_t count, size_t *used, const char *bytes,
                         size_t length) {
    if (*used + length >= count)
        return false;
    memcpy(output + *used, bytes, length);
    *used += length;
    output[*used] = '\0';
    return true;
}

bool crab_json_string(const wchar_t *value, char *json, size_t count) {
    if (!value || !json || count < 3)
        return false;
    size_t used = 0;
    if (!append_bytes(json, count, &used, "\"", 1))
        return false;
    for (; *value; ++value) {
        wchar_t character = *value;
        const char *escape = NULL;
        switch (character) {
        case L'\"':
            escape = "\\\"";
            break;
        case L'\\':
            escape = "\\\\";
            break;
        case L'\b':
            escape = "\\b";
            break;
        case L'\f':
            escape = "\\f";
            break;
        case L'\n':
            escape = "\\n";
            break;
        case L'\r':
            escape = "\\r";
            break;
        case L'\t':
            escape = "\\t";
            break;
        }
        if (escape) {
            if (!append_bytes(json, count, &used, escape, strlen(escape)))
                return false;
            continue;
        }
        if (character < L' ') {
            char encoded[7];
            snprintf(encoded, sizeof(encoded), "\\u%04x", (unsigned)character);
            if (!append_bytes(json, count, &used, encoded, 6))
                return false;
            continue;
        }
        if (character >= 0xd800 && character <= 0xdbff) {
            wchar_t low = value[1];
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            char encoded[4];
            int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, 2, encoded,
                                             sizeof(encoded), NULL, NULL);
            if (length <= 0 || !append_bytes(json, count, &used, encoded, (size_t)length))
                return false;
            ++value;
            continue;
        }
        if (character >= 0xdc00 && character <= 0xdfff)
            return false;
        char encoded[4];
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, 1, encoded,
                                         sizeof(encoded), NULL, NULL);
        if (length <= 0 || !append_bytes(json, count, &used, encoded, (size_t)length))
            return false;
    }
    return append_bytes(json, count, &used, "\"", 1);
}

static const char *skip_space(const char *cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        ++cursor;
    return cursor;
}

static bool hex_digit(char value, unsigned *number) {
    if (value >= '0' && value <= '9')
        *number = (unsigned)(value - '0');
    else if (value >= 'a' && value <= 'f')
        *number = (unsigned)(value - 'a' + 10);
    else if (value >= 'A' && value <= 'F')
        *number = (unsigned)(value - 'A' + 10);
    else
        return false;
    return true;
}

static bool utf8_append(wchar_t *text, size_t count, size_t *used, unsigned codepoint) {
    if (*used + 1 >= count || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return false;
#if WCHAR_MAX <= 0xffff
    if (codepoint > 0xffff) {
        if (*used + 2 >= count)
            return false;
        codepoint -= 0x10000;
        text[(*used)++] = (wchar_t)(0xd800 + (codepoint >> 10));
        text[(*used)++] = (wchar_t)(0xdc00 + (codepoint & 0x3ff));
        text[*used] = L'\0';
        return true;
    }
#endif
    text[(*used)++] = (wchar_t)codepoint;
    text[*used] = L'\0';
    return true;
}

static bool json_decode(const char *start, const char **end, wchar_t *text, size_t count) {
    if (!start || *start != '"' || !text || !count)
        return false;
    size_t used = 0;
    text[0] = L'\0';
    const char *cursor = start + 1;
    while (*cursor && *cursor != '"') {
        if (*cursor == '\\') {
            ++cursor;
            wchar_t character;
            switch (*cursor) {
            case '"':
                character = L'"';
                break;
            case '\\':
                character = L'\\';
                break;
            case '/':
                character = L'/';
                break;
            case 'b':
                character = L'\b';
                break;
            case 'f':
                character = L'\f';
                break;
            case 'n':
                character = L'\n';
                break;
            case 'r':
                character = L'\r';
                break;
            case 't':
                character = L'\t';
                break;
            case 'u': {
                unsigned codepoint = 0;
                for (int i = 1; i <= 4; ++i) {
                    unsigned digit;
                    if (!hex_digit(cursor[i], &digit))
                        return false;
                    codepoint = codepoint * 16 + digit;
                }
                cursor += 4;
                if (!utf8_append(text, count, &used, codepoint))
                    return false;
                ++cursor;
                continue;
            }
            default:
                return false;
            }
            if (!utf8_append(text, count, &used, (unsigned)character))
                return false;
            ++cursor;
            continue;
        }
        int bytes = (*cursor & 0x80) == 0      ? 1
                    : (*cursor & 0xe0) == 0xc0 ? 2
                    : (*cursor & 0xf0) == 0xe0 ? 3
                    : (*cursor & 0xf8) == 0xf0 ? 4
                                               : 0;
        if (!bytes)
            return false;
        wchar_t decoded[2];
        int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cursor, bytes, decoded, 2);
        if (wide <= 0 || used + (size_t)wide >= count)
            return false;
        for (int i = 0; i < wide; ++i)
            text[used++] = decoded[i];
        text[used] = L'\0';
        cursor += bytes;
    }
    if (*cursor != '"')
        return false;
    if (end)
        *end = cursor + 1;
    return true;
}

bool crab_response_text(CrabProvider provider, const char *json, wchar_t *answer, size_t count) {
    if (!json || !answer || count == 0)
        return false;
    answer[0] = L'\0';
    const char *cursor = json;
    if (provider == CRAB_PROVIDER_OPENAI_COMPATIBLE) {
        const char *choices = strstr(json, "\"choices\"");
        const char *message = choices ? strstr(choices, "\"message\"") : NULL;
        const char *content = message ? strstr(message, "\"content\"") : NULL;
        const char *colon = content ? strchr(content + 9, ':') : NULL;
        return colon && json_decode(skip_space(colon + 1), NULL, answer, count);
    }
    if (provider != CRAB_PROVIDER_ANTHROPIC)
        return false;
    while ((cursor = strstr(cursor, "\"type\"")) != NULL) {
        const char *colon = strchr(cursor + 6, ':');
        if (!colon)
            return false;
        colon = skip_space(colon + 1);
        wchar_t type[32];
        const char *after_type = NULL;
        if (!json_decode(colon, &after_type, type, 32))
            return false;
        if (wcscmp(type, L"text") != 0) {
            cursor = after_type;
            continue;
        }
        const char *text_key = strstr(after_type, "\"text\"");
        if (!text_key)
            return false;
        colon = strchr(text_key + 6, ':');
        if (!colon)
            return false;
        colon = skip_space(colon + 1);
        return json_decode(colon, NULL, answer, count);
    }
    return false;
}

bool crab_key_exists(CrabProvider provider, const wchar_t *base_url) {
    if (provider == CRAB_PROVIDER_OPENAI_COMPATIBLE) {
        CrabEndpoint endpoint;
        if (crab_endpoint_parse(provider, base_url, &endpoint) && !endpoint.secure &&
            endpoint_loopback(endpoint.host))
            return true;
    }
    wchar_t target[128];
    if (!credential_target(provider, base_url, target, 128))
        return false;
    PCREDENTIALW credential = NULL;
    bool exists = CredReadW(target, CRED_TYPE_GENERIC, 0, &credential) != FALSE && credential &&
                  credential->CredentialBlobSize > 0;
    if (!exists && provider == CRAB_PROVIDER_ANTHROPIC &&
        _wcsicmp(base_url, L"https://api.anthropic.com") == 0)
        exists =
            CredReadW(CRAB_LEGACY_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0, &credential) != FALSE &&
            credential && credential->CredentialBlobSize > 0;
    if (credential)
        CredFree(credential);
    return exists;
}

static bool key_read(CrabProvider provider, const wchar_t *base_url, char *key, size_t count) {
    wchar_t target[128];
    if (!credential_target(provider, base_url, target, 128))
        return false;
    PCREDENTIALW credential = NULL;
    bool legacy = false;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential) &&
        provider == CRAB_PROVIDER_ANTHROPIC &&
        _wcsicmp(base_url, L"https://api.anthropic.com") == 0) {
        legacy = true;
        (void)CredReadW(CRAB_LEGACY_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0, &credential);
    }
    if (!credential)
        return false;
    size_t size = credential->CredentialBlobSize;
    bool valid = size > 0 && size < count;
    if (valid) {
        memcpy(key, credential->CredentialBlob, size);
        key[size] = '\0';
        if (legacy) {
            CREDENTIALW migrated = {.Type = CRED_TYPE_GENERIC,
                                    .TargetName = target,
                                    .CredentialBlobSize = (DWORD)size,
                                    .CredentialBlob = (BYTE *)key,
                                    .Persist = CRED_PERSIST_LOCAL_MACHINE,
                                    .UserName = L"Crab API key"};
            (void)CredWriteW(&migrated, 0);
        }
    }
    CredFree(credential);
    return valid;
}

bool crab_key_prompt(HWND parent, CrabProvider provider, const wchar_t *base_url) {
    wchar_t target[128];
    if (!credential_target(provider, base_url, target, 128))
        return false;
    wchar_t user[CREDUI_MAX_USERNAME_LENGTH + 1] = L"Crab API key";
    wchar_t password[CREDUI_MAX_PASSWORD_LENGTH + 1] = {0};
    wchar_t message[768];
    swprintf(message, 768,
             L"Enter an API key for %ls at %ls. It is isolated to this endpoint in Windows "
             L"Credential Manager, never settings.ini.",
             crab_provider_text(provider), base_url);
    CREDUI_INFOW info = {.cbSize = sizeof(info),
                         .hwndParent = parent,
                         .pszMessageText = message,
                         .pszCaptionText = L"Connect Crab"};
    BOOL save = TRUE;
    DWORD result =
        CredUIPromptForCredentialsW(&info, target, NULL, 0, user, CREDUI_MAX_USERNAME_LENGTH + 1,
                                    password, CREDUI_MAX_PASSWORD_LENGTH + 1, &save,
                                    CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI |
                                        CREDUI_FLAGS_DO_NOT_PERSIST);
    if (result != NO_ERROR) {
        SecureZeroMemory(password, sizeof(password));
        return false;
    }
    int bytes =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, password, -1, NULL, 0, NULL, NULL);
    char *key = bytes > 1 ? calloc((size_t)bytes, 1) : NULL;
    bool stored = false;
    if (key && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, password, -1, key, bytes, NULL,
                                   NULL) > 0) {
        size_t length = strlen(key);
        CREDENTIALW credential = {.Type = CRED_TYPE_GENERIC,
                                  .TargetName = target,
                                  .CredentialBlobSize = (DWORD)length,
                                  .CredentialBlob = (BYTE *)key,
                                  .Persist = CRED_PERSIST_LOCAL_MACHINE,
                                  .UserName = L"Crab API key"};
        stored = length > 0 && length <= CRED_MAX_CREDENTIAL_BLOB_SIZE &&
                 CredWriteW(&credential, 0) != FALSE;
    }
    SecureZeroMemory(password, sizeof(password));
    if (key) {
        SecureZeroMemory(key, (size_t)bytes);
        free(key);
    }
    return stored;
}

bool crab_key_delete(CrabProvider provider, const wchar_t *base_url) {
    wchar_t target[128];
    if (!credential_target(provider, base_url, target, 128))
        return false;
    bool deleted = CredDeleteW(target, CRED_TYPE_GENERIC, 0) != FALSE;
    DWORD error = deleted ? ERROR_SUCCESS : GetLastError();
    if (provider == CRAB_PROVIDER_ANTHROPIC &&
        _wcsicmp(base_url, L"https://api.anthropic.com") == 0) {
        if (CredDeleteW(CRAB_LEGACY_CREDENTIAL_TARGET, CRED_TYPE_GENERIC, 0))
            deleted = true;
        else if (GetLastError() != ERROR_NOT_FOUND)
            return false;
    }
    return deleted || error == ERROR_NOT_FOUND;
}

static void request_cancel(CrabService *service) {
    if (!service || !service->lock_ready)
        return;
    EnterCriticalSection(&service->lock);
    HINTERNET request = (HINTERNET)service->active_request;
    service->active_request = NULL;
    LeaveCriticalSection(&service->lock);
    if (request)
        WinHttpCloseHandle(request);
}

static void post_state(CrabService *service, CrabState state, HRESULT error,
                       const wchar_t *transcript, const wchar_t *answer) {
    if (!service || !service->window ||
        (state != CRAB_LISTENING && InterlockedCompareExchange(&service->cancel_request, 0, 0)))
        return;
    CrabMessage *message = calloc(1, sizeof(*message));
    if (!message)
        return;
    message->state = state;
    message->error = error;
    if (transcript)
        wcsncpy(message->transcript, transcript, 767);
    if (answer)
        wcsncpy(message->answer, answer, 2047);
    if (!PostMessageW(service->window, WM_DYNAMICWINDOWS_CRAB, 0, (LPARAM)message))
        free(message);
}

static HRESULT modern_wait(CrabService *service, IUnknown *operation, DWORD timeout) {
    CrabAsyncInfo *info = NULL;
    HRESULT result = IUnknown_QueryInterface(operation, &crab_async_info_iid, (void **)&info);
    if (FAILED(result))
        return result;
    uint64_t deadline = GetTickCount64() + timeout;
    CrabAsyncStatus status = 0;
    while (GetTickCount64() < deadline) {
        result = info->lpVtbl->Status(info, &status);
        if (FAILED(result) || status != 0)
            break;
        if (WaitForSingleObject(service->stop, 20) == WAIT_OBJECT_0 ||
            InterlockedCompareExchange(&service->cancel_request, 0, 0)) {
            info->lpVtbl->Cancel(info);
            result = E_ABORT;
            break;
        }
    }
    if (SUCCEEDED(result) && status == 3) {
        HRESULT error = E_FAIL;
        HRESULT error_result = info->lpVtbl->Error(info, &error);
        result = FAILED(error_result) ? error_result : error;
    } else if (SUCCEEDED(result) && status == 2) {
        result = E_ABORT;
    } else if (SUCCEEDED(result) && status == 0) {
        info->lpVtbl->Cancel(info);
        result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    info->lpVtbl->Release(info);
    return result;
}

static void modern_close(IUnknown *operation) {
    CrabAsyncInfo *info = NULL;
    if (SUCCEEDED(IUnknown_QueryInterface(operation, &crab_async_info_iid, (void **)&info))) {
        info->lpVtbl->Close(info);
        info->lpVtbl->Release(info);
    }
}

static HRESULT modern_compile(CrabService *service, CrabRecognizer *recognizer) {
    CrabCompilationOperation *operation = NULL;
    CrabCompilationResult *compilation = NULL;
    HRESULT result = recognizer->lpVtbl->CompileConstraints(recognizer, &operation);
    if (SUCCEEDED(result) && !operation)
        result = E_FAIL;
    if (SUCCEEDED(result))
        result = modern_wait(service, (IUnknown *)operation, 12000);
    if (SUCCEEDED(result))
        result = operation->lpVtbl->GetResults(operation, &compilation);
    if (SUCCEEDED(result) && compilation) {
        CrabRecognitionStatus status = 6;
        result = compilation->lpVtbl->Status(compilation, &status);
        if (SUCCEEDED(result) && status != 0)
            result = status == 5                  ? E_ABORT
                     : status == 7 || status == 8 ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                     : status == 10               ? HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE)
                                                  : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (compilation)
        compilation->lpVtbl->Release(compilation);
    if (operation) {
        modern_close((IUnknown *)operation);
        operation->lpVtbl->Release(operation);
    }
    return result;
}

static HRESULT modern_operation_wait(CrabService *service, CrabRecognitionOperation *operation,
                                     DWORD timeout, CrabRecognitionResult **recognition) {
    HRESULT result = modern_wait(service, (IUnknown *)operation, timeout);
    if (SUCCEEDED(result))
        result = operation->lpVtbl->GetResults(operation, recognition);
    return result;
}

static HRESULT modern_listen_once(CrabService *service, wchar_t *text, size_t count) {
    static const wchar_t class_name[] = L"Windows.Media.SpeechRecognition.SpeechRecognizer";
    HSTRING name = NULL;
    HRESULT result = WindowsCreateString(
        class_name, (UINT32)(sizeof(class_name) / sizeof(class_name[0]) - 1), &name);
    IInspectable *instance = NULL;
    CrabRecognizer *recognizer = NULL;
    CrabRecognitionOperation *operation = NULL;
    CrabRecognitionResult *recognition = NULL;
    if (SUCCEEDED(result))
        result = RoActivateInstance(name, &instance);
    if (SUCCEEDED(result))
        result = IInspectable_QueryInterface(instance, &crab_recognizer_iid, (void **)&recognizer);
    if (SUCCEEDED(result))
        result = modern_compile(service, recognizer);
    if (SUCCEEDED(result))
        result = recognizer->lpVtbl->Recognize(recognizer, &operation);
    if (SUCCEEDED(result) && !operation)
        result = E_FAIL;
    if (SUCCEEDED(result))
        result = modern_operation_wait(service, operation, 12000, &recognition);
    if (SUCCEEDED(result) && recognition) {
        CrabRecognitionStatus status = 6;
        result = recognition->lpVtbl->Status(recognition, &status);
        if (SUCCEEDED(result) && status == 0) {
            HSTRING value = NULL;
            result = recognition->lpVtbl->Text(recognition, &value);
            if (SUCCEEDED(result) && value) {
                UINT32 length = 0;
                const wchar_t *source = WindowsGetStringRawBuffer(value, &length);
                size_t copied = source && length < count - 1 ? length : source ? count - 1 : 0;
                if (copied)
                    wmemcpy(text, source, copied);
                text[copied] = L'\0';
                WindowsDeleteString(value);
            }
        } else if (SUCCEEDED(result)) {
            result = status == 5                  ? E_ABORT
                     : status == 7 || status == 8 ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                     : status == 10               ? HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_AVAILABLE)
                                                  : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    if (recognition)
        recognition->lpVtbl->Release(recognition);
    if (operation) {
        modern_close((IUnknown *)operation);
        operation->lpVtbl->Release(operation);
    }
    if (recognizer) {
        CrabClosable *closable = NULL;
        if (SUCCEEDED(recognizer->lpVtbl->QueryInterface(recognizer, &crab_closable_iid,
                                                         (void **)&closable))) {
            closable->lpVtbl->Close(closable);
            closable->lpVtbl->Release(closable);
        }
        recognizer->lpVtbl->Release(recognizer);
    }
    if (instance)
        IInspectable_Release(instance);
    if (name)
        WindowsDeleteString(name);
    return *text ? result : FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}
static bool response_read(HINTERNET request, char **response) {
    *response = NULL;
    size_t used = 0;
    size_t capacity = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            goto failed;
        if (!available)
            break;
        if (used + available + 1 > CRAB_MAX_RESPONSE)
            goto failed;
        size_t needed = used + available + 1;
        if (needed > capacity) {
            size_t next = capacity ? capacity : 4096;
            while (next < needed)
                next *= 2;
            char *grown = realloc(*response, next);
            if (!grown)
                goto failed;
            *response = grown;
            capacity = next;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, *response + used, available, &read))
            goto failed;
        used += read;
        if (!read)
            break;
    }
    if (!*response)
        *response = calloc(1, 1);
    else
        (*response)[used] = '\0';
    return *response != NULL;
failed:
    free(*response);
    *response = NULL;
    return false;
}

static HRESULT ask_provider(CrabService *service, const wchar_t *question, wchar_t *answer,
                            size_t count) {
    if (InterlockedCompareExchange(&service->cancel_request, 0, 0))
        return E_ABORT;
    CrabProvider provider;
    wchar_t base_url[512];
    wchar_t model_name[96];
    EnterCriticalSection(&service->lock);
    provider = service->provider;
    wcsncpy(base_url, service->base_url, 511);
    base_url[511] = L'\0';
    wcsncpy(model_name, service->model, 95);
    model_name[95] = L'\0';
    LeaveCriticalSection(&service->lock);
    CrabEndpoint endpoint;
    if (!crab_endpoint_parse(provider, base_url, &endpoint) || !*model_name)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    char key[CRED_MAX_CREDENTIAL_BLOB_SIZE + 1] = {0};
    bool has_key = key_read(provider, base_url, key, sizeof(key));
    if (!has_key && (provider == CRAB_PROVIDER_ANTHROPIC || endpoint.secure))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    char prompt[8192];
    char model[256];
    if (!crab_json_string(question, prompt, sizeof(prompt)) ||
        !crab_json_string(model_name, model, sizeof(model))) {
        SecureZeroMemory(key, sizeof(key));
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    static const char anthropic_format[] =
        "{\"model\":%s,\"max_tokens\":1024,\"fallbacks\":\"default\","
        "\"output_config\":{\"effort\":\"medium\"},"
        "\"system\":\"You are Crab, a concise Windows voice assistant. Answer directly in "
        "plain text suitable for speech. Keep most answers under 120 words. Never claim to have "
        "performed an action you cannot perform.\",\"messages\":[{\"role\":\"user\","
        "\"content\":%s}]}";
    static const char openai_format[] =
        "{\"model\":%s,\"max_tokens\":1024,\"stream\":false,\"messages\":["
        "{\"role\":\"system\",\"content\":\"You are Crab, a concise Windows voice "
        "assistant. Answer directly in plain text suitable for speech. Keep most answers under "
        "120 words. Never claim to have performed an action you cannot perform.\"},"
        "{\"role\":\"user\",\"content\":%s}]}";
    const char *format = provider == CRAB_PROVIDER_ANTHROPIC ? anthropic_format : openai_format;
    int body_size = snprintf(NULL, 0, format, model, prompt);
    char *body = body_size > 0 ? malloc((size_t)body_size + 1) : NULL;
    if (!body) {
        SecureZeroMemory(key, sizeof(key));
        return E_OUTOFMEMORY;
    }
    snprintf(body, (size_t)body_size + 1, format, model, prompt);
    HINTERNET session = WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session && !WinHttpSetTimeouts(session, 5000, 5000, 10000, 60000)) {
        HRESULT timeout_error = HRESULT_FROM_WIN32(GetLastError());
        WinHttpCloseHandle(session);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(body, (size_t)body_size + 1);
        free(body);
        return timeout_error;
    }
    HINTERNET connection =
        session ? WinHttpConnect(session, endpoint.host, endpoint.port, 0) : NULL;
    HINTERNET request = connection
                            ? WinHttpOpenRequest(connection, L"POST", endpoint.path, NULL,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 endpoint.secure ? WINHTTP_FLAG_SECURE : 0)
                            : NULL;
    if (service->lock_ready)
        EnterCriticalSection(&service->lock);
    service->active_request = request;
    if (service->lock_ready)
        LeaveCriticalSection(&service->lock);
    HRESULT result = request ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (SUCCEEDED(result) && !WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                                               &redirect_policy, sizeof(redirect_policy)))
        result = HRESULT_FROM_WIN32(GetLastError());
    wchar_t headers[1800] = L"Content-Type: application/json\r\n";
    wchar_t *wide_key = NULL;
    int key_length = 0;
    if (has_key) {
        key_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key, -1, NULL, 0);
        wide_key = key_length > 1 ? calloc((size_t)key_length, sizeof(wchar_t)) : NULL;
        if (!wide_key ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key, -1, wide_key, key_length))
            result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(result) && provider == CRAB_PROVIDER_ANTHROPIC)
        swprintf(headers, sizeof(headers) / sizeof(headers[0]),
                 L"Content-Type: application/json\r\nx-api-key: %ls\r\nanthropic-version: "
                 L"2026-10-21\r\nanthropic-beta: server-side-fallback-2026-07-01\r\n",
                 wide_key);
    else if (SUCCEEDED(result) && has_key)
        swprintf(headers, sizeof(headers) / sizeof(headers[0]),
                 L"Content-Type: application/json\r\nAuthorization: Bearer %ls\r\n", wide_key);
    if (SUCCEEDED(result) && (!WinHttpSendRequest(request, headers, (DWORD)-1L, body,
                                                  (DWORD)body_size, (DWORD)body_size, 0) ||
                              !WinHttpReceiveResponse(request, NULL)))
        result = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(result) && InterlockedCompareExchange(&service->cancel_request, 0, 0))
        result = E_ABORT;
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (SUCCEEDED(result) &&
        (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL,
                              &status, &status_size, NULL) ||
         status < 200 || status >= 300))
        result = HRESULT_FROM_WIN32(status ? status : GetLastError());
    char *response = NULL;
    if (SUCCEEDED(result) && (!response_read(request, &response) ||
                              !crab_response_text(provider, response, answer, count)))
        result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    free(response);
    SecureZeroMemory(headers, sizeof(headers));
    if (wide_key) {
        SecureZeroMemory(wide_key, (size_t)key_length * sizeof(wchar_t));
        free(wide_key);
    }
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(body, (size_t)body_size + 1);
    free(body);
    bool own_request = true;
    if (service->lock_ready)
        EnterCriticalSection(&service->lock);
    if (service->active_request == request)
        service->active_request = NULL;
    else
        own_request = false;
    if (service->lock_ready)
        LeaveCriticalSection(&service->lock);
    if (own_request && request)
        WinHttpCloseHandle(request);
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
    return result;
}

static void speak_answer(CrabService *service, const wchar_t *answer) {
    ISpVoice *voice = NULL;
    if (SUCCEEDED(
            CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void **)&voice))) {
        if (SUCCEEDED(ISpVoice_Speak(voice, answer, SPF_IS_NOT_XML | SPF_ASYNC, NULL))) {
            HANDLE complete = ISpVoice_SpeakCompleteEvent(voice);
            HANDLE events[] = {service->stop, service->wake, complete};
            while (complete && complete != INVALID_HANDLE_VALUE) {
                DWORD wait = WaitForMultipleObjects(3, events, FALSE, 100);
                if (wait == WAIT_OBJECT_0 ||
                    InterlockedCompareExchange(&service->cancel_request, 0, 0)) {
                    ISpVoice_Speak(voice, L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
                    break;
                }
                if (wait == WAIT_OBJECT_0 + 2 || wait == WAIT_FAILED)
                    break;
            }
        }
        ISpVoice_Release(voice);
    }
}

static DWORD WINAPI crab_thread(void *argument) {
    CrabService *service = argument;
    HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    HRESULT speech = initialized;
    HANDLE events[] = {service->stop, service->wake};
    while (WaitForSingleObject(service->stop, 0) != WAIT_OBJECT_0) {
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0)
            break;
        if (!InterlockedCompareExchange(&service->enabled, 0, 0) ||
            !InterlockedExchange(&service->listening, 0))
            continue;
        InterlockedExchange(&service->busy, 1);
        InterlockedExchange(&service->cancel_request, 0);
        if (FAILED(speech)) {
            post_state(service, CRAB_ERROR, speech, NULL, NULL);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        post_state(service, CRAB_LISTENING, S_OK, NULL, NULL);
        wchar_t transcript[768] = {0};
        HRESULT result = modern_listen_once(service, transcript, 768);
        if (FAILED(result)) {
            if (result != E_ABORT)
                post_state(service, CRAB_ERROR, result, transcript, NULL);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        wchar_t question[768];
        wchar_t wake_name[64];
        EnterCriticalSection(&service->lock);
        wcsncpy(wake_name, service->wake_name, 63);
        wake_name[63] = L'\0';
        LeaveCriticalSection(&service->lock);
        if (!crab_wake_match(transcript, wake_name, question, 768)) {
            post_state(service, CRAB_ERROR, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), transcript,
                       L"Start with the configured wake name, then ask a question.");
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        post_state(service, CRAB_THINKING, S_OK, transcript, NULL);
        wchar_t answer[2048] = {0};
        result = ask_provider(service, question, answer, 2048);
        if (FAILED(result)) {
            if (result != E_ABORT)
                post_state(service, CRAB_ERROR, result, transcript, NULL);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        post_state(service, CRAB_SPEAKING, S_OK, transcript, answer);
        if (InterlockedCompareExchange(&service->cancel_request, 0, 0)) {
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        if (InterlockedCompareExchange(&service->speak, 0, 0))
            speak_answer(service, answer);
        InterlockedExchange(&service->busy, 0);
        post_state(service, CRAB_IDLE, S_OK, transcript, answer);
    }
    if (SUCCEEDED(initialized))
        RoUninitialize();
    return 0;
}

bool crab_start(CrabService *service, HWND window, bool enabled, bool speak, CrabProvider provider,
                const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model) {
    *service = (CrabService){.window = window};
    InitializeCriticalSection(&service->lock);
    service->lock_ready = true;
    service->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    service->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    crab_configure(service, enabled, speak, provider, wake_name, base_url, model);
    if (service->stop && service->wake)
        service->thread = CreateThread(NULL, 0, crab_thread, service, 0, NULL);
    if (service->thread)
        return true;
    DWORD error = GetLastError();
    crab_stop(service);
    SetLastError(error);
    return false;
}

void crab_configure(CrabService *service, bool enabled, bool speak, CrabProvider provider,
                    const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model) {
    if (!service)
        return;
    if (service->lock_ready)
        EnterCriticalSection(&service->lock);
    service->provider = provider;
    wcsncpy(service->wake_name, wake_name && *wake_name ? wake_name : L"Crab", 63);
    service->wake_name[63] = L'\0';
    wcsncpy(service->base_url, base_url ? base_url : L"", 511);
    service->base_url[511] = L'\0';
    wcsncpy(service->model, model ? model : L"", 95);
    service->model[95] = L'\0';
    if (service->lock_ready)
        LeaveCriticalSection(&service->lock);
    InterlockedExchange(&service->enabled, enabled ? 1 : 0);
    InterlockedExchange(&service->speak, speak ? 1 : 0);
    if (!enabled)
        crab_cancel(service);
}

bool crab_listen(CrabService *service) {
    if (!service || !service->thread || !InterlockedCompareExchange(&service->enabled, 0, 0) ||
        InterlockedCompareExchange(&service->busy, 0, 0) ||
        InterlockedCompareExchange(&service->listening, 0, 0))
        return false;
    InterlockedExchange(&service->listening, 1);
    InterlockedExchange(&service->cancel_request, 0);
    SetEvent(service->wake);
    return true;
}

void crab_cancel(CrabService *service) {
    if (!service)
        return;
    InterlockedExchange(&service->cancel_request, 1);
    InterlockedExchange(&service->listening, 0);
    request_cancel(service);
    if (service->wake)
        SetEvent(service->wake);
}

void crab_stop(CrabService *service) {
    if (!service)
        return;
    if (!service->lock_ready)
        return;
    InterlockedExchange(&service->cancel_request, 1);
    InterlockedExchange(&service->listening, 0);
    request_cancel(service);
    if (service->stop)
        SetEvent(service->stop);
    if (service->wake)
        SetEvent(service->wake);
    if (service->thread) {
        WaitForSingleObject(service->thread, INFINITE);
        CloseHandle(service->thread);
    }
    if (service->wake)
        CloseHandle(service->wake);
    if (service->stop)
        CloseHandle(service->stop);
    if (service->lock_ready)
        DeleteCriticalSection(&service->lock);
    *service = (CrabService){0};
}

const wchar_t *crab_provider_text(CrabProvider provider) {
    switch (provider) {
    case CRAB_PROVIDER_ANTHROPIC:
        return L"Anthropic Messages";
    case CRAB_PROVIDER_OPENAI_COMPATIBLE:
        return L"OpenAI-compatible Chat Completions";
    case CRAB_PROVIDER_NONE:
    default:
        return L"No provider";
    }
}

const wchar_t *crab_state_text(CrabState state) {
    switch (state) {
    case CRAB_LISTENING:
        return L"Listening";
    case CRAB_THINKING:
        return L"Thinking";
    case CRAB_SPEAKING:
        return L"Speaking";
    case CRAB_ERROR:
        return L"Crab needs attention";
    case CRAB_DISABLED:
        return L"Crab is disabled";
    case CRAB_IDLE:
    default:
        return L"Crab is ready";
    }
}
