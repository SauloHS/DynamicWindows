#include "island.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void test_timer(void) {
    Island island;
    island_init(&island);
    CHECK(island.timer.remaining == 300000);
    CHECK(!island_timer_start(&island, 0, 100));
    CHECK(!island_timer_start(&island, ISLAND_MAX_TIMER_MS + 1, 100));
    CHECK(!island_timer_start(&island, 1000, UINT64_MAX - 500));
    CHECK(island.timer.remaining == 300000);
    CHECK(island_timer_start(&island, 5000, 1000));
    CHECK(island.timer.deadline == 6000);
    CHECK(!island_tick(&island, 2200));
    CHECK(island.timer.remaining == 3800);
    CHECK(fabs(island_timer_progress(&island) - 0.76) < 0.000001);
    island_timer_toggle(&island, 2500);
    CHECK(island.timer.paused);
    CHECK(island.timer.remaining == 3500);
    CHECK(!island_tick(&island, 90000));
    CHECK(island.timer.remaining == 3500);
    island_timer_toggle(&island, 100000);
    CHECK(!island.timer.paused);
    CHECK(island.timer.deadline == 103500);
    CHECK(!island_tick(&island, 103499));
    CHECK(island.timer.remaining == 1);
    CHECK(island_tick(&island, 103500));
    CHECK(island.timer.finished);
    CHECK(!island.timer.running);
    CHECK(island.alert == ALERT_TIMER);
    CHECK(!island_tick(&island, 900000));
    CHECK(island.alert == ALERT_TIMER);
    island_timer_toggle(&island, 900000);
    CHECK(!island.timer.finished);
    CHECK(island.timer.remaining == 5000);
    CHECK(island.alert == ALERT_NONE);
    island_timer_toggle(&island, 900000);
    CHECK(island.timer.running);
    CHECK(island_tick(&island, 2000000));
    island_timer_reset(&island);
    CHECK(!island.timer.running);
    CHECK(!island.timer.paused);
    CHECK(!island.timer.finished);
    CHECK(island.timer.duration == 5000);
    CHECK(island.alert == ALERT_NONE);
}

static void test_expired_pause(void) {
    Island island;
    island_init(&island);
    CHECK(island_timer_start(&island, 100, 0));
    island_timer_toggle(&island, 100);
    CHECK(island.timer.finished);
    CHECK(!island.timer.paused);
    CHECK(island.alert == ALERT_TIMER);
    CHECK(island_timer_progress(&island) == 0);
    CHECK(island_timer_start(&island, 200, 100));
    CHECK(island.alert == ALERT_NONE);
    CHECK(!island.timer.finished);
}

static void test_alerts(void) {
    Island island;
    island_init(&island);
    island_show_alert(&island, ALERT_VOLUME, 100);
    CHECK(island.alert_deadline == 1900);
    island_show_alert(&island, ALERT_VOLUME, 1000);
    CHECK(island.alert_deadline == 2800);
    CHECK(!island_tick(&island, 2799));
    CHECK(island.alert == ALERT_VOLUME);
    CHECK(!island_tick(&island, 2800));
    CHECK(island.alert == ALERT_NONE);
    island_show_alert(&island, ALERT_POWER, 3000);
    island_show_alert(&island, ALERT_VOLUME, 4000);
    CHECK(island.alert == ALERT_POWER);
    CHECK(island.alert_deadline == 7000);
    island_show_alert(&island, ALERT_HEADPHONES, 4500);
    CHECK(island.alert == ALERT_HEADPHONES);
    island_show_alert(&island, ALERT_POWER, 4600);
    CHECK(island.alert == ALERT_HEADPHONES);
    island_show_alert(&island, ALERT_TIMER, 5000);
    CHECK(island.alert == ALERT_TIMER);
    island_show_alert(&island, ALERT_POWER, 6000);
    CHECK(island.alert == ALERT_TIMER);
    CHECK(!island_tick(&island, UINT64_MAX));
    CHECK(island.alert == ALERT_TIMER);
    island_timer_reset(&island);
    island_show_alert(&island, ALERT_VOLUME, UINT64_MAX - 100);
    CHECK(island.alert_deadline == UINT64_MAX);
}

static void test_activities(void) {
    Island island;
    island_init(&island);
    CHECK(island_primary(&island) == ACTIVITY_NONE);
    CHECK(island_secondary(&island) == ACTIVITY_NONE);
    CHECK(island_size(&island).width == 168);
    island.media_available = true;
    island.media_playing = true;
    CHECK(island_primary(&island) == ACTIVITY_MEDIA);
    CHECK(island_size(&island).width == 232);
    CHECK(island_timer_start(&island, 1000, 0));
    CHECK(island_primary(&island) == ACTIVITY_TIMER);
    CHECK(island_secondary(&island) == ACTIVITY_MEDIA);
    CHECK(island_size(&island).bubble == 44);
    island_open(&island, ISLAND_MEDIA);
    CHECK(island.expanded);
    CHECK(island.view == ISLAND_MEDIA);
    CHECK(island_primary(&island) == ACTIVITY_MEDIA);
    CHECK(island_secondary(&island) == ACTIVITY_NONE);
    CHECK(island_size(&island).height == 264);
    island_close(&island);
    CHECK(island_secondary(&island) == ACTIVITY_TIMER);
    island.media_available = false;
    CHECK(island_primary(&island) == ACTIVITY_TIMER);
    CHECK(island_secondary(&island) == ACTIVITY_NONE);
    CHECK(island_tick(&island, 1000));
    CHECK(island_size(&island).height == 76);
    island_open(&island, ISLAND_TIMER);
    CHECK(island_size(&island).height == 264);
    island_timer_reset(&island);
    island_close(&island);
    CHECK(island_primary(&island) == ACTIVITY_NONE);
}

static void test_spring(void) {
    IslandSpring spring = {168, 0};
    CHECK(island_spring_step(&spring, 408, 1.0 / 60, false));
    CHECK(spring.value > 168 && spring.value < 408);
    for (int i = 0; i < 180; ++i)
        (void)island_spring_step(&spring, 408, 1.0 / 60, false);
    CHECK(spring.value == 408);
    CHECK(spring.velocity == 0);
    CHECK(!island_spring_step(&spring, 168, 1, true));
    CHECK(spring.value == 168);
    CHECK(spring.velocity == 0);
    (void)island_spring_step(&spring, 408, 30, false);
    CHECK(isfinite(spring.value));
    CHECK(spring.value > 168 && spring.value < 440);
    double before = spring.value;
    (void)island_spring_step(&spring, 408, -1, false);
    CHECK(spring.value == before);
    CHECK(!island_spring_step(&spring, NAN, 0.1, false));
    CHECK(spring.value == before);
    spring.value = NAN;
    CHECK(!island_spring_step(&spring, 100, 0.1, false));
    CHECK(spring.value == 100);
}

static void test_frame_independent_spring(void) {
    for (int damping = 50; damping <= 100; damping += 25) {
        IslandSpring slow = {168, 0};
        IslandSpring fast = {168, 0};
        for (int i = 0; i < 30; ++i)
            (void)island_spring_tuned(&slow, 408, 1.0 / 30, false, 600, damping);
        for (int i = 0; i < 144; ++i)
            (void)island_spring_tuned(&fast, 408, 1.0 / 144, false, 600, damping);
        CHECK(fabs(slow.value - fast.value) < 0.001);
        CHECK(fabs(slow.velocity - fast.velocity) < 0.001);
        CHECK(isfinite(fast.value));
        (void)island_spring_tuned(&fast, 80, 1.0 / 120, false, 600, damping);
        CHECK(isfinite(fast.value));
        CHECK(isfinite(fast.velocity));
        for (int i = 0; i < 1200; ++i)
            (void)island_spring_tuned(&fast, 80, 1.0 / 120, false, 600, damping);
        CHECK(fast.value == 80);
        CHECK(fast.velocity == 0);
    }
}

static void test_geometry(void) {
    CHECK(island_contains(84, 22, 168, 44, 22));
    CHECK(island_contains(22, 0, 168, 44, 22));
    CHECK(!island_contains(0, 0, 168, 44, 22));
    CHECK(!island_contains(-1, 22, 168, 44, 22));
    CHECK(!island_contains(169, 22, 168, 44, 22));
    CHECK(!island_contains(84, 45, 168, 44, 22));
    CHECK(island_contains(10, 10, 20, 20, 100));
    CHECK(!island_contains(NAN, 10, 20, 20, 10));
    CHECK(!island_contains(10, 10, 0, 20, 10));
    CHECK(island_clamp(-3, 0, 1) == 0);
    CHECK(island_clamp(3, 0, 1) == 1);
    CHECK(island_clamp(0.5, 0, 1) == 0.5);
    CHECK(island_clamp(NAN, 0, 1) == 0);
}

int main(void) {
    test_timer();
    test_expired_pause();
    test_alerts();
    test_activities();
    test_spring();
    test_frame_independent_spring();
    test_geometry();
    printf("%u checks passed\n", checks);
    return 0;
}
