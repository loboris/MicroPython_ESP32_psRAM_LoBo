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
 * @file ds18b20.h
 * @brief Interface definitions for the Maxim Integrated DS18B20 Programmable
 *        Resolution 1-Wire Digital Thermometer device.
 *
 * This component provides structures and functions that are useful for communicating
 * with DS18B20 devices connected via a Maxim Integrated 1-Wire® bus.
 */

#ifndef DS18B20_H
#define DS18B20_H

#include "owb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Return this for readings that suffer CRC errors (msb=0x80, lsb=0x00)
#define DS18B20_INVALID_READING (-2048.0f)

// DS temperature sensor family codes
#define DS18B20_FAMILY_CODE			0x28
#define DS18S20_FAMILY_CODE			0x10
#define DS1822_FAMILY_CODE			0x22
#define DS28EA00_FAMILY_CODE		0x42

#define T_CONV	750   // maximum conversion time at 12-bit resolution in milliseconds

/**
 * @brief Symbols for the supported temperature resolution of the device.
 */
typedef enum
{
    DS18B20_RESOLUTION_INVALID = -1,  ///< Invalid resolution
    DS18B20_RESOLUTION_9_BIT   = 9,   ///< 9-bit resolution, LSB bits 2,1,0 undefined
    DS18B20_RESOLUTION_10_BIT  = 10,  ///< 10-bit resolution, LSB bits 1,0 undefined
    DS18B20_RESOLUTION_11_BIT  = 11,  ///< 11-bit resolution, LSB bit 0 undefined
    DS18B20_RESOLUTION_12_BIT  = 12,  ///< 12-bit resolution (default)
} DS18B20_RESOLUTION;

/**
 * @brief Structure containing information related to a single DS18B20 device connected
 * via a 1-Wire bus.
 */
typedef struct
{
    bool init;                     ///< True if struct has been initialized, otherwise false
    bool solo;                     ///< True if device is intended to be the only one connected to the bus, otherwise false
    bool use_crc;                  ///< True if CRC checks are to be used when retrieving information from a device on the bus
    const OneWireBus * bus;        ///< Pointer to 1-Wire bus information relevant to this device
    OneWireBus_ROMCode rom_code;   ///< The ROM code used to address this device on the bus
    DS18B20_RESOLUTION resolution; ///< Temperature measurement resolution per reading
} DS18B20_Info;

/**
 * @brief Construct a new device info instance.
 *        New instance should be initialized before calling other functions.
 * @return Pointer to new device info instance, or NULL if it cannot be created.
 */
DS18B20_Info * ds18b20_malloc(void);

/**
 * @brief Delete an existing device info instance.
 * @param[in] ds18b20_info Pointer to device info instance.
 * @param[in,out] ds18b20_info Pointer to device info instance that will be freed and set to NULL.
 */
void ds18b20_free(DS18B20_Info ** ds18b20_info);

/**
 * @brief Initialise a device info instance
 * @param[in] ds18b20_info Pointer to device info instance.
 * @param[in] bus Pointer to initialized 1-Wire bus instance.
 * @param[in] rom_code Device-specific ROM code to identify a device on the bus.
 */
void ds18b20_init(DS18B20_Info * ds18b20_info, const OneWireBus * bus, OneWireBus_ROMCode rom_code);

/**
 * @brief Initialise a device info instance as a solo device on the bus.
 *
 * This is subject to the requirement that this device is the ONLY device on the bus.
 * This allows for faster commands to be used without ROM code addressing.
 *
 * NOTE: if additional devices are added to the bus, operation will cease to work correctly.
 *
 * @param[in] ds18b20_info Pointer to device info instance.
 * @param[in] bus Pointer to initialized 1-Wire bus instance.
 * @param[in] rom_code Device-specific ROM code to identify a device on the bus.
 */
void ds18b20_init_solo(DS18B20_Info * ds18b20_info, const OneWireBus * bus);

/**
 * @brief Enable or disable use of CRC checks on device communications.
 * @param[in] ds18b20_info Pointer to device info instance.
 * @param[in] use_crc True to enable CRC checks, false to disable.
 */
void ds18b20_use_crc(DS18B20_Info * ds18b20_info, bool use_crc);

/**
 * @brief Set temperature measurement resolution.
 *
 * This programs the hardware to the specified resolution and sets the cached value to be the same.
 * If the program fails, the value currently in hardware is used to refresh the cache.
 *
 * @param[in] ds18b20_info Pointer to device info instance.
 * @param[in] resolution Selected resolution.
 * @return True if successful, otherwise false.
 */
bool ds18b20_set_resolution(DS18B20_Info * ds18b20_info, DS18B20_RESOLUTION resolution);

/**
 * @brief Update and return the current temperature measurement resolution from the device.
 * @param[in] ds18b20_info Pointer to device info instance.
 * @return The currently configured temperature measurement resolution.
 */
DS18B20_RESOLUTION ds18b20_read_resolution(DS18B20_Info * ds18b20_info);

/**
 * @brief Read 64-bit ROM code from device - only works when there is a single device on the bus.
 * @param[in] ds18b20_info Pointer to device info instance.
 * @return The 64-bit value read from the device's ROM.
 */
OneWireBus_ROMCode ds18b20_read_rom(DS18B20_Info * ds18b20_info);

/**
 * @brief Start a temperature measurement conversion on a single device.
 * @param[in] ds18b20_info Pointer to device info instance.
 */
bool ds18b20_convert(const DS18B20_Info * ds18b20_info);

/**
 * @brief Start temperature conversion on all connected devices.
 *
 * This should be followed by a sufficient delay to ensure all devices complete
 * their conversion before the measurements are read.
 * @param[in] bus Pointer to initialized bus instance.
 */
void ds18b20_convert_all(const DS18B20_Info * ds18b20_info);

/**
 * @brief Wait for the maximum conversion time according to the current resolution of the device.
 * @param[in] bus Pointer to initialized bus instance.
 * @return An estimate of the time elapsed, in milliseconds. Actual elapsed time may be greater.
 */
uint32_t ds18b20_wait_for_conversion(const DS18B20_Info * ds18b20_info);
uint32_t _wait_for_conversion(DS18B20_RESOLUTION resolution);

/**
 * @brief Read last temperature measurement from device.
 *
 * This is typically called after ds18b20_start_mass_conversion(), provided enough time
 * has elapsed to ensure that all devices have completed their conversions.
 * @param[in] ds18b20_info Pointer to device info instance. Must be initialized first.
 * @return The measurement value returned by the device, in degrees Celsius.
 */
float ds18b20_read_temp(const DS18B20_Info * ds18b20_info);
int16_t ds18b20_read_raw_temp(const DS18B20_Info * ds18b20_info);

/**
 * @brief Convert, wait and read current temperature from device.
 * @param[in] ds18b20_info Pointer to device info instance. Must be initialized first.
 * @return The measurement value returned by the device, in degrees Celsius.
 */
float ds18b20_convert_and_read_temp(const DS18B20_Info * ds18b20_info);
int16_t ds18b20_convert_and_read_raw_temp(const DS18B20_Info * ds18b20_info);


uint8_t _is_DS18B20_family(uint8_t code);
void DS18B20_Family(uint8_t code, char *dsfamily);
uint8_t ds18b20_get_power_mode(DS18B20_Info * ds18b20_info);

#ifdef __cplusplus
}
#endif

#endif  // DS18B20_H
