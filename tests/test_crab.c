#include "crab.h"
#include "speech.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static unsigned checks;

#define CHECK(value)                                                                               \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(value)) {                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void test_wake(void) {
    wchar_t question[128];
    CHECK(crab_wake_match(L"Crab what's 1 + 1", L"Crab", question, 128));
    CHECK(wcscmp(question, L"what's 1 + 1") == 0);
    CHECK(crab_wake_match(L"  Siri, explain gravity  ", L"Siri", question, 128));
    CHECK(wcscmp(question, L"explain gravity") == 0);
    CHECK(crab_wake_match(L"  What's one plus one?  ", L"Crab", question, 128));
    CHECK(wcscmp(question, L"What's one plus one?") == 0);
    CHECK(crab_wake_match(L"Quanto \x00e9 um mais um?", L"Crab", question, 128));
    CHECK(wcscmp(question, L"Quanto \x00e9 um mais um?") == 0);
    CHECK(crab_wake_match(L"crab, what's one plus one?", L"Crab", question, 128));
    CHECK(wcscmp(question, L"what's one plus one?") == 0);
    CHECK(crab_wake_match(L"Crabapple question", L"Crab", question, 128));
    CHECK(wcscmp(question, L"Crabapple question") == 0);
    CHECK(crab_wake_match(L"Why?", NULL, question, 128));
    CHECK(wcscmp(question, L"Why?") == 0);
    CHECK(crab_wake_match(L"Why?", L"", question, 128));
    CHECK(!crab_wake_match(L"Crab", L"Crab", question, 128));
    CHECK(!crab_wake_match(L"Crab, ", L"Crab", question, 128));
    CHECK(!crab_wake_match(L" \t", L"Crab", question, 128));
    CHECK(!crab_wake_match(L"", L"Crab", question, 128));
    CHECK(!crab_wake_match(NULL, L"Crab", question, 128));
    CHECK(!crab_wake_match(L"Why?", L"Crab", question, 1));
}

static void test_json(void) {
    char json[256];
    CHECK(crab_json_string(L"Hello \"Crab\"\nSnowman: \x2603", json, sizeof(json)));
    CHECK(strstr(json, "\\\"Crab\\\"") != NULL);
    CHECK(strstr(json, "\\n") != NULL);
    CHECK(strstr(json, "Snowman") != NULL);
    CHECK(crab_json_string(L"Emoji \xd83d\xde80", json, sizeof(json)));
    CHECK(strstr(json, "Emoji") != NULL);
    CHECK(json[0] == '"' && json[strlen(json) - 1] == '"');
    CHECK(!crab_json_string(L"too long", json, 4));
}

static void test_response(void) {
    wchar_t answer[256];
    const char *anthropic = "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"},"
                            "{\"type\":\"text\",\"text\":\"Two.\\nSimple \\\"answer\\\".\"}],"
                            "\"stop_reason\":\"end_turn\"}";
    CHECK(crab_response_text(CRAB_PROVIDER_ANTHROPIC, anthropic, answer, 256));
    CHECK(wcscmp(answer, L"Two.\nSimple \"answer\".") == 0);
    const char *openai = "{\"choices\":[{\"message\":{\"role\":\"assistant\","
                         "\"content\":\"Forty-two.\"},\"finish_reason\":\"stop\"}]}";
    CHECK(crab_response_text(CRAB_PROVIDER_OPENAI_COMPATIBLE, openai, answer, 256));
    CHECK(wcscmp(answer, L"Forty-two.") == 0);
    CHECK(crab_response_text(CRAB_PROVIDER_ANTHROPIC,
                             "{\"type\":\"text\",\"text\":\"Unicode \\u2603\"}", answer, 256));
    CHECK(wcsstr(answer, L"Unicode") != NULL);
    CHECK(!crab_response_text(CRAB_PROVIDER_NONE, openai, answer, 256));
    CHECK(!crab_response_text(CRAB_PROVIDER_ANTHROPIC, "{\"content\":[]}", answer, 256));
}

static void test_endpoints(void) {
    CrabEndpoint endpoint;
    CHECK(crab_endpoint_parse(CRAB_PROVIDER_ANTHROPIC, L"https://api.anthropic.com", &endpoint));
    CHECK(endpoint.secure);
    CHECK(wcscmp(endpoint.host, L"api.anthropic.com") == 0);
    CHECK(wcscmp(endpoint.path, L"/v1/messages") == 0);
    CHECK(crab_endpoint_parse(CRAB_PROVIDER_ANTHROPIC, L"https://api.anthropic.com/v1", &endpoint));
    CHECK(wcscmp(endpoint.path, L"/v1/messages") == 0);
    CHECK(crab_endpoint_parse(CRAB_PROVIDER_OPENAI_COMPATIBLE, L"https://api.openai.com/v1/",
                              &endpoint));
    CHECK(wcscmp(endpoint.path, L"/v1/chat/completions") == 0);
    CHECK(crab_endpoint_parse(CRAB_PROVIDER_OPENAI_COMPATIBLE, L"http://127.0.0.1:11434/v1",
                              &endpoint));
    CHECK(!endpoint.secure);
    CHECK(endpoint.port == 11434);
    CHECK(wcscmp(endpoint.path, L"/v1/chat/completions") == 0);
    CHECK(
        !crab_endpoint_parse(CRAB_PROVIDER_OPENAI_COMPATIBLE, L"http://example.com/v1", &endpoint));
    CHECK(!crab_endpoint_parse(CRAB_PROVIDER_OPENAI_COMPATIBLE,
                               L"https://user:secret@example.com/v1", &endpoint));
    CHECK(!crab_endpoint_parse(CRAB_PROVIDER_OPENAI_COMPATIBLE,
                               L"https://example.com/v1?token=secret", &endpoint));
    CHECK(!crab_endpoint_parse(CRAB_PROVIDER_NONE, L"https://example.com", &endpoint));
}

static void test_speech_activity(void) {
    SpeechActivity activity = {0};
    float quiet[1600] = {0};
    float voice[1600];
    for (size_t i = 0; i < 1600; ++i)
        voice[i] = i % 2 ? 0.05f : -0.05f;
    CHECK(!speech_activity_update(&activity, quiet, 1600));
    CHECK(activity.voiced_samples == 0);
    CHECK(!speech_activity_update(&activity, voice, 1600));
    CHECK(!speech_activity_update(&activity, voice, 1600));
    CHECK(activity.voiced_samples == 3200);
    for (int i = 0; i < 14; ++i)
        CHECK(!speech_activity_update(&activity, quiet, 1600));
    CHECK(speech_activity_update(&activity, quiet, 1600));
    CHECK(!speech_activity_update(&activity, voice, 1600));
    CHECK(activity.last_voice == activity.samples);
    activity = (SpeechActivity){0};
    for (int i = 0; i < 150; ++i)
        CHECK(!speech_activity_update(&activity, quiet, 1600));
    CHECK(activity.voiced_samples == 0);
}

static void test_speech_paths(void) {
    wchar_t path[1024];
    CHECK(SUCCEEDED(speech_path(L"C:\\Voice models\\\x00e7\\tiny.bin", path, 1024)));
    CHECK(wcscmp(path, L"C:\\Voice models\\\x00e7\\tiny.bin") == 0);
    CHECK(SUCCEEDED(speech_path(L"speech\\ggml-tiny.bin", path, 1024)));
    CHECK(wcsstr(path, L"\\speech\\ggml-tiny.bin") != NULL);
    CHECK(FAILED(speech_path(L"", path, 1024)));
    CHECK(FAILED(speech_path(L"speech\\ggml-tiny.bin", path, 4)));
}

static bool bytes_zero(const void *buffer, size_t count) {
    const unsigned char *bytes = buffer;
    for (size_t i = 0; i < count; ++i)
        if (bytes[i])
            return false;
    return true;
}

static void test_conversation(void) {
    CrabConversation conversation = {0};
    CrabMessage message = {.state = CRAB_LISTENING, .transcript = L"not transcribed"};
    CHECK(!crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_THINKING};
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 0 && !conversation.pending);
    message = (CrabMessage){.state = CRAB_THINKING, .transcript = L"  Crab, same question?  "};
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 1 && conversation.pending);
    CHECK(conversation.turns[0].state == CRAB_TURN_WAITING);
    CHECK(wcscmp(conversation.turns[0].transcript, message.transcript) == 0);
    CHECK(!conversation.turns[0].answer[0]);
    CHECK(!crab_conversation_receive(&conversation, &message));
    wcscpy(message.transcript, L"different pending transcript");
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(wcscmp(conversation.turns[0].transcript, L"  Crab, same question?  ") == 0);
    message = (CrabMessage){.state = CRAB_SPEAKING,
                            .transcript = L"  Crab, same question?  ",
                            .answer = L"Same answer."};
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 1 && !conversation.pending);
    CHECK(conversation.turns[0].state == CRAB_TURN_ANSWERED);
    CHECK(wcscmp(conversation.turns[0].answer, L"Same answer.") == 0);
    CHECK(conversation.turns[0].error == S_OK && !conversation.turns[0].detail[0]);
    CHECK(!crab_conversation_receive(&conversation, &message));
    message.state = CRAB_IDLE;
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(!crab_conversation_receive(&conversation, &message));
    message.state = CRAB_THINKING;
    message.answer[0] = L'\0';
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 2 && conversation.pending);
    message.state = CRAB_SPEAKING;
    wcscpy(message.answer, L"Same answer.");
    CHECK(crab_conversation_receive(&conversation, &message));
    message.state = CRAB_IDLE;
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 2);
    CHECK(wcscmp(conversation.turns[0].transcript, conversation.turns[1].transcript) == 0);
    CHECK(wcscmp(conversation.turns[0].answer, conversation.turns[1].answer) == 0);
    message = (CrabMessage){.state = CRAB_THINKING, .transcript = L"Second question"};
    CHECK(crab_conversation_receive(&conversation, &message));
    message.state = CRAB_IDLE;
    wcscpy(message.answer, L"Second answer");
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 3 && !conversation.pending);
    CHECK(conversation.turns[2].state == CRAB_TURN_ANSWERED);
    CHECK(wcscmp(conversation.turns[2].transcript, L"Second question") == 0);
    CHECK(wcscmp(conversation.turns[2].answer, L"Second answer") == 0);
    message = (CrabMessage){
        .state = CRAB_SPEAKING, .transcript = L"Missing thinking", .answer = L"Delivered reply"};
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 4 && !conversation.pending);
    CHECK(wcscmp(conversation.turns[3].transcript, L"Missing thinking") == 0);
    CHECK(wcscmp(conversation.turns[3].answer, L"Delivered reply") == 0);
    CHECK(!crab_conversation_receive(&conversation, &message));
    CHECK(!crab_conversation_receive(NULL, &message));
    CHECK(!crab_conversation_receive(&conversation, NULL));
    CHECK(!crab_conversation_cancel(&conversation));
}

static void test_conversation_errors(void) {
    CrabConversation conversation = {0};
    CrabMessage message = {.state = CRAB_ERROR, .error = E_FAIL, .answer = L"Model unavailable"};
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 1 && !conversation.pending);
    CHECK(conversation.turns[0].state == CRAB_TURN_ERROR);
    CHECK(conversation.turns[0].error == E_FAIL);
    CHECK(!conversation.turns[0].transcript[0] && !conversation.turns[0].answer[0]);
    CHECK(wcscmp(conversation.turns[0].detail, L"Model unavailable") == 0);
    CHECK(!crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_THINKING, .transcript = L"Crab, original text"};
    CHECK(crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_ERROR,
                            .error = E_ACCESSDENIED,
                            .transcript = L"Crab, actual recognized text",
                            .answer = L"Provider rejected request"};
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 2 && !conversation.pending);
    CHECK(conversation.turns[1].state == CRAB_TURN_ERROR);
    CHECK(conversation.turns[1].error == E_ACCESSDENIED);
    CHECK(wcscmp(conversation.turns[1].transcript, L"Crab, actual recognized text") == 0);
    CHECK(wcscmp(conversation.turns[1].detail, L"Provider rejected request") == 0);
    CHECK(bytes_zero(conversation.turns[1].answer, sizeof(conversation.turns[1].answer)));
    CHECK(!crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_THINKING, .transcript = L"Keep transcript"};
    CHECK(crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_ERROR, .error = E_FAIL};
    wmemset(message.answer, L'x', 200);
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 3);
    CHECK(wcscmp(conversation.turns[2].transcript, L"Keep transcript") == 0);
    CHECK(wcslen(conversation.turns[2].detail) == 159);
    CHECK(!conversation.turns[2].answer[0]);
    CHECK(!crab_conversation_receive(&conversation, &message));
    message = (CrabMessage){.state = CRAB_THINKING, .transcript = L"Cancel this"};
    CHECK(crab_conversation_receive(&conversation, &message));
    wcscpy(conversation.turns[3].answer, L"Not a completed answer");
    wcscpy(conversation.turns[3].detail, L"Discard this detail");
    CHECK(crab_conversation_cancel(&conversation));
    CHECK(conversation.count == 4 && !conversation.pending);
    CHECK(conversation.turns[3].state == CRAB_TURN_CANCELLED);
    CHECK(conversation.turns[3].error == E_ABORT);
    CHECK(wcscmp(conversation.turns[3].transcript, L"Cancel this") == 0);
    CHECK(bytes_zero(conversation.turns[3].answer, sizeof(conversation.turns[3].answer)));
    CHECK(bytes_zero(conversation.turns[3].detail, sizeof(conversation.turns[3].detail)));
    CHECK(!crab_conversation_cancel(&conversation));
    message.state = CRAB_SPEAKING;
    wcscpy(message.answer, L"Late reply");
    CHECK(!crab_conversation_receive(&conversation, &message));
    message.state = CRAB_IDLE;
    CHECK(!crab_conversation_receive(&conversation, &message));
    message.state = CRAB_THINKING;
    message.answer[0] = L'\0';
    CHECK(crab_conversation_receive(&conversation, &message));
    message.state = CRAB_SPEAKING;
    wcscpy(message.answer, L"New reply");
    CHECK(crab_conversation_receive(&conversation, &message));
    CHECK(conversation.count == 5 && conversation.turns[4].state == CRAB_TURN_ANSWERED);
    CHECK(conversation.turns[3].state == CRAB_TURN_CANCELLED);
    crab_conversation_clear(&conversation);
    CHECK(bytes_zero(&conversation, sizeof(conversation)));
    memset(&conversation, 0xa5, sizeof(conversation));
    crab_conversation_clear(&conversation);
    CHECK(bytes_zero(&conversation, sizeof(conversation)));
    crab_conversation_clear(NULL);
    CHECK(!crab_conversation_cancel(NULL));
}

static void test_conversation_eviction(void) {
    CrabConversation conversation = {0};
    for (unsigned i = 0; i < CRAB_HISTORY + 2; ++i) {
        CrabMessage message = {.state = CRAB_THINKING};
        swprintf(message.transcript, 768, L"Question %u", i);
        CHECK(crab_conversation_receive(&conversation, &message));
        CrabTurn *turn = &conversation.turns[conversation.count - 1];
        CHECK(turn->state == CRAB_TURN_WAITING && turn->error == S_OK);
        CHECK(bytes_zero(turn->answer, sizeof(turn->answer)));
        CHECK(bytes_zero(turn->detail, sizeof(turn->detail)));
        message.state = CRAB_SPEAKING;
        swprintf(message.answer, 2048, L"Answer %u", i);
        CHECK(crab_conversation_receive(&conversation, &message));
        if (i == 0)
            wcscpy(conversation.turns[0].detail, L"Discarded private detail");
    }
    CHECK(conversation.count == CRAB_HISTORY && !conversation.pending);
    for (unsigned i = 0; i < CRAB_HISTORY; ++i) {
        wchar_t expected[32];
        swprintf(expected, 32, L"Question %u", i + 2);
        CHECK(wcscmp(conversation.turns[i].transcript, expected) == 0);
        swprintf(expected, 32, L"Answer %u", i + 2);
        CHECK(wcscmp(conversation.turns[i].answer, expected) == 0);
        CHECK(conversation.turns[i].state == CRAB_TURN_ANSWERED);
        CHECK(bytes_zero(conversation.turns[i].detail, sizeof(conversation.turns[i].detail)));
    }
    crab_conversation_clear(&conversation);
    CHECK(bytes_zero(&conversation, sizeof(conversation)));
}

static unsigned text_count(const char *text, const char *value) {
    unsigned count = 0;
    size_t length = strlen(value);
    while ((text = strstr(text, value)) != NULL) {
        ++count;
        text += length;
    }
    return count;
}

static void test_request(void) {
    CrabConversation conversation = {
        .turns = {{.transcript = L"First \"question\"\n\\path",
                   .answer = L"First\tanswer \x2603",
                   .state = CRAB_TURN_ANSWERED},
                  {.transcript = L"Error question",
                   .answer = L"Error answer",
                   .detail = L"Private diagnostic",
                   .error = E_FAIL,
                   .state = CRAB_TURN_ERROR},
                  {.transcript = L"Cancelled question",
                   .answer = L"Cancelled answer",
                   .state = CRAB_TURN_CANCELLED},
                  {.transcript = L"Waiting question",
                   .answer = L"Unfinished answer",
                   .state = CRAB_TURN_WAITING},
                  {.transcript = L"Repeat question",
                   .answer = L"Second \"answer\"\\tail",
                   .state = CRAB_TURN_ANSWERED},
                  {.answer = L"Orphan answer", .state = CRAB_TURN_ANSWERED}},
        .count = CRAB_HISTORY,
        .pending = true};
    static const char system[] =
        "\"You are Crab, a concise Windows voice assistant. Answer directly in plain text suitable "
        "for speech. Keep most answers under 120 words. Never claim to have performed an action "
        "you cannot perform.\"";
    static const char exchanges[] =
        "{\"role\":\"user\",\"content\":\"First \\\"question\\\"\\n\\\\path\"},"
        "{\"role\":\"assistant\",\"content\":\"First\\tanswer \xe2\x98\x83\"},"
        "{\"role\":\"user\",\"content\":\"Repeat question\"},"
        "{\"role\":\"assistant\",\"content\":\"Second \\\"answer\\\"\\\\tail\"},"
        "{\"role\":\"user\",\"content\":\"Now \\\"please\\\"\\n\\\\end\"}]}";
    wchar_t question[768];
    CHECK(crab_wake_match(L"  Crab, Now \"please\"\n\\end  ", L"Crab", question, 768));
    char *json = malloc(CRAB_REQUEST_BYTES);
    CHECK(json != NULL);
    char expected[4096];
    for (int provider = CRAB_PROVIDER_ANTHROPIC; provider <= CRAB_PROVIDER_OPENAI_COMPATIBLE;
         ++provider) {
        if (provider == CRAB_PROVIDER_ANTHROPIC)
            snprintf(
                expected, sizeof(expected),
                "{\"model\":\"model\\\"\\\\\\n\",\"max_tokens\":1024,\"fallbacks\":\"default\","
                "\"output_config\":{\"effort\":\"medium\"},\"system\":%s,\"messages\":[%s",
                system, exchanges);
        else
            snprintf(expected, sizeof(expected),
                     "{\"model\":\"model\\\"\\\\\\n\",\"max_tokens\":1024,\"stream\":false,"
                     "\"messages\":[{\"role\":\"system\",\"content\":%s},%s",
                     system, exchanges);
        CHECK(crab_request_json((CrabProvider)provider, L"model\"\\\n", question, &conversation,
                                json, CRAB_REQUEST_BYTES));
        CHECK(strcmp(json, expected) == 0);
        CHECK(text_count(json, "\"role\":\"user\"") == 3);
        CHECK(text_count(json, "\"role\":\"assistant\"") == 2);
        CHECK(text_count(json, "Now \\\"please\\\"\\n\\\\end") == 1);
        CHECK(strstr(json, "Private diagnostic") == NULL);
        CHECK(strstr(json, "Error question") == NULL);
        CHECK(strstr(json, "Cancelled question") == NULL);
        CHECK(strstr(json, "Waiting question") == NULL);
        CHECK(strstr(json, "Orphan answer") == NULL);
        CHECK(crab_request_json((CrabProvider)provider, L"model", L"Repeat question", &conversation,
                                json, CRAB_REQUEST_BYTES));
        CHECK(text_count(json, "\"content\":\"Repeat question\"") == 2);
        CHECK(text_count(json, "\"role\":\"user\"") == 3);
        conversation.turns[5].answer[0] = L'\0';
        wcscpy(conversation.turns[5].transcript, L"Unanswered question");
        CHECK(crab_request_json((CrabProvider)provider, L"model\"\\\n", question, &conversation,
                                json, CRAB_REQUEST_BYTES));
        CHECK(strcmp(json, expected) == 0);
        CHECK(crab_request_json((CrabProvider)provider, L"model", L"Only question", NULL, json,
                                CRAB_REQUEST_BYTES));
        CHECK(text_count(json, "\"role\":\"user\"") == 1);
        CHECK(text_count(json, "\"role\":\"assistant\"") == 0);
        CHECK(strstr(json, "{\"role\":\"user\",\"content\":\"Only question\"}]}") != NULL);
    }
    SecureZeroMemory(json, CRAB_REQUEST_BYTES);
    free(json);
    crab_conversation_clear(&conversation);
}

static void test_request_limits(void) {
    CrabConversation conversation = {.count = CRAB_HISTORY};
    for (unsigned i = 0; i < CRAB_HISTORY; ++i) {
        wmemset(conversation.turns[i].transcript, L'\1', 767);
        wmemset(conversation.turns[i].answer, L'\1', 2047);
        conversation.turns[i].state = CRAB_TURN_ANSWERED;
    }
    wchar_t question[769] = {0};
    wchar_t model[96] = {0};
    wmemset(question, L'\1', 767);
    wmemset(model, L'\1', 95);
    char *json = malloc(CRAB_REQUEST_BYTES);
    CHECK(json != NULL);
    for (int provider = CRAB_PROVIDER_ANTHROPIC; provider <= CRAB_PROVIDER_OPENAI_COMPATIBLE;
         ++provider) {
        CHECK(crab_request_json((CrabProvider)provider, model, question, &conversation, json,
                                CRAB_REQUEST_BYTES));
        CHECK(strlen(json) < CRAB_REQUEST_BYTES);
        CHECK(strlen(json) > CRAB_HISTORY * (767 + 2047) * 6);
        CHECK(text_count(json, "\"role\":\"user\"") == CRAB_HISTORY + 1);
        CHECK(text_count(json, "\"role\":\"assistant\"") == CRAB_HISTORY);
        CHECK(!crab_request_json((CrabProvider)provider, model, question, &conversation, json, 32));
        CHECK(json[0] == '\0');
    }
    CHECK(!crab_request_json(CRAB_PROVIDER_NONE, L"model", L"question", NULL, json,
                             CRAB_REQUEST_BYTES));
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, NULL, L"question", NULL, json,
                             CRAB_REQUEST_BYTES));
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", NULL, NULL, json,
                             CRAB_REQUEST_BYTES));
    CHECK(
        !crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"", NULL, json, CRAB_REQUEST_BYTES));
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"question", NULL, NULL, 1024));
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"question", NULL, json, 0));
    conversation.count = CRAB_HISTORY + 1;
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"question", &conversation, json,
                             CRAB_REQUEST_BYTES));
    conversation.count = CRAB_HISTORY;
    question[767] = L'\1';
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", question, NULL, json,
                             CRAB_REQUEST_BYTES));
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"\xd800", NULL, json,
                             CRAB_REQUEST_BYTES));
    conversation.turns[0].transcript[767] = L'x';
    CHECK(!crab_request_json(CRAB_PROVIDER_ANTHROPIC, L"model", L"question", &conversation, json,
                             CRAB_REQUEST_BYTES));
    SecureZeroMemory(json, CRAB_REQUEST_BYTES);
    free(json);
    crab_conversation_clear(&conversation);
}

static void test_context_clear(void) {
    CrabService service = {.enabled = 1, .provider = CRAB_PROVIDER_ANTHROPIC, .model = L"model"};
    InitializeCriticalSection(&service.lock);
    service.lock_ready = true;
    memset(&service.context, 0xa5, sizeof(service.context));
    crab_clear_context(&service);
    CHECK(bytes_zero(&service.context, sizeof(service.context)));
    CHECK(service.enabled == 1 && service.provider == CRAB_PROVIDER_ANTHROPIC);
    CHECK(wcscmp(service.model, L"model") == 0 && service.lock_ready);
    DeleteCriticalSection(&service.lock);
    service.lock_ready = false;
    crab_clear_context(&service);
    crab_clear_context(NULL);
}

static void test_message_generation(void) {
    CrabService service = {.generation = 7};
    CrabMessage message = {.generation = 7, .state = CRAB_IDLE};
    CHECK(crab_message_current(&service, &message));
    service.generation = 8;
    CHECK(!crab_message_current(&service, &message));
    message.generation = 8;
    CHECK(crab_message_current(&service, &message));
    service.cancel_request = 1;
    CHECK(!crab_message_current(&service, &message));
    CHECK(!crab_message_current(NULL, &message));
    CHECK(!crab_message_current(&service, NULL));
    crab_message_free(NULL);
}

static void test_reply_text(void) {
    CrabTurn turn = {.state = CRAB_TURN_ERROR};
    CHECK(wcsstr(crab_turn_reply(&turn), L"failed") != NULL);
    wcscpy(turn.detail, L"Provider timed out");
    CHECK(wcscmp(crab_turn_reply(&turn), turn.detail) == 0);
    turn.state = CRAB_TURN_ANSWERED;
    CHECK(wcsstr(crab_turn_reply(&turn), L"No reply") != NULL);
    wcscpy(turn.answer, L"Two");
    CHECK(wcscmp(crab_turn_reply(&turn), L"Two") == 0);
    turn.state = CRAB_TURN_CANCELLED;
    CHECK(wcscmp(crab_turn_reply(&turn), L"Cancelled") == 0);
    turn.state = CRAB_TURN_WAITING;
    CHECK(wcsstr(crab_turn_reply(&turn), L"Waiting") != NULL);
}

static void test_states(void) {
    CHECK(wcscmp(crab_state_text(CRAB_LISTENING), L"Listening") == 0);
    CHECK(wcsstr(crab_state_text(CRAB_ERROR), L"attention") != NULL);
    CHECK(wcscmp(crab_provider_text(CRAB_PROVIDER_ANTHROPIC), L"Anthropic Messages") == 0);
    CHECK(wcsstr(crab_provider_text(CRAB_PROVIDER_OPENAI_COMPATIBLE), L"OpenAI") != NULL);
}

int main(void) {
    test_wake();
    test_json();
    test_response();
    test_endpoints();
    test_speech_activity();
    test_speech_paths();
    test_conversation();
    test_conversation_errors();
    test_conversation_eviction();
    test_request();
    test_request_limits();
    test_context_clear();
    test_message_generation();
    test_reply_text();
    test_states();
    printf("%u Crab checks passed; no microphone, network, credentials, speech, or GUI\n", checks);
    return 0;
}
