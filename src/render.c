#include "app.h"

#include <math.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <wchar.h>

typedef enum {
    GLYPH_MUSIC,
    GLYPH_PLAY,
    GLYPH_PAUSE,
    GLYPH_PREVIOUS,
    GLYPH_NEXT,
    GLYPH_SPEAKER,
    GLYPH_MUTED,
    GLYPH_TIMER,
    GLYPH_RESET,
    GLYPH_CLOSE,
    GLYPH_SEARCH,
    GLYPH_BATTERY,
    GLYPH_POWER,
    GLYPH_SETTINGS,
    GLYPH_CHECK,
    GLYPH_HEADPHONES,
    GLYPH_BELL
} Glyph;

static ARGB ink(const Renderer *renderer, ARGB color) {
    static const ARGB defaults[TINT_COUNT] = {COLOR_SURFACE, COLOR_TEXT,    COLOR_SECONDARY,
                                              COLOR_BORDER,  COLOR_CONTROL, COLOR_HOVER,
                                              COLOR_ACCENT,  COLOR_TIMER,   COLOR_GOOD};
    unsigned alpha = (unsigned)((double)(color >> 24) * renderer->opacity);
    for (int i = 0; i < TINT_COUNT; ++i)
        if (color == defaults[i]) {
            color = renderer->colors[i];
            break;
        }
    if (color == 0xff28221bu)
        color = renderer->colors[TINT_CONTROL];
    return (color & 0x00ffffffu) | (alpha << 24);
}

static GpPath *round_path(RectF rect, double radius) {
    GpPath *path = NULL;
    if (GdipCreatePath(FillModeWinding, &path) != Ok)
        return NULL;
    REAL diameter = (REAL)(2 * island_clamp(radius, 0, fmin(rect.Width, rect.Height) / 2));
    if (diameter < 0.01f) {
        GdipAddPathRectangle(path, rect.X, rect.Y, rect.Width, rect.Height);
        return path;
    }
    GdipAddPathArc(path, rect.X, rect.Y, diameter, diameter, 180, 90);
    GdipAddPathArc(path, rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    GdipAddPathArc(path, rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter,
                   diameter, 0, 90);
    GdipAddPathArc(path, rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    GdipClosePathFigure(path);
    return path;
}

static void fill(Renderer *renderer, RectF rect, double radius, ARGB color) {
    GpPath *path = round_path(rect, radius);
    GpSolidFill *brush = NULL;
    if (path && GdipCreateSolidFill(ink(renderer, color), &brush) == Ok) {
        GdipFillPath(renderer->graphics, (GpBrush *)brush, path);
        GdipDeleteBrush((GpBrush *)brush);
    }
    if (path)
        GdipDeletePath(path);
}

static void outline(Renderer *renderer, RectF rect, double radius, ARGB color, double width) {
    GpPath *path = round_path(rect, radius);
    GpPen *pen = NULL;
    if (path && GdipCreatePen1(ink(renderer, color), (REAL)width, UnitPixel, &pen) == Ok) {
        GdipDrawPath(renderer->graphics, pen, path);
        GdipDeletePen(pen);
    }
    if (path)
        GdipDeletePath(path);
}

static void line(Renderer *renderer, double x1, double y1, double x2, double y2, ARGB color,
                 double width) {
    GpPen *pen = NULL;
    if (GdipCreatePen1(ink(renderer, color), (REAL)width, UnitPixel, &pen) != Ok)
        return;
    GdipSetPenStartCap(pen, LineCapRound);
    GdipSetPenEndCap(pen, LineCapRound);
    GdipDrawLine(renderer->graphics, pen, (REAL)x1, (REAL)y1, (REAL)x2, (REAL)y2);
    GdipDeletePen(pen);
}

static void arc(Renderer *renderer, RectF rect, double start, double sweep, ARGB color,
                double width) {
    GpPen *pen = NULL;
    if (sweep <= 0 || GdipCreatePen1(ink(renderer, color), (REAL)width, UnitPixel, &pen) != Ok)
        return;
    GdipSetPenStartCap(pen, LineCapRound);
    GdipSetPenEndCap(pen, LineCapRound);
    GdipDrawArc(renderer->graphics, pen, rect.X, rect.Y, rect.Width, rect.Height, (REAL)start,
                (REAL)sweep);
    GdipDeletePen(pen);
}

static void polygon(Renderer *renderer, const PointF *points, int count, ARGB color) {
    GpSolidFill *brush = NULL;
    if (GdipCreateSolidFill(ink(renderer, color), &brush) != Ok)
        return;
    GdipFillPolygon(renderer->graphics, (GpBrush *)brush, points, count, FillModeWinding);
    GdipDeleteBrush((GpBrush *)brush);
}

static void text(Renderer *renderer, const wchar_t *value, RectF rect, UiFont font, ARGB color,
                 StringAlignment align) {
    GpSolidFill *brush = NULL;
    if (!value || !*value || GdipCreateSolidFill(ink(renderer, color), &brush) != Ok)
        return;
    GdipSetStringFormatAlign(renderer->text_format, align);
    GdipDrawString(renderer->graphics, value, -1, renderer->fonts[font], &rect,
                   renderer->text_format, (GpBrush *)brush);
    GdipDeleteBrush((GpBrush *)brush);
}

static bool svg_path(GpPath *path, const char *start, const char *end) {
    double x = 0;
    double y = 0;
    while (start < end) {
        while (start < end && (*start == ' ' || *start == ','))
            ++start;
        if (start == end)
            break;
        char command = *start++;
        if (command == 'Z') {
            if (GdipClosePathFigure(path) != Ok)
                return false;
            continue;
        }
        unsigned count = command == 'C' ? 6 : command == 'M' || command == 'L' ? 2 : 0;
        if (!count)
            return false;
        double values[6];
        for (unsigned i = 0; i < count; ++i) {
            while (start < end && (*start == ' ' || *start == ','))
                ++start;
            char *next = NULL;
            values[i] = strtod(start, &next);
            if (next == start || next > end || !isfinite(values[i]))
                return false;
            start = next;
        }
        if (command == 'M') {
            GdipStartPathFigure(path);
            x = values[0];
            y = values[1];
        } else if (command == 'L') {
            if (GdipAddPathLine(path, (REAL)x, (REAL)y, (REAL)values[0], (REAL)values[1]) != Ok)
                return false;
            x = values[0];
            y = values[1];
        } else {
            if (GdipAddPathBezier(path, (REAL)x, (REAL)y, (REAL)values[0], (REAL)values[1],
                                  (REAL)values[2], (REAL)values[3], (REAL)values[4],
                                  (REAL)values[5]) != Ok)
                return false;
            x = values[4];
            y = values[5];
        }
    }
    return true;
}

static bool earbuds_init(App *app) {
    HRSRC resource = FindResourceW(app->instance, MAKEINTRESOURCEW(101), RT_RCDATA);
    DWORD size = resource ? SizeofResource(app->instance, resource) : 0;
    HGLOBAL loaded = resource ? LoadResource(app->instance, resource) : NULL;
    const char *bytes = loaded ? LockResource(loaded) : NULL;
    if (!bytes || !size || size > 8192)
        return false;
    char *svg = calloc((size_t)size + 1, 1);
    if (!svg)
        return false;
    memcpy(svg, bytes, size);
    const char *position = svg;
    bool valid = true;
    for (unsigned i = 0; i < EARBUD_PATH_COUNT; ++i) {
        const char *start = strstr(position, " d=\"");
        const char *end = start ? strchr(start + 4, '"') : NULL;
        if (!end || GdipCreatePath(FillModeWinding, &app->renderer.earbuds[i]) != Ok ||
            !svg_path(app->renderer.earbuds[i], start + 4, end)) {
            valid = false;
            break;
        }
        position = end + 1;
    }
    free(svg);
    return valid;
}

static void earbuds(Renderer *renderer, double x, double y, double size) {
    GraphicsState state;
    if (GdipSaveGraphics(renderer->graphics, &state) != Ok)
        return;
    GdipTranslateWorldTransform(renderer->graphics, (REAL)(x - size / 2), (REAL)(y - size / 2),
                                MatrixOrderPrepend);
    GdipScaleWorldTransform(renderer->graphics, (REAL)(size / 48), (REAL)(size / 48),
                            MatrixOrderPrepend);
    for (unsigned i = 0; i < EARBUD_PATH_COUNT; ++i) {
        GpSolidFill *brush = NULL;
        if (renderer->earbuds[i] && GdipCreateSolidFill(ink(renderer, COLOR_TEXT), &brush) == Ok) {
            GdipFillPath(renderer->graphics, (GpBrush *)brush, renderer->earbuds[i]);
            GdipDeleteBrush((GpBrush *)brush);
        }
    }
    GdipRestoreGraphics(renderer->graphics, state);
}

static void glyph(Renderer *renderer, Glyph symbol, double x, double y, double size, ARGB color) {
    double left = x - size / 2;
    double top = y - size / 2;
    double unit = size / 24;
    double stroke = 1.8 * unit;
    if (symbol == GLYPH_PLAY) {
        PointF points[] = {{(REAL)(left + 7 * unit), (REAL)(top + 4 * unit)},
                           {(REAL)(left + 20 * unit), (REAL)(top + 12 * unit)},
                           {(REAL)(left + 7 * unit), (REAL)(top + 20 * unit)}};
        polygon(renderer, points, 3, color);
        return;
    }
    if (symbol == GLYPH_PAUSE) {
        fill(renderer,
             (RectF){(REAL)(left + 6 * unit), (REAL)(top + 4 * unit), (REAL)(4 * unit),
                     (REAL)(16 * unit)},
             unit, color);
        fill(renderer,
             (RectF){(REAL)(left + 14 * unit), (REAL)(top + 4 * unit), (REAL)(4 * unit),
                     (REAL)(16 * unit)},
             unit, color);
        return;
    }
    if (symbol == GLYPH_PREVIOUS || symbol == GLYPH_NEXT) {
        double direction = symbol == GLYPH_NEXT ? 1 : -1;
        PointF points[] = {{(REAL)(x - direction * 7 * unit), (REAL)(y - 7 * unit)},
                           {(REAL)(x + direction * 5 * unit), (REAL)y},
                           {(REAL)(x - direction * 7 * unit), (REAL)(y + 7 * unit)}};
        polygon(renderer, points, 3, color);
        line(renderer, x + direction * 8 * unit, y - 7 * unit, x + direction * 8 * unit,
             y + 7 * unit, color, stroke + 0.5);
        return;
    }
    if (symbol == GLYPH_CLOSE) {
        line(renderer, x - 5 * unit, y - 5 * unit, x + 5 * unit, y + 5 * unit, color, stroke);
        line(renderer, x - 5 * unit, y + 5 * unit, x + 5 * unit, y - 5 * unit, color, stroke);
        return;
    }
    if (symbol == GLYPH_SETTINGS) {
        outline(renderer,
                (RectF){(REAL)(x - 7 * unit), (REAL)(y - 7 * unit), (REAL)(14 * unit),
                        (REAL)(14 * unit)},
                7 * unit, color, stroke);
        outline(renderer,
                (RectF){(REAL)(x - 2.5 * unit), (REAL)(y - 2.5 * unit), (REAL)(5 * unit),
                        (REAL)(5 * unit)},
                2.5 * unit, color, stroke);
        for (int i = 0; i < 8; ++i) {
            double angle = i * 0.78539816339;
            line(renderer, x + cos(angle) * 7 * unit, y + sin(angle) * 7 * unit,
                 x + cos(angle) * 10 * unit, y + sin(angle) * 10 * unit, color, stroke);
        }
        return;
    }
    if (symbol == GLYPH_SEARCH) {
        outline(renderer,
                (RectF){(REAL)(left + 4 * unit), (REAL)(top + 3 * unit), (REAL)(12 * unit),
                        (REAL)(12 * unit)},
                6 * unit, color, stroke);
        line(renderer, left + 15 * unit, top + 14 * unit, left + 21 * unit, top + 20 * unit, color,
             stroke);
        return;
    }
    if (symbol == GLYPH_TIMER || symbol == GLYPH_RESET) {
        arc(renderer,
            (RectF){(REAL)(left + 4 * unit), (REAL)(top + 5 * unit), (REAL)(16 * unit),
                    (REAL)(16 * unit)},
            symbol == GLYPH_RESET ? -65 : 0, symbol == GLYPH_RESET ? 295 : 360, color, stroke);
        if (symbol == GLYPH_RESET) {
            line(renderer, left + 4 * unit, top + 5 * unit, left + 4 * unit, top + 10 * unit, color,
                 stroke);
            line(renderer, left + 4 * unit, top + 5 * unit, left + 9 * unit, top + 5 * unit, color,
                 stroke);
            return;
        }
        line(renderer, x - 3 * unit, top + 1 * unit, x + 3 * unit, top + 1 * unit, color, stroke);
        line(renderer, x, top + 1 * unit, x, top + 4 * unit, color, stroke);
        line(renderer, x, y + unit, x, y - 4 * unit, color, stroke);
        line(renderer, x, y + unit, x + 3 * unit, y + 3 * unit, color, stroke);
        return;
    }
    if (symbol == GLYPH_SPEAKER || symbol == GLYPH_MUTED) {
        PointF points[] = {{(REAL)(left + 2 * unit), (REAL)(top + 9 * unit)},
                           {(REAL)(left + 6 * unit), (REAL)(top + 9 * unit)},
                           {(REAL)(left + 12 * unit), (REAL)(top + 4 * unit)},
                           {(REAL)(left + 12 * unit), (REAL)(top + 20 * unit)},
                           {(REAL)(left + 6 * unit), (REAL)(top + 15 * unit)},
                           {(REAL)(left + 2 * unit), (REAL)(top + 15 * unit)}};
        polygon(renderer, points, 6, color);
        if (symbol == GLYPH_MUTED) {
            line(renderer, left + 16 * unit, top + 9 * unit, left + 22 * unit, top + 15 * unit,
                 color, stroke);
            line(renderer, left + 16 * unit, top + 15 * unit, left + 22 * unit, top + 9 * unit,
                 color, stroke);
        } else {
            arc(renderer,
                (RectF){(REAL)(left + 10 * unit), (REAL)(top + 7 * unit), (REAL)(8 * unit),
                        (REAL)(10 * unit)},
                -60, 120, color, stroke);
            arc(renderer,
                (RectF){(REAL)(left + 7 * unit), (REAL)(top + 3 * unit), (REAL)(16 * unit),
                        (REAL)(18 * unit)},
                -50, 100, color, stroke);
        }
        return;
    }
    if (symbol == GLYPH_BATTERY) {
        outline(renderer,
                (RectF){(REAL)(left + unit), (REAL)(top + 6 * unit), (REAL)(19 * unit),
                        (REAL)(12 * unit)},
                2 * unit, color, stroke);
        fill(renderer,
             (RectF){(REAL)(left + 4 * unit), (REAL)(top + 9 * unit), (REAL)(11 * unit),
                     (REAL)(6 * unit)},
             unit, color);
        line(renderer, left + 23 * unit, top + 10 * unit, left + 23 * unit, top + 14 * unit, color,
             stroke);
        return;
    }
    if (symbol == GLYPH_POWER) {
        PointF points[] = {{(REAL)(left + 14 * unit), (REAL)(top + 2 * unit)},
                           {(REAL)(left + 5 * unit), (REAL)(top + 14 * unit)},
                           {(REAL)(left + 11 * unit), (REAL)(top + 14 * unit)},
                           {(REAL)(left + 9 * unit), (REAL)(top + 23 * unit)},
                           {(REAL)(left + 20 * unit), (REAL)(top + 10 * unit)},
                           {(REAL)(left + 13 * unit), (REAL)(top + 10 * unit)}};
        polygon(renderer, points, 6, color);
        return;
    }
    if (symbol == GLYPH_BELL) {
        arc(renderer,
            (RectF){(REAL)(left + 5 * unit), (REAL)(top + 3 * unit), (REAL)(14 * unit),
                    (REAL)(16 * unit)},
            180, 180, color, stroke);
        line(renderer, left + 5 * unit, top + 11 * unit, left + 4 * unit, top + 19 * unit, color,
             stroke);
        line(renderer, left + 19 * unit, top + 11 * unit, left + 20 * unit, top + 19 * unit, color,
             stroke);
        line(renderer, left + 4 * unit, top + 19 * unit, left + 20 * unit, top + 19 * unit, color,
             stroke);
        arc(renderer,
            (RectF){(REAL)(left + 10 * unit), (REAL)(top + 18 * unit), (REAL)(4 * unit),
                    (REAL)(4 * unit)},
            0, 180, color, stroke);
        return;
    }
    if (symbol == GLYPH_HEADPHONES) {
        arc(renderer,
            (RectF){(REAL)(left + 3 * unit), (REAL)(top + 3 * unit), (REAL)(18 * unit),
                    (REAL)(18 * unit)},
            180, 180, color, stroke);
        fill(renderer,
             (RectF){(REAL)(left + 2 * unit), (REAL)(top + 12 * unit), (REAL)(5 * unit),
                     (REAL)(9 * unit)},
             2 * unit, color);
        fill(renderer,
             (RectF){(REAL)(left + 17 * unit), (REAL)(top + 12 * unit), (REAL)(5 * unit),
                     (REAL)(9 * unit)},
             2 * unit, color);
        return;
    }
    if (symbol == GLYPH_CHECK) {
        line(renderer, x - 7 * unit, y, x - 2 * unit, y + 5 * unit, color, stroke);
        line(renderer, x - 2 * unit, y + 5 * unit, x + 8 * unit, y - 6 * unit, color, stroke);
        return;
    }
    line(renderer, left + 9 * unit, top + 5 * unit, left + 9 * unit, top + 18 * unit, color,
         stroke);
    line(renderer, left + 9 * unit, top + 5 * unit, left + 20 * unit, top + 3 * unit, color,
         stroke);
    line(renderer, left + 20 * unit, top + 3 * unit, left + 20 * unit, top + 16 * unit, color,
         stroke);
    fill(renderer,
         (RectF){(REAL)(left + 3 * unit), (REAL)(top + 16 * unit), (REAL)(7 * unit),
                 (REAL)(5 * unit)},
         2.5 * unit, color);
    fill(renderer,
         (RectF){(REAL)(left + 14 * unit), (REAL)(top + 14 * unit), (REAL)(7 * unit),
                 (REAL)(5 * unit)},
         2.5 * unit, color);
}

static int hit_add(App *app, RectF rect, Action action, bool enabled, const wchar_t *label) {
    if (app->hit_count == UI_HITS)
        return -1;
    int index = app->hit_count++;
    if (app->renderer.panel_layout) {
        rect.X = (REAL)(app->renderer.panel_x +
                        (rect.X - app->renderer.panel_x) * app->renderer.panel_scale);
        rect.Y = (REAL)(app->renderer.panel_y +
                        (rect.Y - app->renderer.panel_y) * app->renderer.panel_scale);
        rect.Width *= (REAL)app->renderer.panel_scale;
        rect.Height *= (REAL)app->renderer.panel_scale;
    }
    app->hits[index] = (UiHit){.rect = rect, .action = action, .enabled = enabled};
    wcsncpy(app->hits[index].label, label, 599);
    app->hits[index].label[599] = L'\0';
    return index;
}

static bool focused(const App *app, Action action) {
    return GetForegroundWindow() == app->window && app->focus >= 0 && app->focus < app->hit_count &&
           app->hits[app->focus].action == action;
}

static void button(App *app, RectF rect, Action action, const wchar_t *label,
                   const wchar_t *display, Glyph symbol, bool icon_only, bool enabled,
                   bool selected, bool add_hit) {
    Renderer *renderer = &app->renderer;
    if (add_hit)
        hit_add(app, rect, action, enabled, label);
    bool hot = enabled && (app->hover == action || app->pressed == action);
    ARGB background = selected ? 0xff28221bu : hot ? COLOR_HOVER : COLOR_CONTROL;
    ARGB foreground = enabled ? hot && icon_only ? COLOR_ACCENT : COLOR_TEXT : COLOR_SECONDARY;
    if (selected)
        foreground = COLOR_TIMER;
    double opacity = renderer->opacity;
    if (!enabled)
        renderer->opacity *= 0.45;
    fill(renderer, rect, icon_only ? rect.Width / 2 : 12, background);
    if (icon_only)
        glyph(renderer, symbol, rect.X + rect.Width / 2, rect.Y + rect.Height / 2, 20, foreground);
    else
        text(renderer, display, rect, FONT_LABEL, foreground, StringAlignmentCenter);
    if (focused(app, action)) {
        RectF ring = {rect.X - 2, rect.Y - 2, rect.Width + 4, rect.Height + 4};
        outline(renderer, ring, icon_only ? ring.Width / 2 : 14, COLOR_ACCENT, 2);
    }
    renderer->opacity = opacity;
}

static void slider(App *app, RectF track, Action action, double value, ARGB color, bool enabled,
                   const wchar_t *label, bool add_hit) {
    Renderer *renderer = &app->renderer;
    RectF target = {track.X - 4, track.Y - 12, track.Width + 8, 28};
    if (add_hit)
        hit_add(app, target, action, enabled, label);
    value = island_clamp(value, 0, 1);
    fill(renderer, track, track.Height / 2, COLOR_BORDER);
    if (enabled && value > 0)
        fill(renderer, (RectF){track.X, track.Y, (REAL)(track.Width * value), track.Height},
             track.Height / 2, color);
    if (enabled && action != ACTION_NONE &&
        (app->hover == action || app->pressed == action || focused(app, action))) {
        fill(renderer, (RectF){(REAL)(track.X + track.Width * value - 5), track.Y - 3, 10, 10}, 5,
             COLOR_TEXT);
        if (focused(app, action))
            outline(renderer, target, 10, COLOR_ACCENT, 1.5);
    }
}

static void artwork(Renderer *renderer, RectF rect, bool circular) {
    GpPath *path = round_path(rect, circular ? rect.Width / 2 : rect.Width > 40 ? 12 : 7);
    GraphicsState state;
    if (!path)
        return;
    GdipSaveGraphics(renderer->graphics, &state);
    GdipSetClipPath(renderer->graphics, path, CombineModeIntersect);
    fill(renderer, rect, 0, COLOR_CONTROL);
    if (renderer->artwork && renderer->opacity > 0.9) {
        UINT width = 0;
        UINT height = 0;
        GdipGetImageWidth(renderer->artwork, &width);
        GdipGetImageHeight(renderer->artwork, &height);
        REAL side = (REAL)(width < height ? width : height);
        GdipDrawImageRectRect(renderer->graphics, renderer->artwork, rect.X, rect.Y, rect.Width,
                              rect.Height, ((REAL)width - side) / 2, ((REAL)height - side) / 2,
                              side, side, UnitPixel, NULL, NULL, NULL);
    } else {
        glyph(renderer, GLYPH_MUSIC, rect.X + rect.Width / 2, rect.Y + rect.Height / 2,
              rect.Width * 0.48, COLOR_ACCENT);
    }
    GdipRestoreGraphics(renderer->graphics, state);
    GdipDeletePath(path);
}

static void media_background(App *app, RectF rect, double opacity) {
    Renderer *renderer = &app->renderer;
    if (!renderer->artwork || !app->config.artwork || !app->config.artwork_background ||
        opacity <= 0)
        return;
    UINT width = 0;
    UINT height = 0;
    GdipGetImageWidth(renderer->artwork, &width);
    GdipGetImageHeight(renderer->artwork, &height);
    if (!width || !height)
        return;
    double source_ratio = (double)width / height;
    double target_ratio = rect.Width / rect.Height;
    double source_width = source_ratio > target_ratio ? height * target_ratio : width;
    double source_height = source_ratio > target_ratio ? height : width / target_ratio;
    GpImageAttributes *attributes = NULL;
    if (GdipCreateImageAttributes(&attributes) != Ok)
        return;
    ColorMatrix matrix = {{{1, 0, 0, 0, 0},
                           {0, 1, 0, 0, 0},
                           {0, 0, 1, 0, 0},
                           {0, 0, 0, (REAL)(opacity * 0.6), 0},
                           {0, 0, 0, 0, 1}}};
    GdipSetImageAttributesColorMatrix(attributes, ColorAdjustTypeBitmap, TRUE, &matrix, NULL,
                                      ColorMatrixFlagsDefault);
    GdipDrawImageRectRect(renderer->graphics, renderer->artwork, rect.X, rect.Y, rect.Width,
                          rect.Height, (REAL)((width - source_width) / 2),
                          (REAL)((height - source_height) / 2), (REAL)source_width,
                          (REAL)source_height, UnitPixel, attributes, NULL, NULL);
    GdipDisposeImageAttributes(attributes);
    double saved = renderer->opacity;
    renderer->opacity = opacity;
    fill(renderer, rect, 0, 0x99000000u);
    renderer->opacity = saved;
}

static void waveform(App *app, double x, double y, double width, double height) {
    for (int i = 0; i < 5; ++i) {
        float peak = (float)app->wave_levels[i];
        double bar =
            app->media.playing ? app->reduced_motion ? 5 : 3 + sqrt(peak) * (height - 3) : 3;
        fill(&app->renderer,
             (RectF){(REAL)(x + i * width / 5), (REAL)(y + (height - bar) / 2), 3, (REAL)bar}, 1.5,
             COLOR_ACCENT);
    }
}

static void timer_ring(App *app, double x, double y, double size) {
    RectF rect = {(REAL)(x - size / 2), (REAL)(y - size / 2), (REAL)size, (REAL)size};
    arc(&app->renderer, rect, 0, 360, COLOR_BORDER, 2.5);
    arc(&app->renderer, rect, -90, 360 * island_timer_progress(&app->island), COLOR_TIMER, 2.5);
}

static void headphone_ring(App *app, const HeadphoneDevice *device, double x, double y, double size,
                           bool number) {
    Renderer *renderer = &app->renderer;
    bool known = device->bluetooth && app->config.headphone_battery &&
                 headphones_progress(device->battery_percent) >= 0;
    RectF rect = {(REAL)(x - size / 2), (REAL)(y - size / 2), (REAL)size, (REAL)size};
    double stroke = number ? 3.5 : 2;
    arc(renderer, rect, 0, 360, known ? COLOR_CONTROL : COLOR_SECONDARY, stroke);
    if (known)
        arc(renderer, rect, -90, 360 * headphones_progress(device->battery_percent),
            device->battery_percent <= 20 ? COLOR_TIMER : COLOR_GOOD, stroke);
    if (number) {
        wchar_t value[16];
        if (known)
            swprintf(value, 16, L"%d", device->battery_percent);
        else
            wcscpy(value, L"?");
        text(renderer, value, rect, FONT_TITLE, COLOR_TEXT, StringAlignmentCenter);
    } else if (!known) {
        line(renderer, x - 2, y, x + 2, y, COLOR_SECONDARY, 1.5);
    }
}

static double headphone_status(App *app, const HeadphoneDevice *device, bool idle, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    double middle = body.Y + body.Height / 2;
    double size = fmin(24, fmax(12, body.Height - 10));
    bool battery = device->bluetooth && app->config.headphone_battery;
    double right = body.X + body.Width - 10;
    double icon_x =
        idle ? body.X + 10 + size / 2 : right - (battery ? size * 2 + 10 : size) + size / 2;
    earbuds(renderer, icon_x, middle, size);
    if (battery)
        headphone_ring(app, device, right - size / 2, middle, size, false);
    if (add_hits && !idle) {
        wchar_t detail[160];
        wchar_t label[360];
        headphones_detail(device, app->config.headphone_battery, detail, 160);
        swprintf(label, 360, L"%ls. %ls. Open system controls.", device->name, detail);
        hit_add(app,
                (RectF){(REAL)(icon_x - size / 2 - 4), body.Y,
                        (REAL)(right - icon_x + size / 2 + 4), body.Height},
                ACTION_TAB_SYSTEM, true, label);
    }
    return idle ? 0 : battery ? size * 2 + 20 : size + 12;
}

static void compact(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    double middle = body.Y + body.Height / 2;
    wchar_t label[600];
    app_summary(app, label, 600);
    if (add_hits)
        hit_add(app, body, app->island.alert == ALERT_TIMER ? ACTION_TIMER_DISMISS : ACTION_EXPAND,
                true, label);
    if (app->island.alert == ALERT_TIMER) {
        glyph(renderer, GLYPH_TIMER, body.X + 32, middle, 25, COLOR_TIMER);
        text(renderer, L"Time is up", (RectF){body.X + 60, body.Y + 13, body.Width - 80, 25},
             FONT_TITLE, COLOR_TEXT, StringAlignmentNear);
        text(renderer, L"Click to dismiss", (RectF){body.X + 60, body.Y + 39, body.Width - 80, 22},
             FONT_BODY, COLOR_SECONDARY, StringAlignmentNear);
        return;
    }
    if (app->island.alert == ALERT_VOLUME) {
        wchar_t value[64];
        swprintf(value, 64, app->system.muted ? L"Muted" : L"Volume %d%%",
                 (int)lround(app->system.volume * 100));
        glyph(renderer, app->system.muted ? GLYPH_MUTED : GLYPH_SPEAKER, body.X + 29, middle, 23,
              COLOR_TEXT);
        text(renderer, value, (RectF){body.X + 57, body.Y + 9, body.Width - 80, 25}, FONT_LABEL,
             COLOR_TEXT, StringAlignmentNear);
        slider(app, (RectF){body.X + 59, body.Y + 46, body.Width - 83, 4}, ACTION_VOLUME,
               app->system.muted ? 0 : app->system.volume, COLOR_TEXT, app->system.audio_available,
               L"System volume", false);
        return;
    }
    if (app->island.alert == ALERT_POWER) {
        const wchar_t *title = app->system.plugged_in ? L"Power connected" : L"On battery";
        if (app->system.charging)
            title = L"Charging";
        glyph(renderer, app->system.plugged_in ? GLYPH_POWER : GLYPH_BATTERY, body.X + 30, middle,
              25, COLOR_GOOD);
        text(renderer, title, (RectF){body.X + 59, body.Y + 10, body.Width - 80, 24}, FONT_LABEL,
             COLOR_TEXT, StringAlignmentNear);
        wchar_t detail[80];
        if (app->system.battery_percent >= 0)
            swprintf(detail, 80, L"%d%% battery", app->system.battery_percent);
        else
            wcscpy(detail, app->system.plugged_in ? L"External power" : L"Power source changed");
        text(renderer, detail, (RectF){body.X + 59, body.Y + 34, body.Width - 80, 22}, FONT_BODY,
             COLOR_SECONDARY, StringAlignmentNear);
        return;
    }
    if (app->island.alert == ALERT_NOTIFICATION) {
        const AppNotification *notification = &app->notification_alert;
        glyph(renderer, GLYPH_BELL, body.X + 31, middle, 25, COLOR_ACCENT);
        text(renderer, notification->source, (RectF){body.X + 58, body.Y + 9, body.Width - 80, 21},
             FONT_SMALL, COLOR_SECONDARY, StringAlignmentNear);
        const wchar_t *title =
            app->config.notification_content ? notification->title : L"New notification";
        text(renderer, title, (RectF){body.X + 58, body.Y + 31, body.Width - 80, 28}, FONT_LABEL,
             COLOR_TEXT, StringAlignmentNear);
        return;
    }
    if (app->island.alert == ALERT_HEADPHONES) {
        const HeadphoneEvent *event = &app->headphone_event;
        double size = fmin(42, body.Height - 24);
        earbuds(renderer, body.X + 34, middle, size);
        const wchar_t *status = event->change == HEADPHONE_DISCONNECTED ? L"Disconnected"
                                : event->change == HEADPHONE_BATTERY    ? L"Battery"
                                                                        : L"Connected";
        REAL right_space = event->device.bluetooth && app->config.headphone_battery ? 78 : 22;
        text(renderer, status,
             (RectF){body.X + 65, (REAL)(middle - 23), body.Width - 65 - right_space, 21},
             FONT_SMALL, COLOR_SECONDARY, StringAlignmentNear);
        text(renderer, event->device.name,
             (RectF){body.X + 65, (REAL)(middle - 1), body.Width - 65 - right_space, 25},
             FONT_TITLE, COLOR_TEXT, StringAlignmentNear);
        if (event->device.bluetooth && app->config.headphone_battery &&
            event->change != HEADPHONE_DISCONNECTED)
            headphone_ring(app, &event->device, body.X + body.Width - 42, middle,
                           fmin(48, body.Height - 22), true);
        return;
    }
    IslandActivity primary = app_primary(app);
    const HeadphoneDevice *headphones = app_headphones(app);
    if (headphones && primary == ACTIVITY_NONE) {
        headphone_status(app, headphones, true, false);
        return;
    }
    if (primary == ACTIVITY_MEDIA) {
        REAL icon_size = (REAL)fmin(28, fmax(10, body.Height - 12));
        RectF icon = {body.X + 8, (REAL)(middle - icon_size / 2), icon_size, icon_size};
        if (app->config.artwork)
            artwork(renderer, icon, true);
        else {
            fill(renderer, icon, icon_size / 2, COLOR_CONTROL);
            glyph(renderer, GLYPH_MUSIC, icon.X + icon_size / 2, middle, icon_size * 0.6,
                  COLOR_ACCENT);
        }
        double peek = island_clamp(app->media_peek.value, 0, 1);
        REAL text_left = icon.X + icon_size + 10;
        REAL text_right = body.X + body.Width - (app->config.waveform ? 46 : 8);
        if (peek > 0.01 && text_right > text_left) {
            double opacity = renderer->opacity;
            renderer->opacity *= peek;
            text(renderer, app->media.title,
                 (RectF){text_left, body.Y + 4, text_right - text_left, body.Height - 8},
                 FONT_LABEL, COLOR_TEXT, StringAlignmentNear);
            renderer->opacity = opacity;
        }
        if (app->config.waveform) {
            double wave_height = fmin(22, fmax(3, body.Height - 12));
            waveform(app, body.X + body.Width - 38, middle - wave_height / 2, 28, wave_height);
        }
    } else if (primary == ACTIVITY_TIMER) {
        wchar_t clock[32];
        app_clock(app->island.timer.remaining, clock, 32);
        glyph(renderer, GLYPH_TIMER, body.X + 25, middle, 19, COLOR_TIMER);
        text(renderer, clock, (RectF){body.X + 46, (REAL)(middle - 14), body.Width - 91, 28},
             FONT_TITLE, COLOR_TEXT, StringAlignmentCenter);
        timer_ring(app, body.X + body.Width - 28, middle, 23);
        if (app->island.timer.paused)
            glyph(renderer, GLYPH_PAUSE, body.X + body.Width - 28, middle, 11, COLOR_TIMER);
    } else {
        wchar_t idle_label[256];
        app_idle_label(app, idle_label, 256);
        double side = app->config.camera_dot ? 30 : 14;
        RectF rect = {(REAL)(body.X + side), (REAL)(middle - body.Height / 2),
                      (REAL)(body.Width - side * 2), body.Height};
        GpSolidFill *brush = NULL;
        if (renderer->idle_font && GdipCreateSolidFill(ink(renderer, COLOR_TEXT), &brush) == Ok) {
            GdipSetStringFormatAlign(renderer->text_format, StringAlignmentCenter);
            GdipDrawString(renderer->graphics, idle_label, -1, renderer->idle_font, &rect,
                           renderer->text_format, (GpBrush *)brush);
            GdipDeleteBrush((GpBrush *)brush);
        }
        if (app->config.camera_dot) {
            fill(renderer, (RectF){body.X + body.Width - 24, (REAL)(middle - 5), 10, 10}, 5,
                 0xff11131bu);
            outline(renderer, (RectF){body.X + body.Width - 24, (REAL)(middle - 5), 10, 10}, 5,
                    0xff242832u, 1);
            fill(renderer, (RectF){body.X + body.Width - 21, (REAL)(middle - 3), 3, 3}, 1.5,
                 0xff35436bu);
        }
    }
}

static void header(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    static const Action actions[] = {ACTION_TAB_MEDIA, ACTION_TAB_TIMER, ACTION_TAB_SYSTEM,
                                     ACTION_TAB_NOTIFICATIONS};
    static const wchar_t *labels[] = {L"Playing", L"Timer", L"System", L"Alerts"};
    static const REAL widths[] = {63, 51, 57, 51};
    REAL x = body.X + 12;
    for (int i = 0; i < 4; ++i) {
        RectF rect = {x, body.Y + 11, widths[i], 29};
        bool selected = (i < 3 && (int)app->island.view == i) ||
                        (i == 3 && app->island.view == ISLAND_NOTIFICATIONS);
        if (add_hits)
            hit_add(app, rect, actions[i], true, labels[i]);
        if (selected || app->hover == actions[i])
            fill(renderer, rect, 10, selected ? COLOR_CONTROL : COLOR_HOVER);
        text(renderer, labels[i], rect, FONT_LABEL, selected ? COLOR_TEXT : COLOR_SECONDARY,
             StringAlignmentCenter);
        if (focused(app, actions[i]))
            outline(renderer, (RectF){rect.X - 1, rect.Y - 1, rect.Width + 2, rect.Height + 2}, 11,
                    COLOR_ACCENT, 1.5);
        x += widths[i] + 5;
    }
    if (app->config.search_button)
        button(app, (RectF){body.X + body.Width - 82, body.Y + 10, 30, 30}, ACTION_SEARCH,
               L"Open Windows search", NULL, GLYPH_SEARCH, true, true, false, add_hits);
    button(app, (RectF){body.X + body.Width - 120, body.Y + 10, 30, 30}, ACTION_SETTINGS,
           L"Customize DynamicWindows", NULL, GLYPH_SETTINGS, true, true, false, add_hits);
    button(app, (RectF){body.X + body.Width - 44, body.Y + 10, 30, 30}, ACTION_CLOSE,
           L"Collapse island", NULL, GLYPH_CLOSE, true, true, false, add_hits);
    line(renderer, body.X + 22, body.Y + 51, body.X + body.Width - 22, body.Y + 51, COLOR_BORDER,
         1);
}

static void volume_row(App *app, double y, bool add_hits) {
    RectF body = app->renderer.body;
    wchar_t label[80];
    swprintf(label, 80, L"System volume %d%%. Left and Right arrows adjust volume.",
             (int)lround(app->system.volume * 100));
    button(app, (RectF){body.X + 20, (REAL)(y - 14), 28, 28}, ACTION_MUTE,
           app->system.muted ? L"Unmute system audio" : L"Mute system audio", NULL,
           app->system.muted ? GLYPH_MUTED : GLYPH_SPEAKER, true, app->system.audio_available,
           false, add_hits);
    slider(app, (RectF){body.X + 62, (REAL)(y - 2), body.Width - 137, 4}, ACTION_VOLUME,
           app->system.muted ? 0 : app->system.volume, COLOR_TEXT, app->system.audio_available,
           label, add_hits);
    wchar_t value[24];
    if (app->system.audio_available)
        swprintf(value, 24, L"%d%%", (int)lround(app->system.volume * 100));
    else
        wcscpy(value, L"N/A");
    text(&app->renderer, value, (RectF){body.X + body.Width - 66, (REAL)(y - 12), 43, 24},
         FONT_SMALL, COLOR_SECONDARY, StringAlignmentFar);
}

static void media_panel(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    if (!app->media.available) {
        fill(renderer, (RectF){body.X + body.Width / 2 - 27, body.Y + 74, 54, 54}, 18,
             COLOR_CONTROL);
        glyph(renderer, GLYPH_MUSIC, body.X + body.Width / 2, body.Y + 101, 29, COLOR_ACCENT);
        const wchar_t *title = app->media.initialized ? L"Nothing playing" : L"Connecting to media";
        if (FAILED(app->media.error))
            title = L"Media service unavailable";
        text(renderer, title, (RectF){body.X + 24, body.Y + 137, body.Width - 48, 28}, FONT_TITLE,
             COLOR_TEXT, StringAlignmentCenter);
        text(renderer, L"Play audio in an app with Windows media support.",
             (RectF){body.X + 24, body.Y + 169, body.Width - 48, 23}, FONT_SMALL, COLOR_SECONDARY,
             StringAlignmentCenter);
        button(app, (RectF){body.X + body.Width / 2 - 99, body.Y + 205, 198, 32}, ACTION_SEARCH,
               L"Open Windows search", L"Open Windows search", GLYPH_SEARCH, false, true, false,
               add_hits);
        if (*app->notice)
            text(renderer, app->notice, (RectF){body.X + 20, body.Y + 239, body.Width - 40, 20},
                 FONT_SMALL, COLOR_SECONDARY, StringAlignmentCenter);
        return;
    }
    if (!app->config.artwork_background) {
        if (app->config.artwork)
            artwork(renderer, (RectF){body.X + 23, body.Y + 69, 58, 58}, false);
        else
            glyph(renderer, GLYPH_MUSIC, body.X + 52, body.Y + 98, 30, COLOR_ACCENT);
    }
    REAL text_left = body.X + (app->config.artwork_background ? 24 : 96);
    REAL text_width = body.Width - (app->config.artwork_background ? 48 : 120);
    wchar_t track[600];
    swprintf(track, 600, L"%ls. %ls", app->media.title, app->media.artist);
    if (add_hits)
        hit_add(app, (RectF){text_left, body.Y + 68, text_width, 64}, ACTION_TRACK_INFO, true,
                track);
    text(renderer, app->media.title, (RectF){text_left, body.Y + 68, text_width, 27}, FONT_TITLE,
         COLOR_TEXT, StringAlignmentNear);
    text(renderer, *app->media.artist ? app->media.artist : L"Unknown artist",
         (RectF){text_left, body.Y + 97, text_width, 21}, FONT_BODY, COLOR_SECONDARY,
         StringAlignmentNear);
    if (app->config.media_source) {
        wchar_t source[160];
        app_media_name(app, source, 160);
        text(renderer, source, (RectF){text_left, body.Y + 119, text_width, 18}, FONT_SMALL,
             COLOR_SECONDARY, StringAlignmentNear);
    }
    double position = app->media_display_position;
    if (app->test_mode)
        position = app->media.position;
    position = island_clamp(position, 0, app->media.duration > 0 ? app->media.duration : 0);
    double progress = app->media.duration > 0 ? position / app->media.duration : 0;
    if (app->config.media_progress)
        slider(app, (RectF){body.X + 24, body.Y + 151, body.Width - 48, 3}, ACTION_SEEK, progress,
               COLOR_SECONDARY, app->media.can_seek && app->media.duration > 0,
               L"Playback position. Left and Right arrows seek five seconds.", add_hits);
    wchar_t elapsed[32];
    wchar_t duration[32];
    app_clock((uint64_t)position * 1000, elapsed, 32);
    app_clock((uint64_t)app->media.duration * 1000, duration, 32);
    if (app->media.duration <= 0)
        wcscpy(duration, L"LIVE");
    if (app->config.media_progress) {
        text(renderer, elapsed, (RectF){body.X + 24, body.Y + 157, 80, 19}, FONT_SMALL,
             COLOR_SECONDARY, StringAlignmentNear);
        text(renderer, duration, (RectF){body.X + body.Width - 104, body.Y + 157, 80, 19},
             FONT_SMALL, COLOR_SECONDARY, StringAlignmentFar);
    }
    REAL center = body.X + body.Width / 2;
    button(app, (RectF){center - 82, body.Y + 185, 36, 36}, ACTION_PREVIOUS, L"Previous track",
           NULL, GLYPH_PREVIOUS, true, app->media.can_previous, false, add_hits);
    button(app, (RectF){center - 23, body.Y + 180, 46, 46}, ACTION_PLAY,
           app->media.playing ? L"Pause playback" : L"Resume playback", NULL,
           app->media.playing ? GLYPH_PAUSE : GLYPH_PLAY, true, app->media.can_toggle, false,
           add_hits);
    button(app, (RectF){center + 46, body.Y + 185, 36, 36}, ACTION_NEXT, L"Next track", NULL,
           GLYPH_NEXT, true, app->media.can_next, false, add_hits);
    volume_row(app, body.Y + 245, add_hits);
    if (*app->notice)
        text(renderer, app->notice, (RectF){body.X + 20, body.Y + 222, body.Width - 40, 18},
             FONT_SMALL, COLOR_SECONDARY, StringAlignmentCenter);
}

static void timer_panel(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    IslandTimer *timer = &app->island.timer;
    wchar_t clock[32];
    app_clock(timer->remaining, clock, 32);
    text(renderer, clock, (RectF){body.X + 24, body.Y + 65, body.Width - 48, 65}, FONT_TIMER,
         COLOR_TEXT, StringAlignmentCenter);
    const wchar_t *status = timer->finished  ? L"Time is up"
                            : timer->paused  ? L"Countdown paused"
                            : timer->running ? L"Countdown running"
                                             : L"Choose a duration";
    text(renderer, status, (RectF){body.X + 24, body.Y + 130, body.Width - 48, 24}, FONT_BODY,
         COLOR_SECONDARY, StringAlignmentCenter);
    slider(app, (RectF){body.X + 76, body.Y + 164, body.Width - 152, 3}, ACTION_NONE,
           island_timer_progress(&app->island), COLOR_TIMER, true, L"Time remaining", false);
    static const uint64_t durations[] = {60000, 300000, 900000, 1500000};
    static const Action actions[] = {ACTION_TIMER_1, ACTION_TIMER_5, ACTION_TIMER_15,
                                     ACTION_TIMER_25};
    static const wchar_t *labels[] = {L"1 minute", L"5 minutes", L"15 minutes", L"25 minutes"};
    static const wchar_t *displays[] = {L"1 min", L"5 min", L"15 min", L"25 min"};
    REAL width = (body.Width - 72) / 4;
    for (int i = 0; i < 4; ++i)
        button(app, (RectF){body.X + 24 + i * (width + 8), body.Y + 185, width, 30}, actions[i],
               labels[i], displays[i], GLYPH_TIMER, false, !timer->running && !timer->finished,
               timer->duration == durations[i], add_hits);
    const wchar_t *display = timer->finished  ? L"Done"
                             : timer->paused  ? L"Resume"
                             : timer->running ? L"Pause"
                                              : L"Start timer";
    button(app, (RectF){body.X + body.Width / 2 - 82, body.Y + 227, 124, 32}, ACTION_TIMER_TOGGLE,
           display, display, GLYPH_TIMER, false, true, true, add_hits);
    button(app, (RectF){body.X + body.Width / 2 + 50, body.Y + 227, 32, 32}, ACTION_TIMER_RESET,
           L"Reset timer", NULL, GLYPH_RESET, true, timer->running || timer->finished, false,
           add_hits);
}

static void system_panel(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    text(renderer, L"Sound output", (RectF){body.X + 24, body.Y + 67, body.Width - 48, 24},
         FONT_LABEL, COLOR_TEXT, StringAlignmentNear);
    text(renderer,
         app->system.audio_available ? app->system.output_name : L"No audio output detected",
         (RectF){body.X + 24, body.Y + 94, body.Width - 48, 22}, FONT_SMALL, COLOR_SECONDARY,
         StringAlignmentNear);
    const HeadphoneDevice *connected = headphones_select(&app->headphones, app->platform.device_id);
    if (connected) {
        const HeadphoneDevice *device = connected;
        wchar_t detail[320];
        wchar_t connection[160];
        headphones_detail(device, app->config.headphone_battery, connection, 160);
        swprintf(detail, 320, L"%ls · %ls", device->name, connection);
        text(renderer, detail, (RectF){body.X + 24, body.Y + 115, body.Width - 48, 18}, FONT_SMALL,
             COLOR_ACCENT, StringAlignmentNear);
    }
    volume_row(app, body.Y + 145, add_hits);
    line(renderer, body.X + 24, body.Y + 170, body.X + body.Width - 24, body.Y + 170, COLOR_BORDER,
         1);
    glyph(renderer, app->system.plugged_in ? GLYPH_POWER : GLYPH_BATTERY, body.X + 37, body.Y + 203,
          25, app->system.plugged_in ? COLOR_GOOD : COLOR_TEXT);
    wchar_t power[64];
    const wchar_t *detail;
    if (!app->system.power_known) {
        wcscpy(power, L"Power status unavailable");
        detail = L"Windows did not report a power source";
    } else if (!app->system.has_battery) {
        wcscpy(power, L"Desktop power");
        detail = L"No battery reported by Windows";
    } else {
        if (app->system.battery_percent >= 0)
            swprintf(power, 64, L"%d%% battery", app->system.battery_percent);
        else
            wcscpy(power, L"Battery level unavailable");
        detail = app->system.charging     ? L"Charging"
                 : app->system.plugged_in ? L"Connected to power"
                                          : L"Running on battery";
    }
    text(renderer, power, (RectF){body.X + 67, body.Y + 184, body.Width - 91, 25}, FONT_LABEL,
         COLOR_TEXT, StringAlignmentNear);
    text(renderer, detail, (RectF){body.X + 67, body.Y + 211, body.Width - 91, 21}, FONT_SMALL,
         COLOR_SECONDARY, StringAlignmentNear);
    text(renderer, *app->notice ? app->notice : L"Ctrl + Alt + D opens the island",
         (RectF){body.X + 20, body.Y + 240, body.Width - 40, 20}, FONT_SMALL, COLOR_SECONDARY,
         StringAlignmentCenter);
}

static void notification_panel(App *app, bool add_hits) {
    Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    if (!app->notification_count) {
        glyph(renderer, GLYPH_BELL, body.X + body.Width / 2, body.Y + 103, 34, COLOR_ACCENT);
        text(renderer, L"No captured notifications",
             (RectF){body.X + 24, body.Y + 135, body.Width - 48, 30}, FONT_TITLE, COLOR_TEXT,
             StringAlignmentCenter);
        wchar_t status[256];
        notifications_status(&app->notification_service, status, 256);
        text(renderer, status, (RectF){body.X + 28, body.Y + 169, body.Width - 56, 42}, FONT_BODY,
             COLOR_SECONDARY, StringAlignmentCenter);
        return;
    }
    const AppNotification *notification = &app->notifications[app->notification_index];
    glyph(renderer, GLYPH_BELL, body.X + 41, body.Y + 91, 28, COLOR_ACCENT);
    text(renderer, notification->source, (RectF){body.X + 66, body.Y + 67, body.Width - 90, 24},
         FONT_LABEL, COLOR_SECONDARY, StringAlignmentNear);
    const wchar_t *title =
        app->config.notification_content ? notification->title : L"Notification content hidden";
    text(renderer, title, (RectF){body.X + 24, body.Y + 104, body.Width - 48, 31}, FONT_TITLE,
         COLOR_TEXT, StringAlignmentNear);
    const wchar_t *body_text = app->config.notification_content
                                   ? notification->body
                                   : L"Enable content in Settings to read it.";
    text(renderer, body_text, (RectF){body.X + 24, body.Y + 139, body.Width - 48, 56}, FONT_BODY,
         COLOR_SECONDARY, StringAlignmentNear);
    wchar_t position[40];
    swprintf(position, 40, L"%u of %u", app->notification_index + 1, app->notification_count);
    text(renderer, position, (RectF){body.X + body.Width / 2 - 45, body.Y + 215, 90, 28},
         FONT_SMALL, COLOR_SECONDARY, StringAlignmentCenter);
    button(app, (RectF){body.X + body.Width / 2 - 90, body.Y + 211, 32, 32},
           ACTION_NOTIFICATION_PREVIOUS, L"Older notification", NULL, GLYPH_PREVIOUS, true,
           app->notification_index + 1 < app->notification_count, false, add_hits);
    button(app, (RectF){body.X + body.Width / 2 + 58, body.Y + 211, 32, 32},
           ACTION_NOTIFICATION_NEXT, L"Newer notification", NULL, GLYPH_NEXT, true,
           app->notification_index > 0, false, add_hits);
}

static void release_surface(Renderer *renderer) {
    if (renderer->graphics)
        GdipDeleteGraphics(renderer->graphics);
    if (renderer->image)
        GdipDisposeImage((GpImage *)renderer->image);
    if (renderer->dc && renderer->old_bitmap)
        SelectObject(renderer->dc, renderer->old_bitmap);
    if (renderer->bitmap)
        DeleteObject(renderer->bitmap);
    if (renderer->dc)
        DeleteDC(renderer->dc);
    renderer->graphics = NULL;
    renderer->image = NULL;
    renderer->bitmap = NULL;
    renderer->dc = NULL;
    renderer->old_bitmap = NULL;
    renderer->pixels = NULL;
}

bool renderer_resize(App *app) {
    Renderer *renderer = &app->renderer;
    release_surface(renderer);
    renderer->width = (int)ceil(app->canvas_width * app->scale);
    renderer->height = (int)ceil(app->canvas_height * app->scale);
    BITMAPINFO info = {.bmiHeader = {.biSize = sizeof(BITMAPINFOHEADER),
                                     .biWidth = renderer->width,
                                     .biHeight = -renderer->height,
                                     .biPlanes = 1,
                                     .biBitCount = 32,
                                     .biCompression = BI_RGB}};
    renderer->dc = CreateCompatibleDC(NULL);
    if (!renderer->dc)
        return false;
    renderer->bitmap =
        CreateDIBSection(renderer->dc, &info, DIB_RGB_COLORS, (void **)&renderer->pixels, NULL, 0);
    if (!renderer->bitmap)
        return false;
    renderer->old_bitmap = SelectObject(renderer->dc, renderer->bitmap);
    if (GdipCreateBitmapFromScan0(renderer->width, renderer->height, renderer->width * 4,
                                  PixelFormat32bppPARGB, renderer->pixels,
                                  &renderer->image) != Ok ||
        GdipGetImageGraphicsContext((GpImage *)renderer->image, &renderer->graphics) != Ok)
        return false;
    GdipSetSmoothingMode(renderer->graphics, SmoothingModeAntiAlias);
    GdipSetTextRenderingHint(renderer->graphics, TextRenderingHintAntiAliasGridFit);
    GdipSetInterpolationMode(renderer->graphics, InterpolationModeHighQualityBicubic);
    GdipSetPixelOffsetMode(renderer->graphics, PixelOffsetModeHighQuality);
    GdipScaleWorldTransform(renderer->graphics, (REAL)app->scale, (REAL)app->scale,
                            MatrixOrderAppend);
    return true;
}

bool renderer_style(App *app) {
    Renderer *renderer = &app->renderer;
    GpFontFamily *family = NULL;
    GpFont *font = NULL;
    if (GdipCreateFontFamilyFromName(app->config.idle_font, NULL, &family) != Ok &&
        GdipGetGenericFontFamilySansSerif(&family) != Ok)
        return false;
    if (GdipCreateFont(family, (REAL)app->config.idle_font_size, FontStyleRegular, UnitPixel,
                       &font) != Ok) {
        GdipDeleteFontFamily(family);
        return false;
    }
    if (renderer->idle_font)
        GdipDeleteFont(renderer->idle_font);
    if (renderer->idle_family)
        GdipDeleteFontFamily(renderer->idle_family);
    renderer->idle_family = family;
    renderer->idle_font = font;
    memcpy(renderer->colors, app->config.colors, sizeof(renderer->colors));
    renderer->border_width = app->config.border_width;
    renderer->shadow = app->config.shadow / 100.0;
    return true;
}

bool renderer_init(App *app) {
    Renderer *renderer = &app->renderer;
    GdiplusStartupInput input = {.GdiplusVersion = 1};
    if (GdiplusStartup(&renderer->token, &input, NULL) != Ok)
        return false;
    if (GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &renderer->family) != Ok &&
        GdipGetGenericFontFamilySansSerif(&renderer->family) != Ok)
        return false;
    static const REAL sizes[] = {12, 14, 13, 18, 24, 48};
    for (int i = 0; i < FONT_COUNT; ++i) {
        INT style = i == FONT_TITLE || i == FONT_LABEL ? FontStyleBold : FontStyleRegular;
        if (GdipCreateFont(renderer->family, sizes[i], style, UnitPixel, &renderer->fonts[i]) != Ok)
            return false;
    }
    if (GdipCreateStringFormat(StringFormatFlagsNoWrap, LANG_NEUTRAL, &renderer->text_format) != Ok)
        return false;
    GdipSetStringFormatLineAlign(renderer->text_format, StringAlignmentCenter);
    GdipSetStringFormatTrimming(renderer->text_format, StringTrimmingEllipsisCharacter);
    return earbuds_init(app) && renderer_style(app) && renderer_resize(app);
}

void renderer_artwork(App *app, const BYTE *data, UINT size) {
    Renderer *renderer = &app->renderer;
    if (renderer->artwork)
        GdipDisposeImage(renderer->artwork);
    if (renderer->artwork_stream)
        IStream_Release(renderer->artwork_stream);
    renderer->artwork = NULL;
    renderer->artwork_stream = NULL;
    if (!data || size == 0 || size > 8 * 1024 * 1024)
        return;
    renderer->artwork_stream = SHCreateMemStream(data, size);
    if (!renderer->artwork_stream ||
        GdipLoadImageFromStream(renderer->artwork_stream, &renderer->artwork) != Ok)
        return;
    UINT width = 0;
    UINT height = 0;
    GdipGetImageWidth(renderer->artwork, &width);
    GdipGetImageHeight(renderer->artwork, &height);
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        GdipDisposeImage(renderer->artwork);
        renderer->artwork = NULL;
    }
}

void renderer_destroy(Renderer *renderer) {
    release_surface(renderer);
    if (renderer->artwork)
        GdipDisposeImage(renderer->artwork);
    if (renderer->artwork_stream)
        IStream_Release(renderer->artwork_stream);
    if (renderer->text_format)
        GdipDeleteStringFormat(renderer->text_format);
    for (unsigned i = 0; i < EARBUD_PATH_COUNT; ++i)
        if (renderer->earbuds[i])
            GdipDeletePath(renderer->earbuds[i]);
    for (int i = 0; i < FONT_COUNT; ++i)
        if (renderer->fonts[i])
            GdipDeleteFont(renderer->fonts[i]);
    if (renderer->idle_font)
        GdipDeleteFont(renderer->idle_font);
    if (renderer->idle_family)
        GdipDeleteFontFamily(renderer->idle_family);
    if (renderer->family)
        GdipDeleteFontFamily(renderer->family);
    if (renderer->token)
        GdiplusShutdown(renderer->token);
    *renderer = (Renderer){0};
}

HICON renderer_icon(App *app) {
    (void)app;
    GpBitmap *image = NULL;
    GpGraphics *graphics = NULL;
    GpSolidFill *brush = NULL;
    HICON icon = NULL;
    if (GdipCreateBitmapFromScan0(32, 32, 0, PixelFormat32bppARGB, NULL, &image) != Ok)
        return NULL;
    if (GdipGetImageGraphicsContext((GpImage *)image, &graphics) == Ok) {
        GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
        GdipGraphicsClear(graphics, 0);
        if (GdipCreateSolidFill(0xff101014u, &brush) == Ok) {
            GdipFillEllipse(graphics, (GpBrush *)brush, 1, 7, 30, 18);
            GdipSetSolidFillColor(brush, COLOR_ACCENT);
            GdipFillEllipse(graphics, (GpBrush *)brush, 22, 13, 6, 6);
            GdipSetSolidFillColor(brush, COLOR_TEXT);
            GdipFillRectangle(graphics, (GpBrush *)brush, 7, 14, 2, 5);
            GdipFillRectangle(graphics, (GpBrush *)brush, 11, 11, 2, 11);
            GdipFillRectangle(graphics, (GpBrush *)brush, 15, 13, 2, 7);
            GdipDeleteBrush((GpBrush *)brush);
        }
        GdipDeleteGraphics(graphics);
    }
    GdipCreateHICONFromBitmap(image, &icon);
    GdipDisposeImage((GpImage *)image);
    return icon;
}

bool renderer_draw(App *app) {
    Renderer *renderer = &app->renderer;
    if (!renderer->graphics)
        return false;
    GdipResetClip(renderer->graphics);
    GdipGraphicsClear(renderer->graphics, 0);
    renderer->opacity = 1;
    renderer->panel_layout = false;
    double width = island_clamp(app->width.value, 40, app->canvas_width - 20);
    double height = island_clamp(app->height.value, 20, app->canvas_height - 20);
    double bubble = island_clamp(app->bubble.value, 0, app->config.bubble_size * 1.12);
    double headphone_bubble =
        island_clamp(app->headphone_bubble.value, 0,
                     fmax(app->config.compact_height, app->config.timer_height) * 1.12);
    double gap = bubble > 0.5 ? app->config.bubble_gap : 0;
    double headphone_gap = headphone_bubble > 0.5 ? app->config.bubble_gap : 0;
    double total = width + (bubble > 0.5 ? bubble + gap : 0) +
                   (headphone_bubble > 0.5 ? headphone_bubble + headphone_gap : 0);
    double reference = config_reference_width(&app->config);
    double left = (app->canvas_width - reference) / 2;
    int horizontal = app->config.anchor % 3;
    int vertical = app->config.anchor / 3;
    double x = horizontal == 0   ? left
               : horizontal == 1 ? (app->canvas_width - total) / 2
                                 : left + reference - total;
    double y = UI_TOP + (vertical == 0   ? 0
                         : vertical == 1 ? (app->config.expanded_height - height) / 2
                                         : app->config.expanded_height - height);
    renderer->body = (RectF){(REAL)x, (REAL)y, (REAL)width, (REAL)height};
    renderer->radius = island_clamp(app->radius.value, 0, fmin(width, height) / 2);
    renderer->bubble = (RectF){(REAL)(x + width + gap), (REAL)(y + (height - bubble) / 2),
                               (REAL)bubble, (REAL)bubble};
    double headphone_x = x + width + (bubble > 0.5 ? bubble + gap : 0) + headphone_gap;
    renderer->headphone_bubble =
        (RectF){(REAL)headphone_x, (REAL)(y + (height - headphone_bubble) / 2),
                (REAL)headphone_bubble, (REAL)headphone_bubble};
    RectF body = renderer->body;
    double opacity = renderer->opacity;
    renderer->opacity *= renderer->shadow;
    for (int i = 3; i >= 1; --i)
        fill(renderer,
             (RectF){body.X - i * 2, body.Y + 3 - i, body.Width + i * 4, body.Height + i * 3},
             renderer->radius + i * 2, i == 1 ? 0x22000000u : 0x0b000000u);
    renderer->opacity = opacity;
    fill(renderer, body, renderer->radius, COLOR_SURFACE);
    if (renderer->border_width > 0)
        outline(renderer, (RectF){body.X + 0.5f, body.Y + 0.5f, body.Width - 1, body.Height - 1},
                fmax(0, renderer->radius - 0.5), COLOR_BORDER, renderer->border_width);
    app->hit_count = 0;
    GpPath *clip = round_path(body, renderer->radius);
    if (clip) {
        GdipSetClipPath(renderer->graphics, clip, CombineModeIntersect);
        GdipDeletePath(clip);
    }
    double opened = island_clamp(app->reveal.value, 0, 1);
    if (app->media.available && app->island.view == ISLAND_MEDIA)
        media_background(app, body, opened);
    if (opened < 0.999) {
        renderer->opacity = 1 - opened;
        compact(app, !app->island.expanded && opened < 0.25);
    }
    if (opened > 0.001) {
        renderer->opacity = opened;
        double scale = fmin(width / 408, height / 264);
        scale = island_clamp(scale, 0.1, 3.1);
        double panel_width = width / scale;
        double panel_height = 264;
        double panel_top = body.Y + (height - panel_height * scale) / 2;
        renderer->panel_x = body.X;
        renderer->panel_y = panel_top;
        renderer->panel_scale = scale;
        renderer->panel_layout = true;
        RectF physical_body = renderer->body;
        renderer->body = (RectF){body.X, (REAL)panel_top, (REAL)panel_width, 264};
        GraphicsState state;
        GdipSaveGraphics(renderer->graphics, &state);
        GdipTranslateWorldTransform(renderer->graphics, (REAL)renderer->panel_x,
                                    (REAL)renderer->panel_y, MatrixOrderPrepend);
        GdipScaleWorldTransform(renderer->graphics, (REAL)scale, (REAL)scale, MatrixOrderPrepend);
        GdipTranslateWorldTransform(renderer->graphics, (REAL)-renderer->panel_x,
                                    (REAL)-renderer->panel_y, MatrixOrderPrepend);
        bool add_hits = app->island.expanded && opened > 0.9;
        header(app, add_hits);
        if (app->island.view == ISLAND_TIMER)
            timer_panel(app, add_hits);
        else if (app->island.view == ISLAND_SYSTEM)
            system_panel(app, add_hits);
        else if (app->island.view == ISLAND_NOTIFICATIONS)
            notification_panel(app, add_hits);
        else
            media_panel(app, add_hits);
        GdipRestoreGraphics(renderer->graphics, state);
        renderer->body = physical_body;
        renderer->panel_layout = false;
    }
    GdipResetClip(renderer->graphics);
    renderer->opacity = island_clamp(bubble / app->config.bubble_size, 0, 1);
    if (bubble > 1) {
        fill(renderer, renderer->bubble, bubble / 2,
             app->hover == ACTION_SECONDARY ? COLOR_HOVER : COLOR_SURFACE);
        if (renderer->border_width > 0)
            outline(renderer, renderer->bubble, bubble / 2, COLOR_BORDER, renderer->border_width);
        IslandActivity secondary = app_secondary(app);
        double center_x = renderer->bubble.X + bubble / 2;
        double center_y = renderer->bubble.Y + bubble / 2;
        if (secondary == ACTIVITY_TIMER) {
            timer_ring(app, center_x, center_y, 26 * bubble / 44);
            glyph(renderer, app->island.timer.paused ? GLYPH_PAUSE : GLYPH_TIMER, center_x,
                  center_y, 15 * bubble / 44, COLOR_TIMER);
        } else {
            glyph(renderer, GLYPH_MUSIC, center_x, center_y, 20 * bubble / 44, COLOR_ACCENT);
        }
        if (secondary != ACTIVITY_NONE && bubble > 20)
            hit_add(app, renderer->bubble, ACTION_SECONDARY, true,
                    secondary == ACTIVITY_TIMER ? L"Expand countdown timer"
                                                : L"Expand media controls");
    }
    renderer->opacity = island_clamp(
        headphone_bubble / fmax(1, fmax(app->config.compact_height, app->config.timer_height)), 0,
        1);
    const HeadphoneDevice *connected = app_headphones(app);
    if (headphone_bubble > 1 && connected) {
        RectF rect = renderer->headphone_bubble;
        double center_x = rect.X + headphone_bubble / 2;
        double center_y = rect.Y + headphone_bubble / 2;
        fill(renderer, rect, headphone_bubble / 2,
             app->hover == ACTION_HEADPHONES ? COLOR_HOVER : COLOR_SURFACE);
        if (renderer->border_width > 0)
            outline(renderer, rect, headphone_bubble / 2, COLOR_BORDER, renderer->border_width);
        double ring = fmax(12, headphone_bubble - 8);
        headphone_ring(app, connected, center_x, center_y, ring, false);
        earbuds(renderer, center_x, center_y, fmax(10, headphone_bubble * 0.46));
        if (headphone_bubble > 20) {
            wchar_t detail[160];
            wchar_t label[360];
            headphones_detail(connected, app->config.headphone_battery, detail, 160);
            swprintf(label, 360, L"%ls. %ls. Open system controls.", connected->name, detail);
            hit_add(app, rect, ACTION_HEADPHONES, true, label);
        }
    }
    renderer->opacity = 1;
    if (app->focus >= app->hit_count)
        app->focus = -1;
    GdipFlush(renderer->graphics, FlushIntentionSync);
    RECT window;
    GetWindowRect(app->window, &window);
    double visible = island_clamp(app->visibility.value, 0, 1);
    double slide = (1 - visible) * (app->config.anchor / 3 == 2 ? 80 : -80) * app->scale;
    POINT destination = {app->base_position.x, app->base_position.y + (LONG)lround(slide)};
    POINT source = {0, 0};
    SIZE size = {renderer->width, renderer->height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)lround(app->config.opacity * 2.55 * visible),
                           AC_SRC_ALPHA};
    return UpdateLayeredWindow(app->window, NULL, &destination, &size, renderer->dc, &source, 0,
                               &blend, ULW_ALPHA) != FALSE;
}

bool renderer_save(App *app, const wchar_t *path) {
    static const CLSID png = {
        0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return GdipSaveImageToFile((GpImage *)app->renderer.image, path, &png, NULL) == Ok;
}

int app_hit(const App *app, double x, double y) {
    for (int i = app->hit_count - 1; i >= 0; --i) {
        RectF rect = app->hits[i].rect;
        if (x >= rect.X && y >= rect.Y && x <= rect.X + rect.Width && y <= rect.Y + rect.Height)
            return i;
    }
    return -1;
}

bool app_inside(const App *app, double x, double y) {
    const Renderer *renderer = &app->renderer;
    RectF body = renderer->body;
    if (island_contains(x - body.X, y - body.Y, body.Width, body.Height, renderer->radius))
        return true;
    RectF bubble = renderer->bubble;
    RectF headphone = renderer->headphone_bubble;
    return (bubble.Width > 20 && island_contains(x - bubble.X, y - bubble.Y, bubble.Width,
                                                 bubble.Height, bubble.Width / 2)) ||
           (headphone.Width > 20 &&
            island_contains(x - headphone.X, y - headphone.Y, headphone.Width, headphone.Height,
                            headphone.Width / 2));
}
