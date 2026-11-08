#include "headphones.h"

#include <devpropdef.h>
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

static HeadphoneDevice device(unsigned id, bool bluetooth, int battery) {
    HeadphoneDevice result = {.container = {id, 0, 0, {0}},
                              .has_container = bluetooth,
                              .bluetooth = bluetooth,
                              .headphones = true,
                              .battery_percent = battery};
    swprintf(result.id, 260, L"endpoint-%u", id);
    swprintf(result.name, 160, L"Headphones %u", id);
    return result;
}

static void test_battery(void) {
    BYTE byte = 0;
    ULONG number = 100;
    USHORT small = 42;
    CHECK(headphones_battery(DEVPROP_TYPE_BYTE, &byte, sizeof(byte)) == 0);
    CHECK(headphones_battery(DEVPROP_TYPE_UINT16, &small, sizeof(small)) == 42);
    CHECK(headphones_battery(DEVPROP_TYPE_UINT32, &number, sizeof(number)) == 100);
    byte = 255;
    CHECK(headphones_battery(DEVPROP_TYPE_BYTE, &byte, sizeof(byte)) == -1);
    number = 101;
    CHECK(headphones_battery(DEVPROP_TYPE_UINT32, &number, sizeof(number)) == -1);
    CHECK(headphones_battery(DEVPROP_TYPE_STRING, &small, sizeof(small)) == -1);
    CHECK(headphones_battery(DEVPROP_TYPE_UINT32, &number, 1) == -1);
    CHECK(headphones_battery(DEVPROP_TYPE_UINT32, NULL, sizeof(number)) == -1);
}

static void test_changes(void) {
    HeadphoneSnapshot previous = {0};
    HeadphoneSnapshot current = {.initialized = true, .count = 1};
    current.devices[0] = device(1, true, 75);
    HeadphoneEvent events[4];
    CHECK(headphones_changes(&previous, &current, events, 4) == 0);
    previous.initialized = true;
    CHECK(headphones_changes(&previous, &current, events, 4) == 1);
    CHECK(events[0].change == HEADPHONE_CONNECTED);
    CHECK(events[0].device.battery_percent == 75);
    previous = current;
    CHECK(headphones_changes(&previous, &current, events, 4) == 0);
    wcscpy(current.devices[0].id, L"hands-free-endpoint");
    CHECK(headphones_changes(&previous, &current, events, 4) == 0);
    current.devices[0].battery_percent = 74;
    CHECK(headphones_changes(&previous, &current, events, 4) == 1);
    CHECK(events[0].change == HEADPHONE_BATTERY);
    current.devices[0].battery_percent = -1;
    CHECK(headphones_changes(&previous, &current, events, 4) == 0);
    current.count = 0;
    CHECK(headphones_changes(&previous, &current, events, 4) == 1);
    CHECK(events[0].change == HEADPHONE_DISCONNECTED);
    CHECK(wcscmp(events[0].device.name, previous.devices[0].name) == 0);
    current.count = 1;
    current.devices[0] = device(2, false, -1);
    CHECK(headphones_changes(&previous, &current, events, 4) == 2);
    CHECK(events[0].change == HEADPHONE_CONNECTED);
    CHECK(events[1].change == HEADPHONE_DISCONNECTED);
    CHECK(headphones_changes(&previous, &current, events, 1) == 1);
    CHECK(headphones_changes(&previous, &current, events, 0) == 0);
    current.error = E_ACCESSDENIED;
    CHECK(headphones_changes(&previous, &current, events, 4) == 0);
}

static void test_details(void) {
    wchar_t text[160];
    HeadphoneDevice headphones = device(1, true, 0);
    headphones_detail(&headphones, true, text, 160);
    CHECK(wcsstr(text, L"0%") != NULL);
    headphones.battery_percent = -1;
    headphones_detail(&headphones, true, text, 160);
    CHECK(wcsstr(text, L"unavailable") != NULL);
    CHECK(wcschr(text, L'%') == NULL);
    headphones.battery_percent = 80;
    headphones_detail(&headphones, false, text, 160);
    CHECK(wcschr(text, L'%') == NULL);
    headphones.bluetooth = false;
    headphones_detail(&headphones, true, text, 160);
    CHECK(wcsstr(text, L"Wired") != NULL);
}

int main(void) {
    test_battery();
    test_changes();
    test_details();
    printf("%u headphone checks passed; no GUI or device changes\n", checks);
    return 0;
}
