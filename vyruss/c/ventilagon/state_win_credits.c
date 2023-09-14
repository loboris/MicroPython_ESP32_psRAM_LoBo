#include "ventilagon.h"
const uint64_t credits_duration = 19750; // 19.75 seconds

extern const byte text_bitmap[];
uint64_t credits_started;

// defined by Ventilastastion
extern volatile int64_t last_turn;
extern volatile int64_t last_turn_duration;
uint32_t* draw_buffer;

//const byte HALL_SENSOR = 2;
const byte CHAR_WIDTH = 6;
const byte NUM_COLORS = 6;

uint64_t millis() {
  return esp_timer_get_time() / 1000;
}

const char texto[] = "                     SUPER VENTILAGON - Bits: alecu - Volts: Jorge - Waves: Cris - Voces: Nessita - (C) 2015 Club de Jaqueo                          ";
uint64_t step_delay;
uint64_t last_step;
int step_position;

void credits_reset(uint64_t now) {
  step_delay = credits_duration * 1000 / (elements_in(texto) * CHAR_WIDTH);
  step_position = 0;
  last_step = now;
}

void credits_draw_column(int visible_column) {
  int x = visible_column + step_position;
  int cursor = x / CHAR_WIDTH;
  int columna_letra = x % CHAR_WIDTH;

  if (cursor >= elements_in(texto)) {
    cursor = 0;
  }
  char letra = texto[cursor];
  byte v = text_bitmap[letra * CHAR_WIDTH + columna_letra];

  uint32_t color = colors[cursor % NUM_COLORS];

  for (int n = 0; n < 8; n++) {
    if (v & (1 << n)) {
      draw_buffer[n * 2] = color;
      draw_buffer[n * 2 + 1] = color;
    }
  }
}

void text_loop2(int64_t now) {
  static int last_column_drawn = 0;
  int current_column = ((now - last_turn) * 256 / last_turn_duration) % 256;

  // only draw the upper half
  if (current_column != last_column_drawn) {

    for (int j=0; j<54; j++) {
      pixels0[j] = 0x000000ff;
      pixels1[j] = 0x000000ff;
      draw_buffer[j] = 0x000000ff;
    }

    if (current_column >= 64 && current_column < 192) {
      int visible_column = current_column - 64;
      credits_draw_column(visible_column);
      for(int k=0; k<16; k++) {
	  pixels1[54 - 16 + k] = draw_buffer[k];
      }
    } else {
      int visible_column = (current_column + 64) % 256;
      credits_draw_column(visible_column);
      for(int m=0; m<16; m++) {
	  pixels0[15-m] = draw_buffer[m];
      }
    }

    spi_write_HSPI();
    last_column_drawn = current_column;
  }

  if ((now - last_step) > step_delay) {
    step_position++;
    last_step = now;
  }
}

//void handle_interrupt() {
//  unsigned long this_turn = micros();
//  unsigned long this_turn_duration = this_turn - last_turn;
//  last_turn_duration = this_turn_duration;
//  last_turn = this_turn;
//}

void credits_setup() {
  int64_t now = esp_timer_get_time();
  credits_started = now / 1000;
  credits_reset(now);
  serial_send("arduino stop");
}

void credits_loop() {
  int64_t now = esp_timer_get_time();
  int64_t now_ms = now / 1000;
  if ((now_ms - credits_started) > credits_duration) {
    change_state(&win_state);
  }
  text_loop2(now);
}

State credits_state = { "Rolling Credits", credits_setup, credits_loop};
