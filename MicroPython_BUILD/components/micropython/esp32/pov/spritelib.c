#include <esp_system.h>
#include "spritelib.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define COLUMNS 256
#define PIXELS 52
#define ROWS 128

const uint8_t TRANSPARENT = 0xFF;
uint8_t deepspace[] = {51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 35, 34, 33, 32, 31, 30, 30, 29, 28, 27, 27, 26, 25, 25, 24, 23, 23, 22, 21, 21, 20, 19, 19, 18, 18, 17, 17, 16, 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


extern uint32_t* pixels;

Sprite sprites[NUM_SPRITES];

uint32_t* palette_pal;
ImageStrip *image_stripes[NUM_IMAGES];

#define STARS COLUMNS/2

typedef struct {
  uint8_t x;
  uint8_t y;
} Star;
Star starfield[STARS];

int random(int max) {
    return esp_random() % max;
}

void init_sprites() {

  for (int f = 0; f<STARS; f++) {
    starfield[f].x = random(COLUMNS);
    starfield[f].y = random(PIXELS);
  }


  for (int i = 0; i < NUM_SPRITES; i++) {
    sprites[i].x = 0;
    sprites[i].y = 0;
    sprites[i].image_strip = 0;
    sprites[i].frame = DISABLED_FRAME;
  }

}

void step() {
  /*
  for (int n = 0; n<40; n++) {
      Sprite* s = &sprites[n];
      s->frame = (s->frame+1) % 2;
  }
  */
}

void step_starfield() {
  for (int f=0; f<STARS; f++) {
    if(starfield[f].y++ > PIXELS) {
      starfield[f].y = 0;
      starfield[f].x = random(COLUMNS);
    }
  }
}

int inline get_visible_column(int sprite_x, int sprite_width, int render_column) {
    int sprite_column = (render_column - sprite_x + COLUMNS) % COLUMNS;
    if (0 <= sprite_column && sprite_column < sprite_width) {
        return sprite_column;
    } else {
        return -1;
    }
}

void render(int x) {
  x = x % COLUMNS;
  for (int y=0; y<PIXELS; y++) {
    pixels[y] = 0x000000ff;
  }
  for (int f=0; f<STARS; f++) {
    if (starfield[f].x == x) {
      pixels[starfield[f].y] = 0x040404ff;
    }
  }
  /*
  int base = (x % 8) * PIXELS;
  for (int y=0; y<PIXELS; y++) {
    byte color = background[base + y];
    pixels[PIXELS - 1 - y] = palette_pal[color];
  }
  */

  // el sprite 0 se dibuja arriba de todos los otros
  for (int n=NUM_SPRITES-1; n>=0; n--) {
    Sprite* s = &sprites[n%NUM_SPRITES];
    ImageStrip* is = image_stripes[s->image_strip];
    if (s->frame == DISABLED_FRAME) {
      continue;
    }
    uint8_t width = is->frame_width;
    int visible_column = get_visible_column(s->x, width, x);
    if (visible_column != -1) {
      uint8_t height = is->frame_height;
      int desde = MAX(s->y, 0);
      int hasta = MIN(s->y + height, ROWS-1);
      int comienzo = MAX(-s->y, 0);
      int base = visible_column * height + (s->frame * width * height);
      const uint8_t* imagen = is->data + base + comienzo;

      for(int y=desde; y<hasta; y++, imagen++) {
        uint8_t color = *imagen;
        if (color != TRANSPARENT) {
          pixels[deepspace[y]] = palette_pal[color];
        }
      }
    }
  }
}
