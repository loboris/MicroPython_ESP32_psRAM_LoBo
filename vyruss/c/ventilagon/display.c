#include "ventilagon.h"

unsigned int last_column_drawn;
int drift_pos;
int drift_speed;
bool calibrating;

int nave_pos = 360;
//int nave_calibrate = -478; // ventilador velocidad media
int nave_calibrate = -250; // ventilador velocidad maxima
int half_ship_width = 50;

// defined by Ventilastastion
extern volatile int64_t last_turn;
extern volatile int64_t last_turn_duration;

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

  bool need_update = false;
  int64_t drift = 0; //drift_pos * last_turn_duration / SUBDEGREES;
  unsigned int current_pos = ((drift + now - last_turn) * SUBDEGREES / last_turn_duration) & SUBDEGREES_MASK;
  unsigned int current_column = ((drift + now - last_turn) * NUM_COLUMNS / last_turn_duration) % NUM_COLUMNS;

  if (current_column != last_column_drawn) {
    need_update = true;
    last_column_drawn = current_column;
  }

  // FIXME: mostrar nave
  // y mostrar nave triangular
  if (display_ship_on(current_pos)) {
    //need_update = true;
  }

  if (need_update || true) {

    int opposed_column = (current_column + NUM_COLUMNS/2) % NUM_COLUMNS;
    for (int j=0; j<54; j++) {
      extra_buf[j] = 0x010000ff;
    }
    board_draw_column(current_column, extra_buf + 54-NUM_ROWS);
    for(int n=0; n<54; n++) {
	pixels0[n] = extra_buf[53-n];
    }

    for (int j=0; j<54; j++) {
      extra_buf[j] = 0x000100ff;
    }
    board_draw_column(current_column, extra_buf + 54-NUM_ROWS);
    for(int n=0; n<54; n++) {
	pixels1[n] = extra_buf[n];
    }

    //FIXME: draw_ship
    //ship_draw();
    spi_write_HSPI();
  }
}

void display_calibrate(bool cal) {
  calibrating = cal;
}

void display_dump_debug() {
  debug("VELOCIDAD:");
  debugln(last_turn_duration);
}
