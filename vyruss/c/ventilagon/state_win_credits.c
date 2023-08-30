#include <Tlc5940.h>
CreditsState state_credits;
const long credits_delay = 19000; // 18 seconds

extern const byte PROGMEM text_bitmap[];
unsigned long credits_started;

//uint32_t colors[] = {
//  0x0000ff,
//  0x00ff00,
//  0xffff00,
//  0x00ffff,
//  0xff00ff,
//  0xff0000,
//};

//const byte HALL_SENSOR = 2;
const byte DISPLAY_LEN = 16;
const byte CHAR_WIDTH = 6;
const byte NUM_COLORS = 6;


const char texto2[] PROGMEM = "SUPER VENTILAGON";
const char texto[] PROGMEM = "                SUPER VENTILAGON - Bits: alecu - Volts: Jorge - Waves: Cris - (C) 2015 Club de Jaqueo                          ";
const int size_texto = sizeof(texto) - DISPLAY_LEN;
const long step_delay = 175;

//volatile unsigned long last_turn = 0;
//volatile unsigned long last_turn_duration = 10L;

unsigned long prev_turn = 0;


class TextDisplay {
    volatile int current_char = 0;
    volatile int char_column = 0;
    const byte baseline = 16;
    unsigned long last_step = 0;
    int cursor;

    void setPixelColor(int pixel, long color) {
      byte red = (color >> 16) & 0xff;
      byte green = (color >> 8) & 0xff;
      byte blue = (color >> 0) & 0xff;
      byte base = pixel * 3;
      Tlc.set(base + 0, (blue << 4));
      Tlc.set(base + 1, (green << 4));
      Tlc.set(base + 2, (red << 4));
    }

  public:
    void hall_sensed() {
      current_char = 0;
      char_column = CHAR_WIDTH;
    }
    void reset() {
      Tlc.clear();
      current_char = 0;
      char_column = CHAR_WIDTH;
      cursor = 0;
    }
    void loop() {
      if (last_turn != prev_turn) {
        prev_turn = last_turn;
        hall_sensed();
      }
      static char letra;
      if (char_column >= CHAR_WIDTH) {
        letra = pgm_read_byte(texto + cursor + current_char++);
        char_column = 0;
        if ((millis() - last_step) > step_delay) {
          last_step = millis();
          cursor = (cursor + 1) % size_texto;
        }
      }
      if (current_char >= DISPLAY_LEN) {
        letra = ' ';
      }
      if (letra == 0) {
        return;
      }
      byte v = pgm_read_byte(text_bitmap + letra * CHAR_WIDTH + char_column);

      Tlc.clear();
      uint32_t color = colors[(current_char + cursor) % NUM_COLORS];
      for (int n = 0; n < 8; n++) {
        if (v & (1 << n)) {
          setPixelColor(n * 2 + baseline, color);
          setPixelColor(n * 2 + baseline + 1, color);
        }
      }
      Tlc.update();

      char_column++;
      delayMicroseconds(200);
    };
};

TextDisplay text;

//void handle_interrupt() {
//  unsigned long this_turn = micros();
//  unsigned long this_turn_duration = this_turn - last_turn;
//  last_turn_duration = this_turn_duration;
//  last_turn = this_turn;
//}

void CreditsState::setup() {
  credits_started = millis();
  text.reset();
}

void CreditsState::loop() {
  unsigned long now_ms = millis();
  if ((now_ms - credits_started) > credits_delay) {
    State::change_state(&win_state);
  }
  text.loop();
}
