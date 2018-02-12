
#include "driver/gpio.h"

#include "py/mphal.h"
#include "modmachine.h"
#include "extmod/virtpin.h"

typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
} machine_pin_obj_t;

typedef struct _machine_pin_irq_obj_t {
    mp_obj_base_t base;
    gpio_num_t id;
} machine_pin_irq_obj_t;

int machine_pin_get_gpio(mp_obj_t pin_in);
