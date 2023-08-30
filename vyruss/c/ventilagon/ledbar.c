// The following two changes are needed in Tlc5940/tlc_config.h
// to enable the grayscales we need:
//
//#define TLC_PWM_PERIOD    2048
//#define TLC_GSCLK_PERIOD    1
//

#include <Tlc5940.h>

Ledbar ledbar;

void Ledbar::setPixelColor(int pixel, long color) {
  byte red = (color >> 16) & 0xff;
  byte green = (color >> 8) & 0xff;
  byte blue = (color >> 0) & 0xff;
  byte base = pixel * 3;
  Tlc.set(base + 0, (blue << 4));
  Tlc.set(base + 1, (green << 4));
  Tlc.set(base + 2, (red << 4));
}

void Ledbar::init() {
  debugln("INIT LEDBAR");
  Tlc.init();
  clear();
}

void Ledbar::clear() {
  Tlc.clear();
}

const long RED = 0xff0000;

void Ledbar::reset() {
  multicolored = false;
}

void Ledbar::set_win_state() {
  multicolored = true;
}

uint32_t colors[] = {
  0x0000ff,
  0x00ff00,
  0xffff00,
  0x00ffff,
  0xff00ff,
  0xff0000,
};

void Ledbar::draw(byte num_row, boolean value, boolean alt_column) {
  long color;
  if (value) {
    if (!multicolored) {
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
  setPixelColor(num_row, color);
}

void Ledbar::update() {
  Tlc.update();
}
