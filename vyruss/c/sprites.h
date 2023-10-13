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
    uint8_t frame;
    const ImageStrip* image_strip;
    int8_t perspective;
    uint8_t sprite_id;
} sprite_obj_t;

#define NUM_SPRITES 100
extern sprite_obj_t* sprites[NUM_SPRITES];

#define NUM_IMAGES 40
extern const ImageStrip* image_stripes[NUM_IMAGES];

static const uint8_t DISABLED_FRAME = 255;
