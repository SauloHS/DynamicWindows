#include "island.h"

#include <math.h>
#include <string.h>

void island_init(Island *island) {
    memset(island, 0, sizeof(*island));
    island->timer.duration = 300000;
    island->timer.remaining = island->timer.duration;
    island->view = ISLAND_MEDIA;
}

bool island_timer_start(Island *island, uint64_t duration, uint64_t now) {
    if (duration == 0 || duration > ISLAND_MAX_TIMER_MS || now > UINT64_MAX - duration)
        return false;
    island->timer = (IslandTimer){
        .duration = duration, .remaining = duration, .deadline = now + duration, .running = true};
    island->preferred = ACTIVITY_TIMER;
    if (island->alert == ALERT_TIMER)
        island->alert = ALERT_NONE;
    return true;
}

void island_timer_toggle(Island *island, uint64_t now) {
    IslandTimer *timer = &island->timer;
    if (timer->finished) {
        island_timer_reset(island);
        return;
    }
    if (!timer->running) {
        (void)island_timer_start(island, timer->duration, now);
        return;
    }
    if (!timer->paused) {
        (void)island_tick(island, now);
        if (timer->finished)
            return;
        timer->paused = true;
        return;
    }
    if (now > UINT64_MAX - timer->remaining)
        return;
    timer->deadline = now + timer->remaining;
    timer->paused = false;
}

void island_timer_reset(Island *island) {
    uint64_t duration = island->timer.duration;
    island->timer = (IslandTimer){.duration = duration, .remaining = duration};
    if (island->alert == ALERT_TIMER)
        island->alert = ALERT_NONE;
}

bool island_tick(Island *island, uint64_t now) {
    bool finished = false;
    IslandTimer *timer = &island->timer;
    if (timer->running && !timer->paused) {
        timer->remaining = now >= timer->deadline ? 0 : timer->deadline - now;
        if (timer->remaining == 0) {
            timer->running = false;
            timer->finished = true;
            island_show_alert(island, ALERT_TIMER, now);
            finished = true;
        }
    }
    if (island->alert != ALERT_NONE && island->alert != ALERT_TIMER &&
        now >= island->alert_deadline)
        island->alert = ALERT_NONE;
    return finished;
}

void island_show_alert(Island *island, IslandAlert alert, uint64_t now) {
    if (alert == ALERT_NONE || alert < island->alert)
        return;
    uint64_t duration = alert == ALERT_VOLUME ? 1800 : 4000;
    island->alert = alert;
    island->alert_deadline = now > UINT64_MAX - duration ? UINT64_MAX : now + duration;
}

void island_open(Island *island, IslandView view) {
    island->expanded = true;
    island->view = view;
    if (view == ISLAND_MEDIA && island->media_available)
        island->preferred = ACTIVITY_MEDIA;
    if (view == ISLAND_TIMER && (island->timer.running || island->timer.finished))
        island->preferred = ACTIVITY_TIMER;
}

void island_close(Island *island) {
    island->expanded = false;
}

IslandActivity island_primary(const Island *island) {
    bool timer = island->timer.running || island->timer.finished;
    if (island->preferred == ACTIVITY_TIMER && timer)
        return ACTIVITY_TIMER;
    if (island->preferred == ACTIVITY_MEDIA && island->media_available)
        return ACTIVITY_MEDIA;
    if (island->media_available)
        return ACTIVITY_MEDIA;
    return timer ? ACTIVITY_TIMER : ACTIVITY_NONE;
}

IslandActivity island_secondary(const Island *island) {
    if (island->expanded || !island->media_available || !island->timer.running)
        return ACTIVITY_NONE;
    return island_primary(island) == ACTIVITY_MEDIA ? ACTIVITY_TIMER : ACTIVITY_MEDIA;
}

IslandSize island_size(const Island *island) {
    if (island->expanded)
        return (IslandSize){408, 264, 30, 0, 0};
    if (island->alert != ALERT_NONE) {
        double height = island->alert == ALERT_TIMER ? 76 : 68;
        return (IslandSize){312, height, height / 2, 0, 0};
    }
    IslandActivity primary = island_primary(island);
    double width = primary == ACTIVITY_MEDIA ? 232 : primary == ACTIVITY_TIMER ? 196 : 168;
    return (IslandSize){width, 44, 22, island_secondary(island) != ACTIVITY_NONE ? 44 : 0, 0};
}

double island_timer_progress(const Island *island) {
    if (island->timer.duration == 0)
        return 0;
    return island_clamp((double)island->timer.remaining / (double)island->timer.duration, 0, 1);
}

double island_clamp(double value, double low, double high) {
    if (isnan(value))
        return low;
    return value < low ? low : value > high ? high : value;
}

bool island_spring_tuned(IslandSpring *spring, double target, double seconds, bool reduced_motion,
                         double response_ms, double damping_percent) {
    if (!isfinite(target))
        return false;
    if (reduced_motion || !isfinite(spring->value) || !isfinite(spring->velocity)) {
        spring->value = target;
        spring->velocity = 0;
        return false;
    }
    double elapsed = island_clamp(seconds, 0, 0.1);
    double damping = island_clamp(damping_percent / 100, 0.5, 1);
    double response = island_clamp(response_ms / 1000, 0.15, 1.8);
    double frequency = 4.6 / (damping * response);
    double delta = spring->value - target;
    double velocity = spring->velocity;
    double decay = exp(-damping * frequency * elapsed);
    if (damping < 0.999) {
        double damped = frequency * sqrt(1 - damping * damping);
        double cosine = cos(damped * elapsed);
        double sine = sin(damped * elapsed);
        spring->value = target + decay * (delta * cosine +
                                          (velocity + damping * frequency * delta) / damped * sine);
        spring->velocity =
            decay *
            (velocity * cosine -
             (damping * frequency * velocity + frequency * frequency * delta) / damped * sine);
    } else {
        double coefficient = velocity + frequency * delta;
        spring->value = target + (delta + coefficient * elapsed) * decay;
        spring->velocity = (velocity - frequency * coefficient * elapsed) * decay;
    }
    if (fabs(target - spring->value) < 0.02 && fabs(spring->velocity) < 0.02) {
        spring->value = target;
        spring->velocity = 0;
        return false;
    }
    return true;
}

bool island_spring_step(IslandSpring *spring, double target, double seconds, bool reduced_motion) {
    return island_spring_tuned(spring, target, seconds, reduced_motion, 420, 78);
}

bool island_contains(double x, double y, double width, double height, double radius) {
    if (!isfinite(x) || !isfinite(y) || width <= 0 || height <= 0 || x < 0 || y < 0 || x > width ||
        y > height)
        return false;
    radius = island_clamp(radius, 0, fmin(width, height) / 2);
    double center_x = island_clamp(x, radius, width - radius);
    double center_y = island_clamp(y, radius, height - radius);
    double dx = x - center_x;
    double dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}
