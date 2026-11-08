#include "notifications.h"

#include <stdio.h>
#include <stdlib.h>
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

static void test_seen(void) {
    NotificationSeen seen;
    notifications_seen_reset(&seen);
    CHECK(!seen.initialized);
    CHECK(!notifications_accept(&seen, 1, 100, false));
    CHECK(!notifications_accept(&seen, 1, 100, false));
    seen.initialized = true;
    CHECK(notifications_accept(&seen, 2, 200, false));
    CHECK(!notifications_accept(&seen, 2, 200, false));
    CHECK(!notifications_accept(&seen, 3, 300, true));
    CHECK(!notifications_accept(NULL, 1, 1, false));
    for (unsigned i = 0; i < NOTIFICATION_SEEN + 5; ++i)
        (void)notifications_accept(&seen, 1000 + i, 1000 + i, false);
    CHECK(seen.count == NOTIFICATION_SEEN);
    CHECK(seen.next < NOTIFICATION_SEEN);
}

static void test_clean(void) {
    wchar_t text[64] = L"  Hello\r\n\tworld  ";
    notifications_clean(text, 64);
    CHECK(wcscmp(text, L"Hello world") == 0);
    wcscpy(text, L"One\x2028Two\x2029Three");
    notifications_clean(text, 64);
    CHECK(wcscmp(text, L"One Two Three") == 0);
    wcscpy(text, L"\t\r\n");
    notifications_clean(text, 64);
    CHECK(!*text);
    notifications_clean(NULL, 0);
}

static void test_status(void) {
    wchar_t text[256];
    NotificationService service = {0};
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"off") != NULL);
    service.access = NOTIFICATIONS_PACKAGE_REQUIRED;
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"MSIX") != NULL);
    service.access = NOTIFICATIONS_REQUESTING;
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"Waiting") != NULL);
    service.access = NOTIFICATIONS_ALLOWED;
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"allowed") != NULL);
    service.access = NOTIFICATIONS_DENIED;
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"denied") != NULL);
    service.access = NOTIFICATIONS_UNAVAILABLE;
    notifications_status(&service, text, 256);
    CHECK(wcsstr(text, L"unavailable") != NULL);
}

int main(void) {
    test_seen();
    test_clean();
    test_status();
    printf("%u notification checks passed; no permission prompt or GUI\n", checks);
    return 0;
}
