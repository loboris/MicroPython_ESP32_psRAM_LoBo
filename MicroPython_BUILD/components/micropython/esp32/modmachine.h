#ifndef MICROPY_INCLUDED_ESP32_MODMACHINE_H
#define MICROPY_INCLUDED_ESP32_MODMACHINE_H

#include "nvs_flash.h"
#include "nvs.h"

#include "py/obj.h"

extern const mp_obj_type_t machine_timer_type;
extern const mp_obj_type_t machine_pin_type;
extern const mp_obj_type_t machine_touchpad_type;
extern const mp_obj_type_t machine_adc_type;
extern const mp_obj_type_t machine_dac_type;
extern const mp_obj_type_t machine_pwm_type;
extern const mp_obj_type_t machine_hw_spi_type;
extern const mp_obj_type_t machine_hw_i2c_type;
extern const mp_obj_type_t machine_uart_type;
extern const mp_obj_type_t machine_neopixel_type;
extern const mp_obj_type_t machine_dht_type;
extern const mp_obj_type_t machine_onewire_type;
extern const mp_obj_type_t machine_ds18x20_type;

extern nvs_handle mpy_nvs_handle;

void machine_pins_init(void);
void machine_pins_deinit(void);
void prepareSleepReset(uint8_t hrst, char *msg);

#endif // MICROPY_INCLUDED_ESP32_MODMACHINE_H
