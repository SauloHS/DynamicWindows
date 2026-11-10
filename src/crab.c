#include "crab.h"
#include "speech.h"

#include <sapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <wincred.h>
#include <winhttp.h>

#define CRAB_MAX_RESPONSE (1024 * 1024)

static const wchar_t user_agent[] = L"DynamicWindows/0.1 Crab";

static CrabTurn *conversation_append(CrabConversation *conversation) {
    if (conversation->count == CRAB_HISTORY) {
        SecureZeroMemory(&conversation->turns[0], sizeof(conversation->turns[0]));
        memmove(conversation->turns, conversation->turns + 1,
                (CRAB_HISTORY - 1) * sizeof(conversation->turns[0]));
        --conversation->count;
    }
    CrabTurn *turn = &conversation->turns[conversation->count++];
    SecureZeroMemory(turn, sizeof(*turn));
    return turn;
}

bool crab_conversation_receive(CrabConversation *conversation, const CrabMessage *message) {
    if (!conversation || !message || conversation->count > CRAB_HISTORY ||
        (conversation->pending && !conversation->count))
        return false;
    CrabTurn *turn = conversation->count ? &conversation->turns[conversation->count - 1] : NULL;
    if (message->state == CRAB_THINKING) {
        if (!message->transcript[0] || conversation->pending)
            return false;
        turn = conversation_append(conversation);
        wcsncpy(turn->transcript, message->transcript, 767);
        turn->state = CRAB_TURN_WAITING;
        conversation->pending = true;
        return true;
    }
    if (message->state == CRAB_SPEAKING || message->state == CRAB_IDLE) {
        if (!message->answer[0] || (message->state == CRAB_IDLE && !conversation->pending))
            return false;
        if (!conversation->pending) {
            bool same_transcript =
                turn && (!message->transcript[0] ||
                         wcsncmp(turn->transcript, message->transcript, 768) == 0);
            if (same_transcript && ((turn->state == CRAB_TURN_ANSWERED &&
                                     wcsncmp(turn->answer, message->answer, 2048) == 0) ||
                                    turn->state == CRAB_TURN_CANCELLED))
                return false;
            turn = conversation_append(conversation);
            wcsncpy(turn->transcript, message->transcript, 767);
        }
        wcsncpy(turn->answer, message->answer, 2047);
        turn->state = CRAB_TURN_ANSWERED;
        turn->error = S_OK;
        conversation->pending = false;
        return true;
    }
    if (message->state != CRAB_ERROR)
        return false;
    if (!conversation->pending) {
        if (turn && turn->state == CRAB_TURN_ERROR && turn->error == message->error &&
            (!message->transcript[0] || wcsncmp(turn->transcript, message->transcript, 768) == 0) &&
            wcsncmp(turn->detail, message->answer, 159) == 0)
            return false;
        turn = conversation_append(conversation);
    }
    if (message->transcript[0]) {
        SecureZeroMemory(turn->transcript, sizeof(turn->transcript));
        wcsncpy(turn->transcript, message->transcript, 767);
    }
    SecureZeroMemory(turn->answer, sizeof(turn->answer));
    SecureZeroMemory(turn->detail, sizeof(turn->detail));
    wcsncpy(turn->detail, message->answer, 159);
    turn->state = CRAB_TURN_ERROR;
    turn->error = message->error;
    conversation->pending = false;
    return true;
}

bool crab_conversation_cancel(CrabConversation *conversation) {
    if (!conversation || !conversation->pending || !conversation->count ||
        conversation->count > CRAB_HISTORY)
        return false;
    CrabTurn *turn = &conversation->turns[conversation->count - 1];
    SecureZeroMemory(turn->answer, sizeof(turn->answer));
    SecureZeroMemory(turn->detail, sizeof(turn->detail));
    turn->state = CRAB_TURN_CANCELLED;
    turn->error = E_ABORT;
    conversation->pending = false;
    return true;
}

void crab_conversation_clear(CrabConversation *conversation) {
    if (conversation)
        SecureZeroMemory(conversation, sizeof(*conversation));
}

const wchar_t *crab_turn_reply(const CrabTurn *turn) {
    if (turn->state == CRAB_TURN_ANSWERED)
        return *turn->answer ? turn->answer : L"No reply received.";
    if (turn->state == CRAB_TURN_ERROR)
        return *turn->detail ? turn->detail : L"The request failed. Try again.";
    return turn->state == CRAB_TURN_CANCELLED ? L"Cancelled" : L"Waiting for a reply…";
}

void crab_message_free(CrabMessage *message) {
    if (!message)
        return;
    SecureZeroMemory(message, sizeof(*message));
    free(message);
}

bool crab_message_current(CrabService *service, const CrabMessage *message) {
    return service && message && !InterlockedCompareExchange(&service->cancel_request, 0, 0) &&
           message->generation == InterlockedCompareExchange(&service->generation, 0, 0);
}

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
    if (!text || !question || count == 0)
        return false;
    question[0] = L'\0';
    while (iswspace(*text))
        ++text;
    if (wake_name && *wake_name) {
        size_t wake_length = wcslen(wake_name);
        if (_wcsnicmp(text, wake_name, wake_length) == 0 && word_boundary(text[wake_length])) {
            text += wake_length;
            while (*text && (iswspace(*text) || iswpunct(*text)))
                ++text;
        }
    }
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

static bool request_string(char *output, size_t count, size_t *used, const wchar_t *value,
                           size_t limit) {
    if (!value)
        return false;
    size_t length = 0;
    while (length < limit && value[length])
        ++length;
    if (length == limit || *used >= count ||
        !crab_json_string(value, output + *used, count - *used))
        return false;
    *used += strlen(output + *used);
    return true;
}

static bool request_message(char *output, size_t count, size_t *used, const char *role,
                            const wchar_t *content, size_t limit, bool comma) {
    if (comma && !append_bytes(output, count, used, ",", 1))
        return false;
    return append_bytes(output, count, used, "{\"role\":\"", 9) &&
           append_bytes(output, count, used, role, strlen(role)) &&
           append_bytes(output, count, used, "\",\"content\":", 12) &&
           request_string(output, count, used, content, limit) &&
           append_bytes(output, count, used, "}", 1);
}

bool crab_request_json(CrabProvider provider, const wchar_t *model, const wchar_t *question,
                       const CrabConversation *conversation, char *output, size_t count) {
    if (!output || !count)
        return false;
    output[0] = '\0';
    if ((provider != CRAB_PROVIDER_ANTHROPIC && provider != CRAB_PROVIDER_OPENAI_COMPATIBLE) ||
        !model || !*model || !question || !*question ||
        (conversation && conversation->count > CRAB_HISTORY))
        return false;
    if (count > CRAB_REQUEST_BYTES)
        count = CRAB_REQUEST_BYTES;
    static const char system[] =
        "\"You are Crab, a concise Windows voice assistant. Answer directly in plain text suitable "
        "for speech. Keep most answers under 120 words. Never claim to have performed an action "
        "you cannot perform.\"";
    static const char anthropic[] =
        ",\"max_tokens\":1024,\"fallbacks\":\"default\",\"output_config\":{\"effort\":\"medium\"},"
        "\"system\":";
    static const char openai[] =
        ",\"max_tokens\":1024,\"stream\":false,\"messages\":[{\"role\":\"system\",\"content\":";
    bool is_anthropic = provider == CRAB_PROVIDER_ANTHROPIC;
    const char *settings = is_anthropic ? anthropic : openai;
    const char *messages = is_anthropic ? ",\"messages\":[" : "}";
    size_t used = 0;
    if (!append_bytes(output, count, &used, "{\"model\":", 9) ||
        !request_string(output, count, &used, model, 96) ||
        !append_bytes(output, count, &used, settings, strlen(settings)) ||
        !append_bytes(output, count, &used, system, sizeof(system) - 1) ||
        !append_bytes(output, count, &used, messages, strlen(messages)))
        goto failed;
    bool comma = !is_anthropic;
    unsigned turns = conversation ? conversation->count : 0;
    for (unsigned i = 0; i < turns; ++i) {
        const CrabTurn *turn = &conversation->turns[i];
        if (turn->state != CRAB_TURN_ANSWERED || !turn->transcript[0] || !turn->answer[0])
            continue;
        if (!request_message(output, count, &used, "user", turn->transcript, 768, comma) ||
            !request_message(output, count, &used, "assistant", turn->answer, 2048, true))
            goto failed;
        comma = true;
    }
    if (!request_message(output, count, &used, "user", question, 768, comma) ||
        !append_bytes(output, count, &used, "]}", 2))
        goto failed;
    return true;
failed:
    output[0] = '\0';
    return false;
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

static void post_state(CrabService *service, LONG generation, CrabState state, HRESULT error,
                       const wchar_t *transcript, const wchar_t *answer) {
    if (!service || !service->window ||
        InterlockedCompareExchange(&service->cancel_request, 0, 0) ||
        generation != InterlockedCompareExchange(&service->generation, 0, 0))
        return;
    CrabMessage *message = calloc(1, sizeof(*message));
    if (!message)
        return;
    message->state = state;
    message->error = error;
    message->generation = generation;
    if (transcript)
        wcsncpy(message->transcript, transcript, 767);
    if (answer)
        wcsncpy(message->answer, answer, 2047);
    if (!PostMessageW(service->window, WM_DYNAMICWINDOWS_CRAB, 0, (LPARAM)message))
        crab_message_free(message);
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
                            size_t count, wchar_t *detail, size_t detail_count) {
    if (InterlockedCompareExchange(&service->cancel_request, 0, 0))
        return E_ABORT;
    CrabProvider provider;
    wchar_t base_url[512];
    wchar_t model_name[96];
    CrabConversation context;
    EnterCriticalSection(&service->lock);
    provider = service->provider;
    wcsncpy(base_url, service->base_url, 511);
    base_url[511] = L'\0';
    wcsncpy(model_name, service->model, 95);
    model_name[95] = L'\0';
    context = service->context;
    LeaveCriticalSection(&service->lock);
    CrabEndpoint endpoint;
    if (!crab_endpoint_parse(provider, base_url, &endpoint) || !*model_name) {
        crab_conversation_clear(&context);
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    }
    char key[CRED_MAX_CREDENTIAL_BLOB_SIZE + 1] = {0};
    bool has_key = key_read(provider, base_url, key, sizeof(key));
    if (!has_key && (provider == CRAB_PROVIDER_ANTHROPIC || endpoint.secure)) {
        crab_conversation_clear(&context);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    char *body = malloc(CRAB_REQUEST_BYTES);
    if (!body) {
        crab_conversation_clear(&context);
        SecureZeroMemory(key, sizeof(key));
        return E_OUTOFMEMORY;
    }
    bool encoded =
        crab_request_json(provider, model_name, question, &context, body, CRAB_REQUEST_BYTES);
    crab_conversation_clear(&context);
    if (!encoded || InterlockedCompareExchange(&service->cancel_request, 0, 0)) {
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(body, CRAB_REQUEST_BYTES);
        free(body);
        if (encoded)
            return E_ABORT;
        swprintf(detail, detail_count, L"Question, model, or conversation cannot be encoded");
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    size_t body_size = strlen(body);
    HINTERNET session = WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session && !WinHttpSetTimeouts(session, 5000, 5000, 10000, 60000)) {
        HRESULT timeout_error = HRESULT_FROM_WIN32(GetLastError());
        WinHttpCloseHandle(session);
        SecureZeroMemory(key, sizeof(key));
        SecureZeroMemory(body, CRAB_REQUEST_BYTES);
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
    if (InterlockedCompareExchange(&service->cancel_request, 0, 0))
        result = E_ABORT;
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
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key, -1, wide_key, key_length)) {
            swprintf(detail, detail_count, L"Saved API key encoding is invalid");
            result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
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
    if (SUCCEEDED(result) && !response_read(request, &response)) {
        swprintf(detail, detail_count, L"Provider response could not be read");
        result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (SUCCEEDED(result) && !crab_response_text(provider, response, answer, count)) {
        swprintf(detail, detail_count, L"Provider response has no readable answer");
        result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    free(response);
    SecureZeroMemory(headers, sizeof(headers));
    if (wide_key) {
        SecureZeroMemory(wide_key, (size_t)key_length * sizeof(wchar_t));
        free(wide_key);
    }
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(body, CRAB_REQUEST_BYTES);
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
    HRESULT initialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    SpeechBackend speech = {0};
    HANDLE events[] = {service->stop, service->wake};
    while (WaitForSingleObject(service->stop, 0) != WAIT_OBJECT_0) {
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0)
            break;
        EnterCriticalSection(&service->lock);
        bool activate = InterlockedCompareExchange(&service->enabled, 0, 0) &&
                        InterlockedExchange(&service->listening, 0);
        LONG generation = InterlockedCompareExchange(&service->generation, 0, 0);
        if (activate)
            InterlockedExchange(&service->busy, 1);
        LeaveCriticalSection(&service->lock);
        if (!activate)
            continue;
        if (FAILED(initialized)) {
            post_state(service, generation, CRAB_ERROR, initialized, NULL, NULL);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        wchar_t transcript[768] = {0};
        wchar_t detail[160] = {0};
        wchar_t speech_model[512];
        EnterCriticalSection(&service->lock);
        wcsncpy(speech_model, service->speech_model, 511);
        speech_model[511] = L'\0';
        LeaveCriticalSection(&service->lock);
        SpeechCancel cancel = {.stop = service->stop, .cancel = &service->cancel_request};
        post_state(service, generation, CRAB_THINKING, S_OK, NULL, NULL);
        HRESULT result = speech_prepare(&speech, speech_model, &cancel, detail, 160);
        float *samples = NULL;
        size_t sample_count = 0;
        if (SUCCEEDED(result)) {
            post_state(service, generation, CRAB_LISTENING, S_OK, NULL, NULL);
            result = speech_capture(&cancel, &samples, &sample_count, detail, 160);
        }
        if (SUCCEEDED(result)) {
            post_state(service, generation, CRAB_THINKING, S_OK, NULL, NULL);
            cancel.deadline = GetTickCount64() + 30000;
            result = speech_transcribe(&speech, &cancel, samples, sample_count, transcript, 768,
                                       detail, 160);
        }
        if (samples) {
            SecureZeroMemory(samples, SPEECH_SAMPLE_RATE * SPEECH_MAX_SECONDS * sizeof(*samples));
            free(samples);
        }
        if (FAILED(result)) {
            if (result != E_ABORT)
                post_state(service, generation, CRAB_ERROR, result, transcript, detail);
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
            swprintf(detail, 160, L"No question heard. Please try again.");
            post_state(service, generation, CRAB_ERROR, HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
                       transcript, detail);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        post_state(service, generation, CRAB_THINKING, S_OK, transcript, NULL);
        wchar_t answer[2048] = {0};
        detail[0] = L'\0';
        result = ask_provider(service, question, answer, 2048, detail, 160);
        if (FAILED(result)) {
            if (result != E_ABORT)
                post_state(service, generation, CRAB_ERROR, result, transcript, detail);
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        post_state(service, generation, CRAB_SPEAKING, S_OK, transcript, answer);
        if (InterlockedCompareExchange(&service->cancel_request, 0, 0)) {
            InterlockedExchange(&service->busy, 0);
            continue;
        }
        if (InterlockedCompareExchange(&service->speak, 0, 0))
            speak_answer(service, answer);
        InterlockedExchange(&service->busy, 0);
        post_state(service, generation, CRAB_IDLE, S_OK, transcript, answer);
    }
    speech_destroy(&speech);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return 0;
}

bool crab_start(CrabService *service, HWND window, bool enabled, bool speak, CrabProvider provider,
                const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model,
                const wchar_t *speech_model) {
    *service = (CrabService){.window = window};
    InitializeCriticalSection(&service->lock);
    service->lock_ready = true;
    service->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    service->wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    crab_configure(service, enabled, speak, provider, wake_name, base_url, model, speech_model);
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
                    const wchar_t *wake_name, const wchar_t *base_url, const wchar_t *model,
                    const wchar_t *speech_model) {
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
    wcsncpy(service->speech_model, speech_model ? speech_model : L"", 511);
    service->speech_model[511] = L'\0';
    if (service->lock_ready)
        LeaveCriticalSection(&service->lock);
    InterlockedExchange(&service->enabled, enabled ? 1 : 0);
    InterlockedExchange(&service->speak, speak ? 1 : 0);
    if (!enabled)
        crab_cancel(service);
}

bool crab_listen(CrabService *service, const CrabConversation *conversation) {
    if (!service || !service->thread || !service->lock_ready ||
        !InterlockedCompareExchange(&service->enabled, 0, 0) ||
        InterlockedCompareExchange(&service->busy, 0, 0) ||
        InterlockedCompareExchange(&service->listening, 0, 0) ||
        (conversation && conversation->count > CRAB_HISTORY))
        return false;
    EnterCriticalSection(&service->lock);
    if (InterlockedCompareExchange(&service->busy, 0, 0) ||
        InterlockedCompareExchange(&service->listening, 0, 0)) {
        LeaveCriticalSection(&service->lock);
        return false;
    }
    crab_conversation_clear(&service->context);
    if (conversation)
        service->context = *conversation;
    InterlockedIncrement(&service->generation);
    InterlockedExchange(&service->listening, 1);
    InterlockedExchange(&service->cancel_request, 0);
    LeaveCriticalSection(&service->lock);
    SetEvent(service->wake);
    return true;
}

void crab_cancel(CrabService *service) {
    if (!service)
        return;
    InterlockedExchange(&service->cancel_request, 1);
    InterlockedIncrement(&service->generation);
    InterlockedExchange(&service->listening, 0);
    request_cancel(service);
    if (service->wake)
        SetEvent(service->wake);
}

void crab_clear_context(CrabService *service) {
    if (!service || !service->lock_ready)
        return;
    EnterCriticalSection(&service->lock);
    crab_conversation_clear(&service->context);
    LeaveCriticalSection(&service->lock);
}

void crab_stop(CrabService *service) {
    if (!service)
        return;
    if (!service->lock_ready)
        return;
    InterlockedExchange(&service->cancel_request, 1);
    InterlockedIncrement(&service->generation);
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
    crab_clear_context(service);
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
