// The following two changes are needed in Tlc5940/tlc_config.h
// to enable the grayscales we need:
//
//#define TLC_PWM_PERIOD    2048
//#define TLC_GSCLK_PERIOD    1
//

#include "ventilagon.h"

void ledbar_setPixelColor(uint32_t* buffer, int pixel, uint32_t color) {
  buffer[pixel] = color;
}

void ledbar_init() {
}

const long RED = 0xff0000;

void ledbar_reset() {
  ledbar_multicolored = false;
}

void ledbar_set_win_state() {
  ledbar_multicolored = true;
}

uint32_t colors[] = {
  0x000066ff,
  0x006600ff,
  0x444400ff,
  0x004444ff,
  0x440044ff,
  0x660000ff,
};

void ledbar_draw(uint32_t* pixels, byte num_row, bool value, bool alt_column) {
  uint32_t color;
  if (value) {
    if (!ledbar_multicolored) {
      color = current_level->color;
    } else {
      color = colors[((num_row>>2)+(alt_column<<1))%6];
    }
  } else { 
    color = alt_column ? current_level->bg1 : current_level->bg2;
  }
  if (num_row == ROW_SHIP && value) {
    //color = RED;
  }
  ledbar_setPixelColor(pixels, num_row, color);
}
