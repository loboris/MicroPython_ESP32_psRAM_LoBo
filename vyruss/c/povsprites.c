#include "py/nlr.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/binary.h"

#include <stdio.h>
#include <string.h>

#include "minispi.h"
#include "spritelib.h"

#define GPIO_HALL     GPIO_NUM_22
#define ESP_INTR_FLAG_DEFAULT 0
#define COLUMNS 256
#define FASTEST_CREDIBLE_TURN 200

char* sendbuf;
int num_bytes;
uint32_t* pixels; 

volatile int64_t last_turn = 0;
volatile int64_t last_turn_duration = 355 * COLUMNS;

extern void render(int n);
extern void init_sprites();
extern void step();
extern void step_starfield();

inline uint32_t max(uint32_t a, uint32_t b) {
    if (b > a)
        return b;
    return a;
}



void spi_init(int num_pixels) {
    num_bytes = 4 + num_pixels * 4 + 8;
    const long freq = 20000000;
    spiStartBus(freq);
    sendbuf=heap_caps_malloc(num_bytes, MALLOC_CAP_DMA);
    memset(sendbuf, 0, num_bytes);
    pixels = (uint32_t*)(sendbuf+4);
    for(int n=0; n<8; n++) {
        pixels[n] = 0xff000010;
    }
}


void spi_write() {
    spiWriteNL(sendbuf, num_bytes);
}

void spi_shutdown() {
    free(sendbuf);
}


static int taskCore = 0;

void delay(int ms) {
    uint32_t end = esp_timer_get_time() + ms * 1000;
    while (esp_timer_get_time() < end) {
    }
}

uint32_t colors[4] = {
    0xff000011,
    0xff001100,
    0xff110000,
    0xff000000,
};

int color = 0;
uint32_t count = 0;

 
static void IRAM_ATTR hall_sensed(void* arg)
{
    int64_t this_turn = esp_timer_get_time();
    int64_t this_turn_duration = this_turn - last_turn;
    if (this_turn_duration > FASTEST_CREDIBLE_TURN) {
        last_turn_duration = this_turn_duration;
        last_turn = this_turn;
    }
}



void hall_init() {
    gpio_set_direction(GPIO_HALL, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_HALL, GPIO_PULLUP_ONLY);
    gpio_pullup_en(GPIO_HALL);
    gpio_set_intr_type(GPIO_HALL, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_HALL, hall_sensed, (void*) GPIO_HALL);
}


STATIC mp_obj_t povsprites_set_imagestrip(mp_obj_t strip_number, mp_obj_t strip_data) {
    int strip_nr = mp_obj_get_int(strip_number);
    char* strip_data_ptr = mp_obj_str_get_str(strip_data);

    image_stripes[strip_nr] = (ImageStrip *)strip_data_ptr;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(povsprites_set_imagestrip_obj, povsprites_set_imagestrip);
 
//#define BIFLEN 10000
//uint32_t biff[BIFLEN];

void coreTask( void * pvParameters ){
    printf("task running on core %d\n", xPortGetCoreID());
/*
    uint32_t last = esp_timer_get_time();
        for(int n=0; n<8; n++) {
            pixels[n] = colors[color];
        }
        render(color);
        spi_write();
        count++;
    }
*/

    int last_column = 0;
    uint32_t last_step = 0;
    uint32_t last_starfield_step = 0;

    hall_init();
    init_sprites();

    while(true){

 //       for (uint32_t n = 0; n < BIFLEN; n++) {
 //           biff[n] = esp_timer_get_time();

            int64_t now = esp_timer_get_time();
            uint32_t column = ((now - last_turn) * COLUMNS / last_turn_duration) % COLUMNS;
            if (column != last_column) {
                //printf("now %u, column %u\n", now, column);
                render(column);
                spi_write();
                last_column = column;
            }

            if (now > last_starfield_step + 20000) {
                step_starfield();
                last_starfield_step = now;
            }

            if (now > last_step + 500000) {
                step();
                last_step = now;
            }
 //       }
 //       
 //       int count_under = 0;
 //       int count_over = 0;
 //       for (uint32_t n = 1; n < BIFLEN; n++) {
 //           int delta = biff[n] - biff[n - 1];
 //           if (delta < 2) {
 //               count_under++;
 //           }

 //           if (delta > 3) {
 //               count_over++;
 //               printf("%d, ", delta);
 //           }
 //       }

 //       printf("\n%d, %d\n----------\n", count_under, count_over);
    }
 
}

STATIC mp_obj_t povsprites_init(mp_obj_t num_pixels, mp_obj_t palette) {
    spi_init(mp_obj_get_int(num_pixels));
    palette_pal = (uint32_t *) mp_obj_str_get_str(palette);
    printf("creating task, running on core %d\n", xPortGetCoreID());

    xTaskCreatePinnedToCore(
            coreTask,   /* Function to implement the task */
            "coreTask", /* Name of the task */
            10000,      /* Stack size in words */
            NULL,       /* Task input parameter */
            10,          /* Priority of the task */
            NULL,       /* Task handle. */
            taskCore);  /* Core where the task should run */
    printf("task created...\n");
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(povsprites_init_obj, povsprites_init);

STATIC mp_obj_t povsprites_getaddress(mp_obj_t sprite_num) {
    int num = mp_obj_get_int(sprite_num);
    return mp_obj_new_int((mp_int_t)(uintptr_t)&sprites[num]);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(povsprites_getaddress_obj, povsprites_getaddress);
// ------------------------------

STATIC const mp_map_elem_t povsprites_globals_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_povsprites) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_init), (mp_obj_t)&povsprites_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_imagestrip), (mp_obj_t)&povsprites_set_imagestrip_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_getaddress), (mp_obj_t)&povsprites_getaddress_obj },
};

STATIC MP_DEFINE_CONST_DICT (
    mp_module_povsprites_globals,
    povsprites_globals_table
);

const mp_obj_module_t mp_module_povsprites = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_povsprites_globals,
};
