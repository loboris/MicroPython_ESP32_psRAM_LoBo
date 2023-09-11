#include "ventilagon.h"

unsigned int last_column_drawn;
int drift_pos;
int drift_speed;
bool calibrating;

int nave_pos = 360;
//int nave_calibrate = -478; // ventilador velocidad media
//int nave_calibrate = -250; // ventilador velocidad maxima
int nave_calibrate = 0; // ventilastation
int half_ship_width = 50;

// defined by Ventilastastion
extern volatile int64_t last_turn;
extern volatile int64_t last_turn_duration;
uint32_t* draw_buffer;

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
   draw_buffer = extra_buf;
}

void display_reset() {
  drift_pos = 0;
  drift_speed = 0;
}

void display_adjust_drift() {
  static int n = 0;
  n = (n+1) & 0x3FF;
  if (n == 0) {
    drift_speed = level_new_drift(drift_speed);
  }
}

int display_ship_rows(int current_pos) {
  if (calibrating) {
    return board_colision(current_pos, ROW_SHIP);
  }

  // NO HAY QUE ARREGLAR NADA ACA

  int d1 = abs(nave_pos - current_pos);
  int d2 = abs(((nave_pos + SUBDEGREES / 2) & SUBDEGREES_MASK) -
           ((current_pos + SUBDEGREES / 2) & SUBDEGREES_MASK));
  if (d1 < half_ship_width || d2 < half_ship_width) {
    return 2;
  }
  if (d1 < (half_ship_width * 2.5) || d2 < (half_ship_width * 2.5)) {
    return 1;
  }

  return 0;
}

int64_t last_drift = 0;
int last_ship_rows = 0;
int last_infront_ship_rows = 0;
int queued_steps = 0;

void display_tick(int64_t now) {
  // esto no hace falta calcularlo tan seguido. Una vez por vuelta deberia alcanzar
  if (now > (last_drift + current_level->step_delay/32)) {
    drift_pos = (drift_pos + drift_speed) & SUBDEGREES_MASK;
    last_drift = now;
  }

  bool need_update = false;
  int64_t drift = drift_pos * last_turn_duration / SUBDEGREES;
  unsigned int current_pos = ((drift + now - last_turn) * SUBDEGREES / last_turn_duration) & SUBDEGREES_MASK;
  unsigned int current_column = ((drift + now - last_turn) * NUM_COLUMNS / last_turn_duration) % NUM_COLUMNS;
  int ship_rows = display_ship_rows(current_pos);
  unsigned int infront_pos = (current_pos + (SUBDEGREES/2)) & SUBDEGREES_MASK;
  int infront_ship_rows = display_ship_rows(infront_pos);

  if (current_column != last_column_drawn) {
    need_update = true;
    last_column_drawn = current_column;
    for (int n = 0; n < queued_steps ; n++) {
      board_step();
    }
    queued_steps = 0;
  }

  if (last_ship_rows != ship_rows || last_infront_ship_rows != infront_ship_rows) {
    need_update = true;
    last_ship_rows = ship_rows;
    last_infront_ship_rows = infront_ship_rows;
  }

  if (need_update) {

    unsigned int opposed_column = (current_column + (NUM_COLUMNS/2)) % NUM_COLUMNS;
    for (int j=0; j<NUM_ROWS; j++) {
      draw_buffer[j] = 0x010000ff;
    }
    board_draw_column(current_column, draw_buffer);
    for (int j=0; j<ship_rows; j++) {
      draw_buffer[ROW_SHIP + j] = SHIP_COLOR;
    }
    for(int k=0; k<NUM_ROWS; k++) {
	pixels0[k] = draw_buffer[NUM_ROWS-k-1];
    }

    for (int l=0; l<NUM_ROWS; l++) {
      draw_buffer[l] = 0x000100ff;
    }
    board_draw_column(opposed_column, draw_buffer);

    for (int j=0; j<infront_ship_rows; j++) {
      draw_buffer[ROW_SHIP + j] = SHIP_COLOR;
    }

    for(int m=0; m<NUM_ROWS; m++) {
        pixels1[m + 54-NUM_ROWS] = draw_buffer[m];
    }

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
