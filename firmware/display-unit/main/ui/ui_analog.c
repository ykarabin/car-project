/* Theme "Аналог": a classic needle speedometer whose scale ends at the effective speed limit (0..3, 0..5, 0..8 or
 * 0..12 km/h, a tick every 0.5 km/h (1 above 8), a numeral every 1 (2 above 8)). The face (dial, bezel, ticks, numerals) is baked into one opaque
 * bitmap the first time the theme is shown and again whenever the scale changes (a level change, or the
 * reverse gear's 3 km/h cap: ~0.1 s, while the level card covers the screen); live: the needle (three thick
 * lines that taper), text blocks for charge and range and a gear lever. */
#include <math.h>
#include <stdio.h>

#include "ui_priv.h"

#define CX 400
#define CY 262
#define FACE_X PX(202)          /* the dial's bounding square, panel pixels */
#define FACE_Y PY(64)
#define FACE_S PS(396)
#define MAX_TICKS 17             /* 17 ticks cover 0..8 km/h in half steps and 0..12 km/h in whole steps */
#define LEVER_X 700             /* centre of the gear lever's slot, design coordinates */

static lv_obj_t *face_img, *needle[3], *hub;
#define BSEGS 10
static lv_obj_t *batt_txt, *speed_txt, *range_txt, *gear_lbl[3], *bseg[BSEGS];
static lv_draw_buf_t *face_snap;
static lv_point_precise_t needle_pts[3][2];
static float last_deg = -1000;
static int face_key = -1;     /* effective limit in half-km/h the baked face was made for */
static uint32_t last_ncol;

/* point at radius r (design units) and angle deg around the dial centre, in panel pixels */
static void pt(lv_point_precise_t *p, float r, float deg, int ox, int oy)
{
    const float a = deg * (float)M_PI / 180;
    p->x = PX(CX) - ox + PS(r) * cosf(a);
    p->y = PY(CY) - oy + PS(r) * sinf(a);
}

static lv_obj_t *thick_line(lv_obj_t *p, lv_point_precise_t *pts, int n, int width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(p);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

/* dial + bezel + ticks (minor every 0.5 km/h, major and a numeral every 1 km/h) over 0..max, drawn into an RGB565 snapshot (~490 KB) */
static void bake_face(float max, int key)
{
    static lv_point_precise_t tick_pts[MAX_TICKS][2];
    if (face_snap) {   /* free the old one first: two faces would not fit comfortably */
        lv_image_set_src(face_img, NULL);
        lv_draw_buf_destroy(face_snap);
        face_snap = NULL;
    }
    lv_obj_t *c = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_size(c, FACE_S, FACE_S);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_BG), 0);   /* the corners outside the circle */
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);

    ui_origin(FACE_X, FACE_Y);   /* children are written in design coordinates, relative to the container */
    lv_obj_t *dial = ui_box(c, CX - 198, CY - 198, 396, 396, -1, C_DIAL);
    lv_obj_set_style_border_color(dial, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(dial, 2, 0);
    lv_obj_t *ring = ui_box(c, CX - 186, CY - 186, 372, 372, -1, C_DIAL);
    lv_obj_set_style_border_color(ring, lv_color_hex(C_TRACK), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    const float u = max > 8 ? 1.0f : 0.5f;   /* km/h per minor tick: limits go up to 12 */
    const int n = (int)lroundf(max / u);
    for (int i = 0; i <= n && i < MAX_TICKS; i++) {
        const bool maj = i % 2 == 0;
        const float a = 150 + 240 * (i * u) / max;
        pt(&tick_pts[i][0], maj ? 158 : 166, a, FACE_X, FACE_Y);
        pt(&tick_pts[i][1], 178, a, FACE_X, FACE_Y);
        thick_line(c, tick_pts[i], 2, maj ? PS(4) : PS(2), maj ? C_SILVER : 0x5b6069);
        if (maj) {
            const float r = a * (float)M_PI / 180;
            char lbl[4];
            snprintf(lbl, sizeof lbl, "%d", (int)lroundf(i * u));
            ui_text(c, &font_s28, C_SILVER, ALIGN_C, CX + 138 * cosf(r), CY + 138 * sinf(r) + 8, lbl);
        }
    }
    ui_origin(0, 0);

    lv_obj_update_layout(c);
    face_snap = lv_snapshot_take(c, LV_COLOR_FORMAT_RGB565);
    lv_obj_delete(c);
    LV_ASSERT_MALLOC(face_snap);
    lv_image_set_src(face_img, face_snap);
    face_key = key;
}

void analog_create(lv_obj_t *p)
{
    face_img = lv_image_create(p);       /* gets its bitmap on first use */
    lv_obj_set_pos(face_img, FACE_X, FACE_Y);

    needle[0] = thick_line(p, needle_pts[0], 2, PS(16), C_RED);   /* hub end, thick */
    needle[1] = thick_line(p, needle_pts[1], 2, PS(9), C_RED);
    needle[2] = thick_line(p, needle_pts[2], 2, PS(4), C_RED);    /* tip */
    hub = ui_box(p, CX - 15, CY - 15, 30, 30, -1, C_BG);
    lv_obj_set_style_border_width(hub, 4, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(C_RED), 0);

    speed_txt = ui_text(p, &font_s138, C_TEXT, ALIGN_C, 400, 382, "0");
    ui_text(p, &font_r22, C_MUTED, ALIGN_C, 400, 406, "км/год");

    /* left column: charge and range as two equal text blocks */
    batt_txt = ui_text(p, &font_s70, C_TEXT, ALIGN_C, 100, 204, "78%");
    ui_text(p, &font_r22, C_MUTED, ALIGN_C, 100, 232, "заряд");
    for (int i = 0; i < BSEGS; i++) bseg[i] = ui_box(p, 17 + 17 * i, 246, 14, 20, 4, C_TRACK);   /* the visual charge scale */
    range_txt = ui_text(p, &font_s70, C_TEXT, ALIGN_C, 100, 336, "11,7");
    ui_text(p, &font_r22, C_MUTED, ALIGN_C, 100, 366, "км запасу");

    /* gear lever on the right, mirroring the left column */
    lv_obj_t *slot = ui_box(p, 672, 164, 56, 196, 28, C_DIAL);
    lv_obj_set_style_border_color(slot, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(slot, 2, 0);
    gear_create_v(gear_lbl, p, LEVER_X, 210, 66);
}

static void set_needle(float deg)
{
    /* distance along the needle (design units) of each segment's two ends: tail..hub..tip, tapering */
    static const float s[3][2] = {{-26, 36}, {36, 78}, {78, 118}};
    for (int i = 0; i < 3; i++) {
        static const int width[3] = {PS(16), PS(9), PS(4)};
        pt(&needle_pts[i][0], s[i][0] < 0 ? -s[i][0] : s[i][0], s[i][0] < 0 ? deg + 180 : deg, 0, 0);
        pt(&needle_pts[i][1], s[i][1], deg, 0, 0);
        ui_line_set(needle[i], needle_pts[i], 2, width[i]);
    }
}

void analog_update(const drive_ctx_t *c)
{
    const int key = (int)lroundf(c->eff * 2);
    if (key != face_key) bake_face(c->eff, key);

    char b[24];
    const bool lost = c->lost;
    const float deg = 150 + 240 * c->frac;
    const uint32_t ncol = S.gear == GEAR_R ? C_SILVER : C_RED;

    if (fabsf(deg - last_deg) > 0.3f) {
        last_deg = deg;
        set_needle(deg);
    }
    if (ncol != last_ncol) {
        last_ncol = ncol;
        for (int i = 0; i < 3; i++) lv_obj_set_style_line_color(needle[i], lv_color_hex(ncol), 0);
        lv_obj_set_style_border_color(hub, lv_color_hex(ncol), 0);
    }

    gear_set_v(gear_lbl, LEVER_X, 210, 66, &font_s50, &font_s40);
    snprintf(b, sizeof b, "%d", (int)lroundf(c->d_speed));
    ui_text_set(speed_txt, lost ? "—" : b);

    ui_set_color_cache(batt_txt, c->low ? C_AMBER : C_TEXT, 0, true);
    for (int i = 0; i < BSEGS; i++) ui_set_bg(bseg[i], !lost && c->d_batt >= i * 10 + 5 ? (c->low ? C_AMBER : C_GREEN) : C_TRACK);
    snprintf(b, sizeof b, "%d%%", (int)lroundf(c->d_batt));
    ui_text_set(batt_txt, lost ? "—" : b);
    ui_text_set(range_txt, lost ? "—" : c->range_txt);
}
