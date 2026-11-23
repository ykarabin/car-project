/* Theme "Кластер": charge dial, speed dial with limit tick and character, range dial. */
#include <math.h>
#include <stdio.h>

#include "ui_priv.h"

static lv_obj_t *batt_arc, *batt_txt, *range_arc, *range_txt;
static lv_obj_t *speed_arc, *speed_txt;
static lv_obj_t *gear_lbl[3];

static lv_point_precise_t tick_pts[11][2];

static lv_obj_t *dial(lv_obj_t *p, int cx, int cy, int r)
{
    lv_obj_t *c = ui_box(p, cx - r, cy - r, 2 * r, 2 * r, -1, C_DIAL);
    lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    return c;
}

static void polar(lv_point_precise_t *pt, float deg, float r)
{
    const float a = deg * (float)M_PI / 180;
    pt->x = PX(400) + PS(r) * cosf(a);
    pt->y = PY(270) + PS(r) * sinf(a);
}

static lv_obj_t *line(lv_obj_t *p, lv_point_precise_t *pts, int width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

/* ---- static art baked into one bitmap ----
 * Anti-aliased big circles and wide arcs are the most expensive thing to draw in software
 * (~0.45 us/px on the S3 vs ~0.05 us/px for fills), and every value change on a gauge used to
 * redraw them. They never change, so they are rendered once at start-up into an RGB565
 * snapshot and only the coloured indicators are drawn live on top of it. */
#define BG_X 28
#define BG_Y 86
#define BG_W 968
#define BG_H 504
static lv_draw_buf_t *bg_snap;

static lv_obj_t *track(lv_obj_t *p, int cx, int cy, int r, int w)
{
    lv_obj_t *a = ui_arc(p, cx, cy, r, w, C_TRACK, true);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    return a;
}

static lv_obj_t *indicator(lv_obj_t *p, int cx, int cy, int r, int w, uint32_t color)
{
    lv_obj_t *a = ui_arc(p, cx, cy, r, w, color, true);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);   /* the track is in the bitmap */
    return a;
}

static void bake_background(lv_obj_t *layer)
{
    lv_obj_t *c = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 1024, 600);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);

    dial(c, 114, 300, 100);
    track(c, 114, 300, 74, 12);
    ui_text(c, &font_r20, C_MUTED, ALIGN_C, 114, 334, "заряд");
    dial(c, 686, 300, 100);
    track(c, 686, 300, 74, 12);
    ui_text(c, &font_r20, C_MUTED, ALIGN_C, 686, 334, "км запасу");
    /* speed dial last so its circle overlaps the side dials like the Porsche cluster; the side arcs stay
     * clear of it (centre distance 288 - arc outer radius 80 > 200), so no mask is needed */
    dial(c, 400, 270, 200);
    for (int i = 0; i <= 10; i++) {
        polar(&tick_pts[i][0], 150 + 24 * i, 180);
        polar(&tick_pts[i][1], 150 + 24 * i, 190);
        line(c, tick_pts[i], PS(4.6), C_LINE2);
    }
    track(c, 400, 270, 165, 20);
    ui_text(c, &font_r25, C_MUTED, ALIGN_C, 400, 366, "км/год");

    /* crop to the dials' bounding box (~0.95 MB instead of 1.2 MB for the full screen) */
    for (uint32_t i = 0; i < lv_obj_get_child_count(c); i++) {
        lv_obj_t *ch = lv_obj_get_child(c, i);
        lv_obj_set_pos(ch, lv_obj_get_style_x(ch, 0) - BG_X, lv_obj_get_style_y(ch, 0) - BG_Y);
    }
    lv_obj_set_size(c, BG_W, BG_H);
    lv_obj_update_layout(c);
    bg_snap = lv_snapshot_take(c, LV_COLOR_FORMAT_RGB565);
    lv_obj_delete(c);
    LV_ASSERT_MALLOC(bg_snap);

    lv_obj_t *img = lv_image_create(layer);
    lv_image_set_src(img, bg_snap);
    lv_obj_set_pos(img, BG_X, BG_Y);
}

void cluster_create(lv_obj_t *p)
{
    bake_background(p);

    batt_arc = indicator(p, 114, 300, 74, 12, C_GREEN);
    batt_txt = ui_text(p, &font_s50, C_TEXT, ALIGN_C, 114, 306, "78%");

    range_arc = indicator(p, 686, 300, 74, 12, C_SILVER);
    range_txt = ui_text(p, &font_s50, C_TEXT, ALIGN_C, 686, 306, "11,7");

    speed_arc = indicator(p, 400, 270, 165, 20, C_RED);

    gear_create(gear_lbl, p, 366, 34, 196);
    speed_txt = ui_text(p, &font_s212, C_TEXT, ALIGN_C, 400, 334, "0");
}

void cluster_update(const drive_ctx_t *c)
{
    char b[96];
    const bool lost = c->lost;

    ui_arc_set_pct(speed_arc, c->frac * 100);   /* the arc's end is the effective limit */
    ui_set_color_cache(speed_arc, S.gear == GEAR_R ? C_SILVER : C_RED, LV_PART_INDICATOR, false);
    snprintf(b, sizeof b, "%d", (int)lroundf(c->d_speed));
    ui_text_set(speed_txt, lost ? "—" : b);

    ui_arc_set_pct(batt_arc, lost ? 0 : c->d_batt);
    ui_set_color_cache(batt_arc, c->low ? C_AMBER : C_GREEN, LV_PART_INDICATOR, false);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(c->d_batt));
    ui_text_set(batt_txt, lost ? "—" : b);

    ui_arc_set_pct(range_arc, lost ? 0 : c->d_batt);
    ui_text_set(range_txt, lost ? "—" : c->range_txt);


    gear_set(gear_lbl, 366, 34, 196, &font_s28, &font_r25);
}
