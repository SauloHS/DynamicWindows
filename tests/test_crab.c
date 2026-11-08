#include "crab.h"

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
    CHECK(!crab_wake_match(L"Crabapple question", L"Crab", question, 128));
    CHECK(!crab_wake_match(L"Crab", L"Crab", question, 128));
    CHECK(!crab_wake_match(NULL, L"Crab", question, 128));
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
    const char *response = "{\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"},"
                           "{\"type\":\"text\",\"text\":\"Two.\\nSimple \\\"answer\\\".\"}],"
                           "\"stop_reason\":\"end_turn\"}";
    CHECK(crab_response_text(response, answer, 256));
    CHECK(wcscmp(answer, L"Two.\nSimple \"answer\".") == 0);
    CHECK(crab_response_text("{\"type\":\"text\",\"text\":\"Unicode \\u2603\"}", answer, 256));
    CHECK(wcsstr(answer, L"Unicode") != NULL);
    CHECK(crab_response_text("{\"type\":\"text\",\"text\":\"Rocket 🚀\"}", answer, 256));
    CHECK(wcsstr(answer, L"Rocket") != NULL);
    CHECK(!crab_response_text("{\"content\":[]}", answer, 256));
    CHECK(!crab_response_text("not json", answer, 256));
}

static void test_states(void) {
    CHECK(wcscmp(crab_state_text(CRAB_LISTENING), L"Listening") == 0);
    CHECK(wcscmp(crab_state_text(CRAB_THINKING), L"Thinking") == 0);
    CHECK(wcscmp(crab_state_text(CRAB_SPEAKING), L"Speaking") == 0);
    CHECK(wcsstr(crab_state_text(CRAB_ERROR), L"attention") != NULL);
    CHECK(wcsstr(crab_state_text(CRAB_DISABLED), L"disabled") != NULL);
}

int main(void) {
    test_wake();
    test_json();
    test_response();
    test_states();
    printf("%u Crab checks passed; no microphone, network, credentials, speech, or GUI\n", checks);
    return 0;
}
