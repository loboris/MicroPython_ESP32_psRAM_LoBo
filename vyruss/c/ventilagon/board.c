#include "ventilagon.h"

static portMUX_TYPE buffer_mutex = portMUX_INITIALIZER_UNLOCKED;
byte buffer[NUM_ROWS];
byte first_row;

void circularbuffer_reset() {
  first_row = 0;
  int n;
  for (n = 0; n < NUM_ROWS; n++) {
    buffer[n] = 0;
  }
}

void circularbuffer_init() {
  circularbuffer_reset();
}

void circularbuffer_push_front(byte row) {
  portENTER_CRITICAL(&buffer_mutex);
  buffer[first_row] = row;
  first_row = (first_row - 1 + NUM_ROWS) % NUM_ROWS;
  portEXIT_CRITICAL(&buffer_mutex);
}

void circularbuffer_push_back(byte row) {
  portENTER_CRITICAL(&buffer_mutex);
  buffer[first_row] = row;
  first_row = (first_row + 1) % NUM_ROWS;
  portEXIT_CRITICAL(&buffer_mutex);
}

byte circularbuffer_get_row(byte row_num) {
  byte pos = (row_num + first_row) % NUM_ROWS;
  return buffer[pos];
}

void board_init() {
  pattern_init();
  board_reset();
}

void board_reset() {
  pattern_randomize();
  circularbuffer_reset();
}

void board_fill_patterns() {
  byte row_num = 20;
  while (row_num != NUM_ROWS) {
    pattern_randomize();

    while (!pattern_is_finished()) {
      circularbuffer_push_back(pattern_next_row());
      row_num++;
      if (row_num == NUM_ROWS) {
        break;
      }
    }
  }
}

bool board_colision(int pos, byte num_row) {
  unsigned int real_pos = (pos + nave_calibrate) & SUBDEGREES_MASK;
  byte ship_column = (real_pos * NUM_COLUMNS) / SUBDEGREES;
  byte row_ship = circularbuffer_get_row(num_row);
  byte mask = 1 << ship_column;
  return row_ship & mask;
}

void board_step() {
  circularbuffer_push_back(pattern_next_row());

  if (pattern_is_finished()) {
    pattern_randomize();
  }
}

void board_step_back() {
  circularbuffer_push_front(0);
}

void board_win_reset() {
  pattern_randomize();
}

void board_win_step_back() {
  circularbuffer_push_front(pattern_next_row());

  if (pattern_is_finished()) {
    pattern_randomize();
  }
}

void board_draw_column(byte column, uint32_t* pixels) {
  byte mask = 1 << column;
  
  // always paint the innermost circle
  ledbar_draw(pixels, 0, true, true);
  
  for (byte n = 1; n < NUM_ROWS; n++) {
    byte row = circularbuffer_get_row(n);
    bool value = row & mask;
    ledbar_draw(pixels, n, value, column & 1);
  }
}

