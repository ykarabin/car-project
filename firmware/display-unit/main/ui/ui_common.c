#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_priv.h"

/* Origin of the parent being filled, in panel pixels: helpers that take design coordinates subtract it,
 * so children of a card can be written in the same design coordinates as everything else. */
static int32_t g_ox, g_oy;
void ui_origin(int32_t ox, int32_t oy)
{
    g_ox = ox;
    g_oy = oy;
}

lv_obj_t *ui_layer(lv_obj_t *parent, bool opaque)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (opaque) {
        lv_obj_set_style_bg_color(o, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, PX(x) - g_ox, PY(y) - g_oy);
    lv_obj_set_size(o, PS(w), PS(h));
    lv_obj_set_style_radius(o, radius >= 0 ? PS(radius) : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void ui_text_move(lv_obj_t *l, halign_t align, int x, int y)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    const int32_t top = PY(y) - (f->line_height - f->base_line);
    lv_obj_set_pos(l, PX(x) - g_ox, top - g_oy);
    /* anchor via translate so the label box stays tight (small redraw areas) */
    lv_obj_set_style_translate_x(l, align == ALIGN_C ? LV_PCT(-50) : align == ALIGN_R ? LV_PCT(-100) : 0, 0);
}

lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, halign_t align, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    ui_text_move(l, align, x, y);
    return l;
}

void ui_text_set(lv_obj_t *l, const char *txt)
{
    if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

void ui_set_color_cache(lv_obj_t *obj, uint32_t color, lv_style_selector_t sel, bool text)
{
    const lv_color_t c = lv_color_hex(color);
    if (text) {
        if (!lv_color_eq(lv_obj_get_style_text_color(obj, sel), c)) lv_obj_set_style_text_color(obj, c, sel);
    } else if (lv_obj_check_type(obj, &lv_arc_class)) {
        if (!lv_color_eq(lv_obj_get_style_arc_color(obj, sel), c)) lv_obj_set_style_arc_color(obj, c, sel);
    } else {
        if (!lv_color_eq(lv_obj_get_style_border_color(obj, sel), c)) lv_obj_set_style_border_color(obj, c, sel);
    }
}

lv_obj_t *ui_arc(lv_obj_t *parent, int cx, int cy, int r, int width, uint32_t color, bool rounded)
{
    lv_obj_t *a = lv_arc_create(parent);
    const int32_t size = 2 * PS(r) + PS(width);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, PX(cx) - size / 2, PY(cy) - size / 2);
    lv_arc_set_bg_angles(a, 150, 30);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, PS(width), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, PS(width), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_INDICATOR);
    return a;
}

void ui_arc_set_pct(lv_obj_t *a, float pct)
{
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    const int32_t v = (int32_t)lroundf(pct * 10);
    if (lv_arc_get_value(a) != v) lv_arc_set_value(a, v);
    /* a zero-length rounded indicator would still draw a dot */
    const lv_opa_t opa = pct < 0.6f ? LV_OPA_TRANSP : LV_OPA_COVER;
    if (!lv_anim_get(a, NULL) && lv_obj_get_style_arc_opa(a, LV_PART_INDICATOR) != opa)
        lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
}

/* An lv_line object spans from its own position to the farthest point, so a line far from its parent's
 * origin is a huge object and every change repaints that whole area (a needle at the far side of the
 * screen was ~200 kpx per line per frame). Park the object on the points' bounding box instead.
 * pts are absolute panel coordinates on entry and become relative to the object. */
void ui_line_set(lv_obj_t *l, lv_point_precise_t *pts, int n, int width)
{
    float minx = pts[0].x, miny = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < minx) minx = pts[i].x;
        if (pts[i].y < miny) miny = pts[i].y;
    }
    const int pad = width / 2 + 2;
    const int ox = (int)floorf(minx) - pad, oy = (int)floorf(miny) - pad;
    for (int i = 0; i < n; i++) {
        pts[i].x -= ox;
        pts[i].y -= oy;
    }
    lv_obj_set_pos(l, ox, oy);
    lv_line_set_points(l, pts, n);
}

void ui_set_bg(lv_obj_t *obj, uint32_t color)
{
    const lv_color_t c = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), c)) lv_obj_set_style_bg_color(obj, c, 0);
}

static void btn_style(lv_obj_t *b)
{
    lv_obj_set_style_bg_color(b, lv_color_hex(C_DIAL), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_TRACK), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, PS(14), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
}

lv_obj_t *ui_button(lv_obj_t *parent, const lv_font_t *font, const char *txt, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    btn_style(b);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static void pulse_arc_cb(void *obj, int32_t v) { lv_obj_set_style_arc_opa(obj, (lv_opa_t)v, LV_PART_INDICATOR); }
static void pulse_border_cb(void *obj, int32_t v) { lv_obj_set_style_border_opa(obj, (lv_opa_t)v, 0); }
static void pulse_line_cb(void *obj, int32_t v) { lv_obj_set_style_line_opa(obj, (lv_opa_t)v, 0); }

void ui_pulse(lv_obj_t *obj, bool on, lv_style_selector_t part)
{
    lv_anim_exec_xcb_t cb = lv_obj_check_type(obj, &lv_line_class) ? pulse_line_cb
                            : part == LV_PART_INDICATOR                ? pulse_arc_cb
                                                                       : pulse_border_cb;
    const bool running = lv_anim_get(obj, cb) != NULL;
    if (on == running) return;
    if (!on) {
        lv_anim_delete(obj, cb);
        cb(obj, LV_OPA_COVER);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    /* a 2 Hz blink instead of a smooth fade: every step repaints the whole arc/ring,
     * which costs ~100 ms on the S3, so only two repaints per second */
    /* a polyline overlaps itself at the joints, so it blinks fully on/off instead */
    lv_anim_set_values(&a, LV_OPA_COVER, cb == pulse_line_cb ? LV_OPA_TRANSP : 110);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_reverse_duration(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_step);
    lv_anim_start(&a);
}

const lv_image_dsc_t *ui_char_img(int level, char size)
{
    static const lv_image_dsc_t *const s[] = {&img_turtle_s, &img_hare_s, &img_cheetah_s};   /* settings rows (alpha) */
    static const lv_image_dsc_t *const t[] = {&img_turtle_t, &img_hare_t, &img_cheetah_t};   /* top bar, opaque */
    static const lv_image_dsc_t *const c[] = {&img_turtle_lc, &img_hare_lc, &img_cheetah_lc};
    static const lv_image_dsc_t *const k[] = {&img_turtle_lk, &img_hare_lk, &img_cheetah_lk};
    const int i = (level < 1 ? 1 : level > 3 ? 3 : level) - 1;
    return size == 'c' ? c[i] : size == 'k' ? k[i] : size == 't' ? t[i] : s[i];
}

/* Mirrored copy of a sprite, built in RAM when reverse gear needs it and freed when it ends.
 * LVGL 9.4 has no cheap image flip (a transform redraws every pixel through the slow path), and a stored
 * mirrored copy would cost 35 KB (small) or 129 KB (Kids) per character for a rare state.
 * RGB565A8 layout: the RGB565 plane (w*h*2 bytes) followed by the A8 plane (w*h bytes); RGB565 has no
 * alpha plane. Two descriptors alternate, so LVGL always sees a new source pointer when the content
 * changes (a level change while reversing). */
typedef struct {
    lv_image_dsc_t dsc[2];
    uint8_t *buf;
    int level, idx;
} flip_t;
static flip_t flip_small, flip_big;

static const lv_image_dsc_t *flip_get(flip_t *f, const lv_image_dsc_t *src, int level)
{
    const uint32_t w = src->header.w, h = src->header.h;
    const bool alpha = src->header.cf == LV_COLOR_FORMAT_RGB565A8;
    if (f->buf && f->level == level) return &f->dsc[f->idx];
    if (!f->buf) {
        f->buf = lv_malloc(w * h * (alpha ? 3 : 2));
        if (!f->buf) return src;   /* out of memory: show the unmirrored sprite */
    }
    const uint16_t *sc = (const uint16_t *)src->data;
    uint16_t *dc = (uint16_t *)f->buf;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) dc[y * w + x] = sc[y * w + (w - 1 - x)];
    if (alpha) {
        const uint8_t *sa = src->data + w * h * 2;
        uint8_t *da = f->buf + w * h * 2;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) da[y * w + x] = sa[y * w + (w - 1 - x)];
    }
    f->idx ^= 1;
    f->dsc[f->idx] = *src;
    f->dsc[f->idx].data = f->buf;
    f->level = level;
    return &f->dsc[f->idx];
}

const lv_image_dsc_t *ui_char_flipped(int level, bool big)
{
    return big ? flip_get(&flip_big, ui_char_img(level, 'k'), level) : flip_get(&flip_small, ui_char_img(level, 't'), level);
}

void ui_char_flip_release(bool big)
{
    flip_t *f = big ? &flip_big : &flip_small;
    if (!f->buf) return;
    lv_free(f->buf);
    f->buf = NULL;
    f->level = 0;
}

void fmt_num(char *buf, size_t n, float v, int decimals)
{
    if (decimals < 0) decimals = fabsf(v - roundf(v)) > 0.01f ? 1 : 0;
    snprintf(buf, n, "%.*f", decimals, v);
    char *dot = strchr(buf, '.');
    if (dot) *dot = ',';
}
