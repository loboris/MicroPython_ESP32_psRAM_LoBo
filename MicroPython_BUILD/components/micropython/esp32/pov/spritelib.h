#include <esp_system.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t byte;

typedef struct {
  const byte* image;
  byte x;
  int8_t y;
  byte w; // width
  byte h; // height
  byte frame;
  bool enabled;
} Sprite;

#define NUM_SPRITES 48
extern Sprite sprites[NUM_SPRITES];
