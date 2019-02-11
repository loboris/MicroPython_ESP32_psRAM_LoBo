#include <esp_system.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint8_t x;
  int8_t y;
  uint8_t image_strip;
  int8_t frame;
} Sprite;

typedef struct {
  const uint8_t frame_width;
  const uint8_t frame_height;
  const uint8_t total_frames;
  const uint8_t palette;
  const uint8_t data[];
} ImageStrip;

#define NUM_SPRITES 64
extern Sprite sprites[NUM_SPRITES];

#define NUM_IMAGES 16
extern ImageStrip *image_stripes[NUM_IMAGES];

static const int8_t DISABLED_FRAME = -1;
extern uint32_t* palette_pal;
