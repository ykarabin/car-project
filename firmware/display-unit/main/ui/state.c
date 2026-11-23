#include "state.h"

#include <math.h>
#include <string.h>

#include "nvs.h"

dash_state_t S;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void state_init(void)
{
    memset(&S, 0, sizeof(S));
    S.pedal = 4;
    S.gear = GEAR_D;
    S.level = 2;
    S.battery = 78;
    S.lights = true;
    S.temp_l = 38;
    S.temp_r = 41;
    S.limits[0] = 3; S.limits[1] = 5; S.limits[2] = 12;
    S.wheel_cm = 18;
    S.brightness = 100;
    strcpy(S.pin, "1234");
    S.odo = 42;
    S.trip = 1.4;   /* demo value; the real one resets daily */
    S.outdoor = 12;   /* demo value until the sensor is wired */
    S.outdoor_ok = true;
    S.demo = true;

    nvs_handle_t h;
    if (nvs_open("dash", NVS_READONLY, &h) == ESP_OK) {
        uint32_t lim = 0;
        if (nvs_get_u32(h, "limits", &lim) == ESP_OK) {   /* three half-km/h steps packed in bytes */
            for (int i = 0; i < 3; i++) S.limits[i] = ((lim >> (8 * i)) & 0xFF) / 2.0f;
        }
        uint8_t v;
        if (nvs_get_u8(h, "wheel", &v) == ESP_OK) S.wheel_cm = v;
        if (nvs_get_u8(h, "bright", &v) == ESP_OK) S.brightness = v;
        if (nvs_get_u8(h, "theme", &v) == ESP_OK && v < 5) S.theme = v;   /* theme_t has five values */
        size_t n = sizeof(S.pin);
        nvs_get_str(h, "pin", S.pin, &n);
        nvs_close(h);
    }
}

void state_save_settings(void)
{
    nvs_handle_t h;
    if (nvs_open("dash", NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t lim = 0;
    for (int i = 0; i < 3; i++) lim |= ((uint32_t)lroundf(S.limits[i] * 2) & 0xFF) << (8 * i);
    nvs_set_u32(h, "limits", lim);
    nvs_set_u8(h, "wheel", (uint8_t)S.wheel_cm);
    nvs_set_u8(h, "bright", (uint8_t)S.brightness);
    nvs_set_u8(h, "theme", (uint8_t)S.theme);
    nvs_set_str(h, "pin", S.pin);
    nvs_commit(h);
    nvs_close(h);
}

float state_effective_limit(void)
{
    float lim = S.limits[S.level - 1];
    return S.gear == GEAR_R ? fminf(lim, 3) : lim;
}

alert_t state_tick(float dt, bool running)
{
    alert_t raise = ALERT_NONE;

    if (S.demo && running) {
        S.demo_t += dt;
        float p = 4.5f + 4 * sinf(S.demo_t * 0.45f) + 1.5f * sinf(S.demo_t * 1.3f);
        S.pedal = fmaxf(0, roundf(p * 2) / 2);
    }

    const float eff = state_effective_limit();
    const bool moving = S.gear != GEAR_N && !S.charging && !S.can_lost && !S.stop;
    const float target = moving ? fminf(S.pedal, eff) : 0;
    S.at_limit = moving && S.pedal > eff + 0.05f;
    S.speed += (target - S.speed) * fminf(1, dt * 2.5f);
    if (S.speed < 0.02f) S.speed = 0;

    if (running) {
        const float km = S.speed * dt / 3600;
        S.trip += km;
        S.odo += km;
        if (S.demo) S.battery = fmaxf(0, S.battery - S.speed * dt * 0.06f);
        const float heat = 30 + S.speed * 4.2f;
        S.temp_l += (heat - S.temp_l) * dt * 0.04f;
        S.temp_r += (heat + 3 - S.temp_r) * dt * 0.04f;
        if (S.charging) S.battery = fminf(100, S.battery + dt * 0.8f);
        if (S.battery < 10 && !S.low_shown && !S.charging) raise = ALERT_LOWBAT;
        else if (S.temp_r >= 70 && !S.heat_shown) raise = ALERT_OVERHEAT;
        if (S.temp_r < 55) S.heat_shown = false;
        if (S.battery > 15) S.low_shown = false;
    }
    S.battery = clampf(S.battery, 0, 100);
    return raise;
}
