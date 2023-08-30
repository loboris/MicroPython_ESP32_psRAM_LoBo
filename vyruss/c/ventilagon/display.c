#include "ventilagon.h"

unsigned int last_column_drawn;
int drift_pos;
int drift_speed;
bool calibrating;

int nave_pos = 360;
//int nave_calibrate = -478; // ventilador velocidad media
int nave_calibrate = -250; // ventilador velocidad maxima
int half_ship_width = 50;

volatile int64_t last_turn = 0;
volatile int64_t last_turn_duration = 10L;

void handle_interrupt() {
  int64_t this_turn = esp_timer_get_time();
  int64_t this_turn_duration = this_turn - last_turn;
  //if (this_turn_duration < (last_turn_duration >> 2)) {
  //  return;
  //}
  last_turn_duration = this_turn_duration;
  last_turn = this_turn;
}

void display_init(){
   last_column_drawn = -1;
   drift_pos = 0;
   drift_speed = 0;
   calibrating = false;
}

void display_reset() {
  drift_pos = 0;
  drift_speed = 0;
}

void display_adjust_drift() {
  static int n = 0;
  n = (n+1) & 0xF;
  if (n == 0) {
    drift_speed = level_new_drift(drift_speed);
  }
}

bool display_ship_on(int current_pos) {
  if (calibrating) {
    return board_colision(current_pos, ROW_SHIP);
  }

  // NO HAY QUE ARREGLAR NADA ACA

  if (abs(nave_pos - current_pos) < (half_ship_width)) {
    return true;
  }
  if (abs( ((nave_pos + SUBDEGREES / 2) & SUBDEGREES_MASK) -
           ((current_pos + SUBDEGREES / 2) & SUBDEGREES_MASK))
      < (half_ship_width)) {
    return true;
  }
  return false;
}

void display_tick(int64_t now) {
  // esto no hace falta calcularlo tan seguido. Una vez por vuelta deberia alcanzar
  drift_pos = (drift_pos + drift_speed) & SUBDEGREES_MASK;

  int64_t drift = drift_pos * last_turn_duration / SUBDEGREES;
  unsigned int current_pos = ((drift + now - last_turn) * SUBDEGREES / last_turn_duration) & SUBDEGREES_MASK;
  unsigned int current_column = ((drift + now - last_turn) * NUM_COLUMNS / last_turn_duration) % NUM_COLUMNS;

  if (display_ship_on(current_pos)) {
    ship_prender();
  } else {
    ship_apagar();
  }

  if (current_column != last_column_drawn) {
    board_draw_column(current_column);
  }
}

void display_calibrate(bool cal) {
  calibrating = cal;
}

void display_dump_debug() {
  debug("VELOCIDAD:");
  debugln(last_turn_duration);
}
