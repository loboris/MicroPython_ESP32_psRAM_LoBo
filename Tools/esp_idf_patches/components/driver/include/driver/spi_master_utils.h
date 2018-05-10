// Copyright 2010-2017 Espressif Systems (Shanghai) PTE LTD
// Copyright 2017 LoBo (https://github.com/loboris)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _DRIVER_SPI_MASTER_UTILS_H_
#define _DRIVER_SPI_MASTER_UTILS_H_

#include "driver/spi_master.h"
#include "driver/spi_master_internal.h"

typedef struct {
    spi_device_handle_t           handle;
    int8_t                        cs;
    int8_t                        dc;
    uint8_t                       selected;
    uint8_t                       spihost;
    uint8_t                       dma_channel;
    uint32_t                      curr_clock;
    spi_bus_config_t              *buscfg;
    spi_device_interface_config_t devcfg;
} exspi_device_handle_t;


#define SPI_SEMAPHORE_WAIT  2000    // Time in ms to wait for SPI mutex
#define SDSPI_HOST_ID       -2      // sdspi ID
#define TOTAL_CS            (NO_CS*2)

extern spi_bus_config_t *SPIbus_configs[3];
extern QueueHandle_t spi_utils_mutex;

/**
 * @brief Check if spi slot with the same/different configuration is used
 *
 * @param spidev ext spidev structure to check
 *
 * @return
 *      - ESP_OK    if other device uses the same spi host with the same configuration
 *      - ESP_FAIL  if other device uses the same spi host with different configuration
 *      - 1         if the spi sost is not used
 */
esp_err_t check_spi_host(exspi_device_handle_t *spidev);

/**
 * @brief Check if some spi bus is used by sdspi driver
 *
 * @return
 *      - spi host slot used by sdspi driver or 0 if sdspi driver not used
 */
int spi_host_used_by_sdspi();

/**
 * @brief Check for spi bus slots used by other than sdspi drivers
 *
 * @return
 *      - HSPI_HOST | VSPI_HOST combined integer
 */
int spi_host_not_used_by_sdspi();

/**
 * @brief Add ext spi device to the list of used ext spi devices
 *
 * Checks for conflicts with other devices
 * Initializes the spi bud if needed
 * Add the device to the spi buscfg
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
esp_err_t add_extspi_device(exspi_device_handle_t *spidev);

/**
 * @brief Remove ext spi device from the list of used ext spi devices
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
esp_err_t remove_extspi_device(exspi_device_handle_t *spidev);

esp_err_t spi_device_select(exspi_device_handle_t *spidev, int force);

esp_err_t spi_device_deselect(exspi_device_handle_t *spidev);

esp_err_t spi_transfer_data_nodma(exspi_device_handle_t *spidev, spi_transaction_t *trans);

uint32_t spi_get_speed(exspi_device_handle_t *spidev);

uint32_t spi_set_speed(exspi_device_handle_t *spidev, uint32_t speed);

bool spi_uses_native_pins(spi_device_handle_t handle);

void _spi_transfer_start(exspi_device_handle_t *spi_dev, int wrbits, int rdbits);

void _dma_send(exspi_device_handle_t *spi_dev, uint8_t *data, uint32_t size);

esp_err_t _wait_trans_finish(exspi_device_handle_t *spi_dev);


#endif
