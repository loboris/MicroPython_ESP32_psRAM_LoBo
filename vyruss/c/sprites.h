#include <stdint.h>

typedef struct {
    const uint8_t frame_width;
    const uint8_t frame_height;
    const uint8_t total_frames;
    const uint8_t palette;
    const uint8_t data[];
} ImageStrip;

typedef struct _sprite_obj_t {
    mp_obj_base_t   base;
    uint8_t x;
    uint8_t y;
    int8_t frame;
    ImageStrip* image_strip;
} sprite_obj_t;

#define NUM_SPRITES 64
extern sprite_obj_t* sprites[NUM_SPRITES];

#define NUM_IMAGES 16
extern ImageStrip* image_stripes[NUM_IMAGES];

static const int8_t DISABLED_FRAME = -1;
