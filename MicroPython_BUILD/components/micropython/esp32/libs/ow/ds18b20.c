/*
 * MIT License
 *
 * Copyright (c) 2017 David Antliff
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file ds18b20.c
 *
 * Resolution is cached in the DS18B20_Info object to avoid querying the hardware
 * every time a temperature conversion is required. However this can result in the
 * cached value becoming inconsistent with the hardware value, so care must be taken.
 *
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "libs/ow/ds18b20.h"
#include "libs/ow/owb.h"
#include "mphalport.h"

static const char * TAG = "ds18b20";

// Function commands
#define DS18B20_FUNCTION_TEMP_CONVERT       0x44
#define DS18B20_FUNCTION_SCRATCHPAD_WRITE   0x4E
#define DS18B20_FUNCTION_SCRATCHPAD_READ    0xBE
#define DS18B20_FUNCTION_SCRATCHPAD_COPY    0x48
#define DS18B20_FUNCTION_EEPROM_RECALL      0xB8
#define DS18B20_FUNCTION_POWER_SUPPLY_READ  0xB4

/// @cond ignore
typedef struct
{
    uint8_t temperature[2];    // [0] is LSB, [1] is MSB
    uint8_t trigger_high;
    uint8_t trigger_low;
    uint8_t configuration;
    uint8_t reserved[3];
    uint8_t crc;
} Scratchpad;
/// @endcond ignore


//-----------------------------------------
uint8_t _is_DS18B20_family(uint8_t code) {
	/* Checks if first byte is equal to DS18B20's family code */
	if ((code == DS18B20_FAMILY_CODE) || (code == DS18S20_FAMILY_CODE) || (code == DS1822_FAMILY_CODE)  || (code == DS28EA00_FAMILY_CODE)) {
		return 1;
	}
	return 0;
}

//-------------------------------------------------
void DS18B20_Family(uint8_t code, char *dsfamily) {
  switch (code) {
  	  case DS18B20_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS18B20");
	  	  break;
  	  case DS18S20_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS18S20");
	  	  break;
  	  case DS1822_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS1822");
	  	  break;
  	  case DS28EA00_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS28EA00");
	  	  break;
  	  default:
  		  sprintf(dsfamily, "unknown");
  }
}


static void _init(DS18B20_Info * ds18b20_info, const OneWireBus * bus)
{
    if (ds18b20_info != NULL)
    {
        ds18b20_info->bus = bus;
        memset(&ds18b20_info->rom_code, 0, sizeof(ds18b20_info->rom_code));
        ds18b20_info->use_crc = false;
        ds18b20_info->resolution = DS18B20_RESOLUTION_INVALID;
        ds18b20_info->solo = false;   // assume multiple devices unless told otherwise
        ds18b20_info->init = true;
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

static bool _is_init(const DS18B20_Info * ds18b20_info)
{
    bool ok = false;
    if (ds18b20_info != NULL)
    {
        if (ds18b20_info->init)
        {
            // OK
            ok = true;
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20_info is not initialised");
        }
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
    return ok;
}

static bool _address_device(const DS18B20_Info * ds18b20_info)
{
    bool present = false;
    if (_is_init(ds18b20_info))
    {
        owb_reset(ds18b20_info->bus, &present);
        if (present)
        {
            if (ds18b20_info->solo)
            {
                // if there's only one device on the bus, we can skip
                // sending the ROM code and instruct it directly
                owb_write_byte(ds18b20_info->bus, OWB_ROM_SKIP);
            }
            else
            {
                // if there are multiple devices on the bus, a Match ROM command
                // must be issued to address a specific slave
                owb_write_byte(ds18b20_info->bus, OWB_ROM_MATCH);
                owb_write_rom_code(ds18b20_info->bus, ds18b20_info->rom_code);
            }
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20 device not responding");
        }
    }
    return present;
}


static bool _check_resolution(DS18B20_RESOLUTION resolution)
{
    return (resolution >= DS18B20_RESOLUTION_9_BIT) && (resolution <= DS18B20_RESOLUTION_12_BIT);
}

//----------------------------------------------------------
uint32_t _wait_for_conversion(DS18B20_RESOLUTION resolution)
{
    uint32_t max_conversion_time = 0;
    if (_check_resolution(resolution)) {
        uint32_t max_conversion_time = (T_CONV >> (DS18B20_RESOLUTION_12_BIT - resolution)) + 2;

        // wait at least this maximum conversion time
        mp_hal_delay_ms(max_conversion_time);
    }
    return max_conversion_time;
}

static int16_t _decode_temp_int(uint8_t lsb, uint8_t msb, DS18B20_RESOLUTION resolution)
{
    int16_t result = 0;
    if (_check_resolution(resolution))
    {
        // masks to remove undefined bits from result
        static const uint8_t lsb_mask[4] = { ~0x03, ~0x02, ~0x01, ~0x00 };
        uint8_t lsb_masked = lsb_mask[resolution - DS18B20_RESOLUTION_9_BIT] & lsb;
        int16_t raw = (msb << 8) | lsb_masked;
        result = raw;
    }
    else
    {
        ESP_LOGE(TAG, "Unsupported resolution %d", resolution);
    }
    return result;
}

static float _decode_temp(uint8_t lsb, uint8_t msb, DS18B20_RESOLUTION resolution)
{
    float result = 0.0f;
    if (_check_resolution(resolution))
    {
        // masks to remove undefined bits from result
        static const uint8_t lsb_mask[4] = { ~0x03, ~0x02, ~0x01, ~0x00 };
        uint8_t lsb_masked = lsb_mask[resolution - DS18B20_RESOLUTION_9_BIT] & lsb;
        int16_t raw = (msb << 8) | lsb_masked;
        result = raw / 16.0f;
    }
    else
    {
        ESP_LOGE(TAG, "Unsupported resolution %d", resolution);
    }
    return result;
}

static size_t _min(size_t x, size_t y)
{
    return x > y ? y : x;
}

static Scratchpad _read_scratchpad(const DS18B20_Info * ds18b20_info, size_t count)
{
    count = _min(sizeof(Scratchpad), count);   // avoid overflow
    Scratchpad scratchpad = {0};
    ESP_LOGD(TAG, "scratchpad read %d bytes: ", count);
    if (_address_device(ds18b20_info))
    {
        owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_SCRATCHPAD_READ);
        owb_read_bytes(ds18b20_info->bus, (uint8_t *)&scratchpad, count);
        //esp_log_buffer_hex(TAG, &scratchpad, count);
    }
    return scratchpad;
}

static bool _write_scratchpad(const DS18B20_Info * ds18b20_info, const Scratchpad * scratchpad, bool verify)
{
    bool result = false;
    // Only bytes 2, 3 and 4 (trigger and configuration) can be written.
    // All three bytes MUST be written before the next reset to avoid corruption.
    if (_is_init(ds18b20_info))
    {
        if (_address_device(ds18b20_info))
        {
            owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_SCRATCHPAD_WRITE);
            owb_write_bytes(ds18b20_info->bus, (uint8_t *)&scratchpad->trigger_high, 3);
            ESP_LOGD(TAG, "scratchpad write 3 bytes:");
            //esp_log_buffer_hex(TAG, &scratchpad->trigger_high, 3);
            result = true;

            if (verify)
            {
                Scratchpad read = _read_scratchpad(ds18b20_info, offsetof(Scratchpad, configuration) + 1);
                if (memcmp(&scratchpad->trigger_high, &read.trigger_high, 3) != 0)
                {
                    ESP_LOGE(TAG, "scratchpad verify failed: "
                            "wrote {0x%02x, 0x%02x, 0x%02x}, "
                            "read {0x%02x, 0x%02x, 0x%02x}",
                            scratchpad->trigger_high, scratchpad->trigger_low, scratchpad->configuration,
                            read.trigger_high, read.trigger_low, read.configuration);
                    result = false;
                }
            }
        }
    }
    return result;
}


// Public API

DS18B20_Info * ds18b20_malloc(void)
{
    DS18B20_Info * ds18b20_info = malloc(sizeof(*ds18b20_info));
    if (ds18b20_info != NULL)
    {
        memset(ds18b20_info, 0, sizeof(*ds18b20_info));
        ESP_LOGD(TAG, "malloc %p", ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "malloc failed");
    }

    return ds18b20_info;
}

void ds18b20_free(DS18B20_Info ** ds18b20_info)
{
    if (ds18b20_info != NULL && (*ds18b20_info != NULL))
    {
        ESP_LOGD(TAG, "free %p", *ds18b20_info);
        free(*ds18b20_info);
        *ds18b20_info = NULL;
    }
}

void ds18b20_init(DS18B20_Info * ds18b20_info, const OneWireBus * bus, OneWireBus_ROMCode rom_code)
{
    if (ds18b20_info != NULL)
    {
        _init(ds18b20_info, bus);
        ds18b20_info->rom_code = rom_code;

        // read current resolution from device as it may not be power-on or factory default
        ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

void ds18b20_init_solo(DS18B20_Info * ds18b20_info, const OneWireBus * bus)
{
    if (ds18b20_info != NULL)
    {
        _init(ds18b20_info, bus);
        ds18b20_info->solo = true;

        // read current resolution from device as it may not be power-on or factory default
        ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
    }
    else
    {
        ESP_LOGE(TAG, "ds18b20_info is NULL");
    }
}

void ds18b20_use_crc(DS18B20_Info * ds18b20_info, bool use_crc)
{
    if (_is_init(ds18b20_info))
    {
        ds18b20_info->use_crc = use_crc;
        ESP_LOGD(TAG, "use_crc %d", ds18b20_info->use_crc);
    }
}

bool ds18b20_set_resolution(DS18B20_Info * ds18b20_info, DS18B20_RESOLUTION resolution)
{
    bool result = false;
    if (_is_init(ds18b20_info))
    {
        if (_check_resolution(ds18b20_info->resolution))
        {
            // read scratchpad up to and including configuration register
            Scratchpad scratchpad = _read_scratchpad(ds18b20_info,
                    offsetof(Scratchpad, configuration) - offsetof(Scratchpad, temperature) + 1);

            // modify configuration register to set resolution
            uint8_t value = (((resolution - 1) & 0x03) << 5) | 0x1f;
            scratchpad.configuration = value;
            ESP_LOGD(TAG, "configuration value 0x%02x", value);

            // write bytes 2, 3 and 4 of scratchpad
            result = _write_scratchpad(ds18b20_info, &scratchpad, /* verify */ true);
            if (result)
            {
                ds18b20_info->resolution = resolution;
                ESP_LOGD(TAG, "Resolution set to %d bits", (int)resolution);
            }
            else
            {
                // Resolution change failed - update the info resolution with the value read from configuration
                ds18b20_info->resolution = ds18b20_read_resolution(ds18b20_info);
                ESP_LOGW(TAG, "Resolution consistency lost - refreshed from device: %d", ds18b20_info->resolution);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Unsupported resolution %d", resolution);
        }
    }
    return result;
}

DS18B20_RESOLUTION ds18b20_read_resolution(DS18B20_Info * ds18b20_info)
{
    DS18B20_RESOLUTION resolution = DS18B20_RESOLUTION_INVALID;
    if (_is_init(ds18b20_info))
    {
        // read scratchpad up to and including configuration register
        Scratchpad scratchpad = _read_scratchpad(ds18b20_info,
                offsetof(Scratchpad, configuration) - offsetof(Scratchpad, temperature) + 1);

        resolution = ((scratchpad.configuration >> 5) & 0x03) + DS18B20_RESOLUTION_9_BIT;
        if (!_check_resolution(resolution))
        {
            ESP_LOGE(TAG, "invalid resolution read from device: 0x%02x", scratchpad.configuration);
            resolution = DS18B20_RESOLUTION_INVALID;
        }
        else
        {
            ESP_LOGD(TAG, "Resolution read as %d", resolution);
        }
    }
    return resolution;
}

bool ds18b20_convert(const DS18B20_Info * ds18b20_info)
{
    bool result = false;
    if (_is_init(ds18b20_info))
    {
        const OneWireBus * bus = ds18b20_info->bus;
        if (_address_device(ds18b20_info))
        {
            // initiate a temperature measurement
            owb_write_byte(bus, DS18B20_FUNCTION_TEMP_CONVERT);
            if (ds18b20_info->bus->parasite_pwr) {
            	GPIO.pin[ds18b20_info->bus->gpio].pad_driver = 0;
            }
            result = true;
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20 device not responding");
        }
    }
    return result;
}

void ds18b20_convert_all(const DS18B20_Info * ds18b20_info)
{
    bool is_present = false;
    owb_reset(ds18b20_info->bus, &is_present);
    owb_write_byte(ds18b20_info->bus, OWB_ROM_SKIP);
    owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_TEMP_CONVERT);
    if (ds18b20_info->bus->parasite_pwr) {
    	GPIO.pin[ds18b20_info->bus->gpio].pad_driver = 0;
    }
}

//---------------------------------------------------------------------
uint32_t ds18b20_wait_for_conversion(const DS18B20_Info * ds18b20_info)
{
    uint32_t elapsed_time = 0.0f;
    if (_is_init(ds18b20_info))
    {
        elapsed_time = _wait_for_conversion(ds18b20_info->resolution);
    }
    return elapsed_time;
}

//------------------------------------------------------------------
static int _ds18b20_read_raw_temp(const DS18B20_Info * ds18b20_info)
{
	int temper = -99999;
    if (_is_init(ds18b20_info))
    {
        const OneWireBus * bus = ds18b20_info->bus;
        if (_address_device(ds18b20_info))
        {
            // read measurement
            owb_write_byte(bus, DS18B20_FUNCTION_SCRATCHPAD_READ);

            uint8_t temp_LSB = 0;
            uint8_t temp_MSB = 0;
            if (!ds18b20_info->use_crc)
            {
                // Without CRC:
                owb_read_byte(bus, &temp_LSB);
                owb_read_byte(bus, &temp_MSB);
                bool is_present = false;
                owb_reset(bus, &is_present);  // terminate early
            }
            else
            {
                // with CRC:
                uint8_t buffer[9] = {0};
                owb_read_bytes(bus, buffer, 9);

                temp_LSB = buffer[0];
                temp_MSB = buffer[1];

                if (owb_crc8_bytes(0, buffer, 9) != 0)
                {
                    ESP_LOGE(TAG, "CRC failed");
                    temp_LSB = 0x00;
                    temp_MSB = 0x80;
                }
            }

            ESP_LOGD(TAG, "temp_LSB 0x%02x, temp_MSB 0x%02x", temp_LSB, temp_MSB);
            temper = ((int)temp_MSB << 8) | (int)temp_LSB;
        }
        else
        {
            ESP_LOGE(TAG, "ds18b20 device not responding");
        }
    }
    return temper;
}

int16_t ds18b20_read_raw_temp(const DS18B20_Info * ds18b20_info)
{
    int16_t temp = 0;
    int temper = _ds18b20_read_raw_temp(ds18b20_info);
    if (temper != -99999) {
    	temp = _decode_temp_int((uint8_t)(temper & 0xFF), (uint8_t)(temper >> 8), ds18b20_info->resolution);
    }
    return temp;
}

float ds18b20_read_temp(const DS18B20_Info * ds18b20_info)
{
    float temp = 0.0f;
    int temper = _ds18b20_read_raw_temp(ds18b20_info);
    if (temper != -99999) {
    	temp = _decode_temp((uint8_t)(temper & 0xFF), (uint8_t)(temper >> 8), ds18b20_info->resolution);
    }
    return temp;
}

float ds18b20_convert_and_read_temp(const DS18B20_Info * ds18b20_info)
{
    float temp = 0.0f;
    if (_is_init(ds18b20_info))
    {
        if (ds18b20_convert(ds18b20_info))
        {
            // wait at least maximum conversion time
            _wait_for_conversion(ds18b20_info->resolution);

            temp = ds18b20_read_temp(ds18b20_info);
        }
    }
    return temp;
}

int16_t ds18b20_convert_and_read_raw_temp(const DS18B20_Info * ds18b20_info)
{
    int16_t temp = 0;
    if (_is_init(ds18b20_info))
    {
        if (ds18b20_convert(ds18b20_info))
        {
            // wait at least maximum conversion time
            _wait_for_conversion(ds18b20_info->resolution);

            temp = ds18b20_read_raw_temp(ds18b20_info);
        }
    }
    return temp;
}

//-----------------------------------------------------------
uint8_t ds18b20_get_power_mode(DS18B20_Info * ds18b20_info) {
    bool present = false;
    uint8_t res = 3;

    if (_is_init(ds18b20_info)) {
        owb_reset(ds18b20_info->bus, &present);
        if (present) {
            owb_write_byte(ds18b20_info->bus, OWB_ROM_SKIP);
            owb_write_byte(ds18b20_info->bus, DS18B20_FUNCTION_POWER_SUPPLY_READ);
            owb_read_byte(ds18b20_info->bus, &res);
            return res;
        }
        else {
            ESP_LOGE(TAG, "ds18b20 device not responding");
            return 2;
        }
    }
    return 1;
}

