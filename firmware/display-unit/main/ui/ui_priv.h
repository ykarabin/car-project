#pragma once
/* Internal helpers shared by the display unit screens.
 * All coordinates in the screen builders are the 800x480 design coordinates;
 * PX()/PY() map them onto the 1024x600 panel (x1.25, centred horizontally). */

#include "assets.h"
#include "lvgl.h"
#include "state.h"

#define PX(x) ((int32_t)(12 + (x) * 5 / 4))
#define PY(y) ((int32_t)((y) * 5 / 4))
#define PS(v) ((int32_t)((v) * 5 / 4))

/* palette (see design spec) */
#define C_BG     0x0a0a0c   /* #0b0c0e quantises green-ish in RGB565; this stays neutral */
#define C_DIAL   0x14161a
#define C_TRACK  0x1f2228
#define C_LINE   0x2a2d34
#define C_LINE2  0x3a3d45
#define C_TEXT   0xf2f2f2
#define C_MUTED  0x8a8f98
#define C_DIM    0x6b7079
#define C_RED    0xe5322d
#define C_GREEN  0x3fb950
#define C_AMBER  0xd29922
#define C_BLUE   0x58a6ff
#define C_BEAM   0xfff3c4
#define C_SILVER 0xc9ccd3

typedef enum { ALIGN_L, ALIGN_C, ALIGN_R } halign_t;

void ui_origin(int32_t ox, int32_t oy);   /* panel-pixel origin of the parent being filled (0,0 = screen) */
lv_obj_t *ui_layer(lv_obj_t *parent, bool opaque);
lv_obj_t *ui_box(lv_obj_t *parent, int x, int y, int w, int h, int radius, uint32_t color);
/* label anchored like an SVG <text>: x is the anchor, y the baseline (design coordinates) */
lv_obj_t *ui_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, halign_t align, int x, int y, const char *txt);
void ui_text_set(lv_obj_t *label, const char *txt);   /* only redraws when the text changes */
void ui_text_move(lv_obj_t *label, halign_t align, int x, int y);
lv_obj_t *ui_arc(lv_obj_t *parent, int cx, int cy, int r, int width, uint32_t color, bool rounded);
void ui_arc_set_pct(lv_obj_t *arc, float pct);
lv_obj_t *ui_button(lv_obj_t *parent, const lv_font_t *font, const char *txt, lv_event_cb_t cb, void *user);
void ui_set_color_cache(lv_obj_t *obj, uint32_t color, lv_style_selector_t sel, bool text);
void ui_pulse(lv_obj_t *obj, bool on, lv_style_selector_t part);
void ui_line_set(lv_obj_t *line, lv_point_precise_t *pts, int n, int width);   /* tight object around absolute points */
void ui_set_bg(lv_obj_t *obj, uint32_t color);           /* only touches the style when the colour changes */
/* 's' small with alpha (settings rows), 't' tiny top-bar sprite, 'c' level-card sprite and 'k' Kids sprite (all opaque, on their background colour) */
const lv_image_dsc_t *ui_char_img(int level, char size);
const lv_image_dsc_t *ui_char_flipped(int level, bool big);   /* mirrored 't' ('k' when big) built in RAM on demand */
void ui_char_flip_release(bool big);                          /* free it when reverse gear ends */
void fmt_num(char *buf, size_t n, float v, int decimals);  /* decimal comma, drops ",0" when decimals<0 */

/* ---- gauge themes: the same data, four looks (see the design prototype) ---- */
typedef enum { THEME_CLUSTER, THEME_SPORT, THEME_ANALOG, THEME_KIDS, THEME_AUTO, THEME_COUNT } theme_t;
extern const char *const THEME_NAMES[THEME_COUNT];

/* everything a theme needs for one frame, computed once in ui_theme.c */
typedef struct {
    bool lost, low, sleepy;
    float d_speed, d_batt;   /* values to show */
    float eff;               /* effective speed limit, km/h */
    float speed;             /* real speed, km/h */
    float frac;              /* shown speed as a share of the scale; the scale ends at the effective limit */
    char range_txt[12];      /* "11,7" */
} drive_ctx_t;

void drive_ctx_build(drive_ctx_t *c);
void drive_update(void);              /* build the context and redraw the active theme + chrome */
void chrome_create(lv_obj_t *parent);          /* clock-side status: lights lamp, link warning, parent banner */
void chrome_update(void);                      /* tiny, so it also runs while the level card hides the gauges */
void gear_create(lv_obj_t *lbl[3], lv_obj_t *parent, int x0, int dx, int y);
void gear_set(lv_obj_t *lbl[3], int x0, int dx, int y, const lv_font_t *on, const lv_font_t *off);
/* the same, stacked vertically (Kids): letters at x, baselines y0 + dy * i */
void gear_create_v(lv_obj_t *lbl[3], lv_obj_t *parent, int x, int y0, int dy);
void gear_set_v(lv_obj_t *lbl[3], int x, int y0, int dy, const lv_font_t *on, const lv_font_t *off);

void cluster_create(lv_obj_t *parent);  void cluster_update(const drive_ctx_t *c);
void sport_create(lv_obj_t *parent);    void sport_update(const drive_ctx_t *c);
void analog_create(lv_obj_t *parent);   void analog_update(const drive_ctx_t *c);
void kids_create(lv_obj_t *parent);     void kids_update(const drive_ctx_t *c);
void auto_create(lv_obj_t *parent);     void auto_update(const drive_ctx_t *c);

/* the animated character shared by the themes (hop, shake and sweat, breathe, sleep, mirrored in reverse) */
typedef struct char_ctl char_ctl_t;
char_ctl_t *char_create(lv_obj_t *parent, int x, int y, bool big);   /* x,y: design coordinates of the sprite's top-left */
void char_set_pos(char_ctl_t *c, int x, int y);
void char_update(char_ctl_t *c, const drive_ctx_t *ctx);

/* screens */
void car_create(lv_obj_t *parent);
void car_update(void);
void charge_create(lv_obj_t *parent);
void charge_update(void);
void splash_create(lv_obj_t *parent);
void splash_play(void);
void splash_stop(void);
void overlays_create(lv_obj_t *parent);
void level_show(int level);
void alert_show(alert_t a);
void toast(const char *txt);
void pin_create(lv_obj_t *parent);
void pin_open(void);
void settings_create(lv_obj_t *parent);
void settings_open(void);
void settings_tick(void);
void themes_create(lv_obj_t *parent);
void themes_open(void);
void demo_create(lv_obj_t *parent);
void demo_open(void);   /* remembers where it was opened from */

/* navigation (ui.c) */
typedef enum { SCR_SPLASH, SCR_MAIN } scr_t;
typedef enum { PAGE_DRIVE, PAGE_CAR } page_t;
typedef enum { MODAL_NONE, MODAL_PIN, MODAL_SETTINGS, MODAL_THEMES, MODAL_DEMO } modal_t;

typedef struct {
    scr_t screen;
    page_t page;
    modal_t modal;
    alert_t alert;
    lv_obj_t *l_splash, *l_theme[THEME_COUNT], *l_chrome, *l_car, *l_charge, *l_level, *l_alert, *l_pin, *l_settings, *l_themes, *l_demo;
    lv_obj_t *hold, *dim, *dev_btn;
} ui_nav_t;

extern ui_nav_t N;

void ui_apply_layers(void);
void ui_go_main(void);
void ui_restart_splash(void);
void ui_raise(alert_t a);
void ui_set_level(int level);
void ui_set_theme(int theme, bool save);   /* wraps around; save = remember the choice */
void ui_set_brightness(int pct);
void ui_scene(int idx);
extern const char *const UI_SCENES[];
extern const int UI_SCENE_COUNT;
