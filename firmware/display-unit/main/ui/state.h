#pragma once
/* Display unit data model. In the car these values will come from the CAN bus;
 * for now state_tick() simulates them (same model as the web prototype). */

#include <stdbool.h>
#include <stdint.h>

typedef enum { GEAR_R, GEAR_N, GEAR_D } gear_t;
typedef enum { TURN_OFF, TURN_LEFT, TURN_RIGHT, TURN_HAZARD } turn_t;
typedef enum { ALERT_NONE, ALERT_LOWBAT, ALERT_OVERHEAT, ALERT_CAN } alert_t;

typedef struct {
    /* inputs (CAN in the real car) */
    float pedal;          /* requested speed, km/h */
    gear_t gear;
    int level;            /* 1..3, chosen by the parents */
    float battery;        /* % */
    bool lights, remote, charging, can_lost;
    bool stop;            /* overheat alert: motors held */
    turn_t turn;          /* turn signals / hazard lights */
    bool fault;           /* generic error: lights the check-engine telltale */
    float temp_l, temp_r; /* motor temperature, C */

    /* settings (stored in NVS) */
    float limits[3];      /* km/h per level */
    int wheel_cm;
    int brightness;       /* % */
    int theme;            /* gauge theme, see theme_t in ui_priv.h */
    char pin[5];

    /* derived / simulated */
    float speed;          /* actual, km/h */
    bool at_limit;
    float odo, trip;      /* km */
    float outdoor;        /* outside temperature, C */
    bool outdoor_ok;      /* an outdoor sensor is connected */
    bool demo;
    float demo_t;
    bool low_shown, heat_shown;
} dash_state_t;

extern dash_state_t S;

void state_init(void);
void state_save_settings(void);
/* advance the simulation by dt seconds; returns an alert to raise (or ALERT_NONE) */
alert_t state_tick(float dt, bool running);
float state_effective_limit(void);
