#ifndef DYNAMICWINDOWS_ISLAND_H
#define DYNAMICWINDOWS_ISLAND_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { ISLAND_MEDIA, ISLAND_TIMER, ISLAND_SYSTEM } IslandView;
typedef enum { ACTIVITY_NONE, ACTIVITY_MEDIA, ACTIVITY_TIMER } IslandActivity;
typedef enum { ALERT_NONE, ALERT_VOLUME, ALERT_POWER, ALERT_HEADPHONES, ALERT_TIMER } IslandAlert;

typedef struct {
    uint64_t duration;
    uint64_t remaining;
    uint64_t deadline;
    bool running;
    bool paused;
    bool finished;
} IslandTimer;

typedef struct {
    IslandTimer timer;
    IslandView view;
    IslandActivity preferred;
    IslandAlert alert;
    uint64_t alert_deadline;
    bool expanded;
    bool media_available;
    bool media_playing;
} Island;

typedef struct {
    double value;
    double velocity;
} IslandSpring;

typedef struct {
    double width;
    double height;
    double radius;
    double bubble;
} IslandSize;

#define ISLAND_MAX_TIMER_MS UINT64_C(86400000)

void island_init(Island *island);
bool island_timer_start(Island *island, uint64_t duration, uint64_t now);
void island_timer_toggle(Island *island, uint64_t now);
void island_timer_reset(Island *island);
bool island_tick(Island *island, uint64_t now);
void island_show_alert(Island *island, IslandAlert alert, uint64_t now);
void island_open(Island *island, IslandView view);
void island_close(Island *island);
IslandActivity island_primary(const Island *island);
IslandActivity island_secondary(const Island *island);
IslandSize island_size(const Island *island);
double island_timer_progress(const Island *island);
bool island_spring_step(IslandSpring *spring, double target, double seconds, bool reduced_motion);
bool island_spring_tuned(IslandSpring *spring, double target, double seconds, bool reduced_motion,
                         double response_ms, double damping_percent);
bool island_contains(double x, double y, double width, double height, double radius);
double island_clamp(double value, double low, double high);

#endif
