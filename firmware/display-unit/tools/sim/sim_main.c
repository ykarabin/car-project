/* Host screenshot simulator: runs the firmware UI code against LVGL on the Mac,
 * renders into a 1024x600 RGB565 buffer and writes PPM screenshots of each scene.
 * Time and touch are simulated, so the run is deterministic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "ui_priv.h"

#define W 1024
#define H 600

int64_t sim_us;
static uint16_t fb[W * H];
static int tx, ty;
static bool tdown;
static const char *outdir = ".";

static uint32_t tick_cb(void) { return (uint32_t)(sim_us / 1000); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * W + a->x1], px + (y - a->y1) * w * 2, w * 2);
    lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *i, lv_indev_data_t *data)
{
    (void)i;
    data->point.x = tx;
    data->point.y = ty;
    data->state = tdown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run_ms(int ms)
{
    for (int t = 0; t < ms; t += 5) {
        sim_us += 5000;
        lv_timer_handler();
    }
}

static void snap(const char *name)
{
    lv_refr_now(NULL);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        const uint16_t c = fb[i];
        const uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("snap %s\n", name);
}

static void tap(int x, int y)
{
    tx = x; ty = y; tdown = true;
    run_ms(80);
    tdown = false;
    run_ms(150);
}

static void drag(int x0, int y0, int x1, int y1, int ms)
{
    tx = x0; ty = y0; tdown = true;
    run_ms(30);
    for (int t = 0; t <= ms; t += 10) {
        tx = x0 + (x1 - x0) * t / ms;
        ty = y0 + (y1 - y0) * t / ms;
        run_ms(10);
    }
    tdown = false;
    run_ms(150);
}

static void key(char k)
{
    static const char K[] = "123456789\0000<";
    const int x0 = 1024 - 50 - (3 * 135 + 2 * 15), y0 = (600 - (4 * 110 + 3 * 15)) / 2;
    for (int i = 0; i < 12; i++)
        if (K[i] == k) tap(x0 + (i % 3) * 150 + 67, y0 + (i / 3) * 125 + 55);
}

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    static uint8_t buf[W * H * 2] __attribute__((aligned(64)));
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, read_cb);

    ui_init(d);
    run_ms(350); snap("00_crest_fading_in"); run_ms(430); snap("00b_pre_sweep"); run_ms(470); snap("01_splash_crest"); run_ms(250); snap("01b_splash_sweep_late");
    run_ms(300);
    run_ms(500);  snap("02b_splash_car_emerging");
    run_ms(400);  snap("02c_splash_car_almost");
    run_ms(770);  snap("03_splash_end");
    run_ms(2500); snap("04_drive_demo");

    ui_scene(2);  run_ms(2500); snap("05_at_limit");
    ui_scene(3);  run_ms(2500); snap("06_reverse");
    S.level = 3; run_ms(400); snap("06b_reverse_level3_cheetah");
    S.level = 2; S.gear = GEAR_D; run_ms(400); snap("06c_forward_again");
    ui_scene(4);  run_ms(1500); snap("07_remote");
    ui_scene(5);  run_ms(450);  snap("08_level_change");
    run_ms(900);  snap("08b_level_card_settled");
    run_ms(1700); snap("08c_level_card_closed");
    ui_scene(6);  run_ms(400);  snap("09_low_battery");
    ui_scene(7);  run_ms(400);  snap("10_overheat");
    ui_scene(8);  run_ms(400);  snap("11_no_can");
    tap(512, 438 + 32);         snap("11b_no_can_dismissed");
    ui_scene(9);  run_ms(800);  snap("12_car");
    ui_scene(10); run_ms(800);  snap("13_charging");

    ui_scene(1);  run_ms(1500);
    drag(850, 300, 250, 320, 200); run_ms(300); snap("14_swipe_to_car");
    drag(250, 300, 850, 320, 200); run_ms(300);

    tx = 80; ty = 50; tdown = true; run_ms(800); snap("15_hold_progress");
    run_ms(900); tdown = false; run_ms(200); snap("16_pin");
    key('1'); key('1'); key('1'); key('1'); run_ms(100); snap("17_pin_wrong");
    run_ms(600);
    key('1'); key('2'); key('3'); key('4'); run_ms(300); snap("18_settings");
    tap(760, 52); run_ms(300); snap("19_demo_scenes");
    tap(35 + 60, 140 + 42 + 100); run_ms(2500); snap("20_scene_from_panel");

    /* always-visible DEMO button: from the drive page and in the middle of the splash */
    tap(58, 566); run_ms(300); snap("21_dev_button_panel");
    tap(1024 - 35 - 75, 52); run_ms(300); snap("22_dev_panel_back");
    ui_scene(0); run_ms(1000);
    tap(58, 566); run_ms(300); snap("23_dev_button_during_splash");

    /* ---- gauge themes: normal, at limit, reverse, low battery */
    static const char *tn[] = {"cluster", "sport", "analog", "kids", "auto"};
    for (int t = 0; t < THEME_COUNT; t++) {
        char n[64];
        ui_set_theme(t, false);
        ui_scene(1); S.battery = 78; S.low_shown = false; run_ms(2300);
        snprintf(n, sizeof n, "40_%s_drive", tn[t]); snap(n);
        ui_scene(2); run_ms(2800);
        snprintf(n, sizeof n, "41_%s_limit", tn[t]); snap(n);
        ui_scene(3); run_ms(2500);
        snprintf(n, sizeof n, "42_%s_reverse", tn[t]); snap(n);
        ui_scene(1); S.low_shown = true; S.battery = 8; run_ms(1500);
        snprintf(n, sizeof n, "43_%s_low", tn[t]); snap(n);
        /* every telltale lit and the hazards on, captured while the arrows are in their "on" half */
        ui_scene(1); S.battery = 78; S.low_shown = false; S.turn = TURN_HAZARD; S.fault = true; S.remote = true; S.temp_r = 60;
        run_ms(600);
        while ((lv_tick_get() / 350) % 2 != 0) run_ms(30);
        run_ms(40);
        snprintf(n, sizeof n, "44_%s_chrome", tn[t]); snap(n);
        S.turn = TURN_OFF; S.fault = false; S.remote = false; S.temp_r = 41;
    }
    /* a 12 km/h limit: two-digit speeds in every theme */
    S.limits[2] = 12;
    for (int t = 0; t < THEME_COUNT; t++) {
        char n[64];
        ui_set_theme(t, false);
        ui_scene(1); S.level = 3; S.pedal = 12; S.battery = 78; S.low_shown = false; run_ms(3500);
        snprintf(n, sizeof n, "60_%s_12kmh", tn[t]); snap(n);
    }
    S.limits[2] = 8; S.level = 2;
    /* theme picker and a swipe through the themes */
    ui_set_theme(THEME_CLUSTER, false);
    ui_scene(1); run_ms(1500);
    drag(500, 450, 500, 150, 200); run_ms(900); snap("50_swipe_up_sport");
    drag(500, 450, 500, 150, 200); run_ms(900); snap("51_swipe_up_analog");
    drag(500, 150, 500, 450, 200); run_ms(900); snap("52_swipe_down_sport");
    ui_scene(1);
    ui_set_theme(THEME_CLUSTER, false);
    N.modal = MODAL_NONE;
    tx = 80; ty = 50; tdown = true; run_ms(1700); tdown = false; run_ms(300);
    key('1'); key('2'); key('3'); key('4'); run_ms(300);
    tap(1024 - 35 - 150 - 2 * (14 + 130) + 65, 52); run_ms(400); snap("53_theme_picker");
    tap(35 + 2 * (226 + 16) + 100, 300); run_ms(400); snap("54_theme_picker_analog_selected");
    ui_set_theme(THEME_CLUSTER, false);
    return 0;
}
