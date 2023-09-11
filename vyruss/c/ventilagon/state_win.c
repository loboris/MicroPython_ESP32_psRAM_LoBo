#include "ventilagon.h"

const long win_step_delay = 25;
int64_t win_last_step = 0;
int64_t win_started;
const long win_delay_1 = 45000 - 7000; // 45 seconds
const long win_delay_2 = 45000; // un poquin mas para que se vaya todo

void win_setup() {
  display_calibrate(true);
  board_reset();
  ledbar_set_win_state();
  int64_t now = esp_timer_get_time();
  win_started = now / 1000;
}

void win_loop() {
  int64_t now = esp_timer_get_time();
  int64_t now_ms = now / 1000;
  if ((now_ms - win_last_step) > win_step_delay) {
    win_last_step = now_ms;
    if ((now_ms - win_started) > win_delay_1) {
      board_step_back();
    } else {
      board_win_step_back();
      // DEBUG
      // board_step();
    }
    display_adjust_drift();
  }

  if ((now_ms - win_started) > win_delay_2) {
    change_state(&gameover_state);
  }
  now = esp_timer_get_time();
  display_tick(now);
}

State win_state = {"FOR THE WIN!", win_setup, win_loop};
