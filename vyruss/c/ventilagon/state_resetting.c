#include "ventilagon.h"

const unsigned long reset_step_delay = (10L * 100 * 1000) / NUM_ROWS;
uint64_t last_step;
int counter;

void resetting_setup() {
  last_step = esp_timer_get_time();
  counter = 0;
  audio_reset();
  serial_send("arduino reset");
  serial_send("arduino stop");
}

void resetting_loop() {
  int64_t now = esp_timer_get_time();
  display_tick(now);
  
  // tirar las lineas para afuera durante medio segundo
  if ((now - last_step) > reset_step_delay) {
    board_step_back();
    last_step = now;
    counter++;
  }

  // despues de medio segundo, arrancar el juego
  if (counter > NUM_ROWS) {
    change_state(&play_state);
  }
}

State resetting_state = { "RESETTING", resetting_setup, resetting_loop };

