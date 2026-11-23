#ifndef VEHICLE_BOARD_CONTROLS_H
#define VEHICLE_BOARD_CONTROLS_H

#include <stdint.h>

typedef enum { BOARD_BUTTON_NONE, BOARD_BUTTON_SHORT, BOARD_BUTTON_LONG } board_button_event_t;

typedef enum { BOARD_CAL_OFF, BOARD_CAL_CENTER, BOARD_CAL_LEFT, BOARD_CAL_RIGHT } board_cal_stage_t;

typedef enum { BOARD_NOTICE_NONE, BOARD_NOTICE_ERROR, BOARD_NOTICE_SAVED } board_notice_t;

/* Nonblocking button/LED handling in the control task.
 * Short press fires on release; a button held at boot must first be released. */
void board_controls_init(uint32_t now_ms);
board_button_event_t board_controls_poll(uint32_t now_ms);
void board_controls_set_stage(board_cal_stage_t stage, uint32_t now_ms);
void board_controls_signal(board_notice_t notice, uint32_t now_ms);

#endif
