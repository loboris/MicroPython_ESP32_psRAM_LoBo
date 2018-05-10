// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
// Copyright 2017-2018 LoBo (https://github.com/loboris)
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

/*
Additional spi_master functions and utilities

*/

#include "driver/spi_master_utils.h"
#include "soc/spi_reg.h"
#include "esp_log.h"
#include "sdspi_host.h"
#include "sdkconfig.h"

/*
 * There are two SPI hosts on ESP32 available to the user, HSPI_HOST & VSPI_HOST
 * If psRAM is used and is configured to run at 80 MHz, only HSPI_HOST is available !
 * Each SPI host is configured with miso, mosi & sck pins on which it operates
 * Up to 6 spi devices can be attached to each SPI host, all must use different CS pin
 * CS is handled by the driver using spi_device_select() / spi_device_deselect() functions
 */

// SPI bus configuration is used by generic SPI, Display and SDCard SPI
#if CONFIG_SPIRAM_SPEED_80M
static spi_bus_config_t HSPI_buscfg = {-1, -1, -1, -1, -1, 0, 0};
spi_bus_config_t *SPIbus_configs[3] = {NULL, &HSPI_buscfg, NULL};
#else
static spi_bus_config_t HSPI_buscfg = {-1, -1, -1, -1, -1, 0, 0};
static spi_bus_config_t VSPI_buscfg = {-1, -1, -1, -1, -1, 0, 0};
spi_bus_config_t *SPIbus_configs[3] = {NULL, &HSPI_buscfg, &VSPI_buscfg};
#endif


QueueHandle_t spi_utils_mutex = NULL;

static exspi_device_handle_t *extspi_devices[TOTAL_CS] = {NULL};
static uint8_t _dma_sending = 0;
static uint8_t prev_bus = 0;

static const char TAG[] = "[SPI_UTILS]";


/*
 * returns ESP_OK if the bus is initialized by other device using the same bus configuration
 * returns ESP_FAIL if the bus is initialized by other device using different bus configuration
 * returns 1 if bus is not initialized (not used by any device)
*/
//-----------------------------------------------------
esp_err_t check_spi_host(exspi_device_handle_t *spidev)
{
    exspi_device_handle_t *extspidev;
    esp_err_t res = ESP_OK;
    int extdev = TOTAL_CS;

    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] != NULL) {
            extspidev = extspi_devices[extdev];
            if (extspidev->spihost == spidev->spihost) {
                // same spi bus host, check pins
                if (extspidev->buscfg->miso_io_num != spidev->buscfg->miso_io_num) res = ESP_FAIL;
                if (extspidev->buscfg->mosi_io_num != spidev->buscfg->mosi_io_num) res = ESP_FAIL;
                if (extspidev->buscfg->sclk_io_num != spidev->buscfg->sclk_io_num) res = ESP_FAIL;
                if (extspidev->buscfg->quadwp_io_num != spidev->buscfg->quadwp_io_num) res = ESP_FAIL;
                if (extspidev->buscfg->quadhd_io_num != spidev->buscfg->quadhd_io_num) res = ESP_FAIL;
                break;
            }
        }
    }
    if (extdev == TOTAL_CS) res = 1; // no device uses this bus, bus not initialized
    return res;
}

// Returns spi host used by sdcard driver, 0 if sdcard driver not used
//--------------------------
int spi_host_used_by_sdspi()
{
    int res = 0;

    for (int extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] != NULL) {
            if (extspi_devices[extdev]->dc == SDSPI_HOST_ID)  {
                res = extspi_devices[extdev]->spihost;
                break;
            }
        }
    }
    return res;
}

// Returns spi buses used by other than sdcard driver devices
//------------------------------
int spi_host_not_used_by_sdspi()
{
    int res = 0;

    for (int extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] != NULL) {
            if (extspi_devices[extdev]->dc != SDSPI_HOST_ID)  {
                if (extspi_devices[extdev]->spihost == HSPI_HOST) res |= 1;
                if (extspi_devices[extdev]->spihost == VSPI_HOST) res |= 2;
            }
        }
    }
    return res;
}

//--------------------------------------------------------
esp_err_t add_extspi_device(exspi_device_handle_t *spidev)
{
    int extdev;
    esp_err_t ret;

    // Find free ext spi device possition
    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] == NULL) break;
    }
    if (extdev == TOTAL_CS) {
        ESP_LOGE(TAG, "Error adding ext spi device, no free entries");
        return ESP_ERR_NOT_FOUND;
    }

    // Check if spi bus is used by other devices
    ret = check_spi_host(spidev);

    if (ret == ESP_FAIL) {
        // Other spi host uses different pins
        ESP_LOGE(TAG, "spi bus already used with different configuration (%d)", spidev->spihost);
        return ret;
    }

    if (ret == 1) {
        // SPI host bus not yet initialized
        ret=spi_bus_initialize(spidev->spihost, spidev->buscfg, spidev->dma_channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spi bus initialization failed with rc=0x%x", ret);
            return ret;
        }
        prev_bus = spidev->spihost;
        ESP_LOGD(TAG, "spi bus initialized (%d)", spidev->spihost);
    }
    else if (ret == ESP_OK) {
        ESP_LOGD(TAG, "spi bus already initialized (%d)", spidev->spihost);
    }

    // add the spi device
    ret=spi_bus_add_device(spidev->spihost, &spidev->devcfg, &spidev->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Adding spi device failed with rc=0x%x", ret);
        spi_bus_free(spidev->spihost);
        spidev->handle = NULL;
        return ret;
    }

    // Add the ext spi device to the list
    extspi_devices[extdev] = spidev;

    ESP_LOGV(TAG, "== Added ext spi device (%d)==", extdev);

    return ESP_OK;
}

//-----------------------------------------------------------
esp_err_t remove_extspi_device(exspi_device_handle_t *spidev)
{
    int extdev;
    esp_err_t ret;

    // Find this ext spi device possition in list
    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] == spidev) {
            break;
        }
    }
    if (extdev == TOTAL_CS) return ESP_OK;

    // Remove the device from spi bus
    ret = spi_bus_remove_device(spidev->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Removing spi device failed with rc=0x%x", ret);
        spidev->handle = NULL;
        return ret;
    }

    spidev->handle = NULL;
    extspi_devices[extdev] = NULL; // remove from list

    // Check for other devices using the same spi bus
    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if ((extspi_devices[extdev]) && (extspi_devices[extdev]->spihost == spidev->spihost)) break;
    }
    if (extdev == TOTAL_CS) {
        // No devices on this spi bus, free it
        spi_bus_free(spidev->spihost);
    }

    return ESP_OK;
}

/*
 * Set the spi clock according to pre-calculated register value.
 */
//--------------------------------------------------------------------
static inline void spi_set_clock(spi_dev_t *hw, spi_clock_reg_t reg) {
    hw->clock.val = reg.val;
}

//-----------------------------------------------------------
static esp_err_t reinit_spidev(exspi_device_handle_t *spidev)
{
    int extdev;
    esp_err_t ret;

    // Remove **all** devices on this bus
    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] != NULL) {
            if (extspi_devices[extdev]->spihost == spidev->spihost) {
                if (extspi_devices[extdev]->handle) {
                    ret = spi_bus_remove_device(extspi_devices[extdev]->handle);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Select: Error Removing device %d from bus %d", extdev, spidev->spihost);
                        return ret;
                    }
                    extspi_devices[extdev]->handle = NULL;
                }
            }
        }
    }

    // Free the spi bus
    spi_bus_free((spi_host_device_t) spidev->spihost);

    // ReInitialize the spi bus
    ret = spi_bus_initialize(spidev->spihost, spidev->buscfg, spidev->dma_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Select: Error ReInitializing spi bus %d", spidev->spihost);
        return ret;
    }
    // add **all** devices
    for (extdev=0; extdev<TOTAL_CS; extdev++) {
        if (extspi_devices[extdev] != NULL) {
            if (extspi_devices[extdev]->spihost == spidev->spihost) {
                ret = spi_bus_add_device(extspi_devices[extdev]->spihost, &extspi_devices[extdev]->devcfg, &extspi_devices[extdev]->handle);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Select: Error ReAdding device %d to bus %d", extdev, extspi_devices[extdev]->spihost);
                    extspi_devices[extdev]->handle = NULL;
                    return ret;
                }
            }
        }
    }
    return ESP_OK;
}

//-------------------------------------------------------------------
esp_err_t spi_device_select(exspi_device_handle_t *spidev, int force)
{
    if (spidev->handle == NULL) return ESP_ERR_INVALID_ARG;
    if ((!force) && (spidev->selected)) return ESP_OK; // already selected

    // Get the spi device handle pointer
    spi_device_handle_t handle = spidev->handle;
    // Get the spi bus host pointer
    spi_host_t *host=(spi_host_t*)handle->host;

    int curr_dev;

    // find spi device's slot (based on CS) on spi bus
    for (curr_dev=0; curr_dev<NO_CS; curr_dev++) {
        if (host->device[curr_dev] == handle) break;
    }
    if (curr_dev == NO_CS) {
        ESP_LOGE(TAG, "Select: Error, device not found");
        return ESP_ERR_INVALID_ARG;
    }

    if (spi_utils_mutex == NULL) {
        spi_utils_mutex = xSemaphoreCreateMutex();
        if (spi_utils_mutex == NULL) {
            ESP_LOGE(TAG, "Select: Error creating spi mutex");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGV(TAG, "Select: SPI mutex created");
    }
    if (spi_utils_mutex) {
        if (!(xSemaphoreTake(spi_utils_mutex, SPI_SEMAPHORE_WAIT))) {
            ESP_LOGE(TAG, "Select: Timeout waiting for mutex");
            return ESP_ERR_INVALID_STATE;
        }
    }

    if (prev_bus != spidev->spihost) {
        // ** SPI BUS was changed **
        ESP_LOGD(TAG, "Select: REINITIALIZE SPI BUS (%d)", spidev->spihost);
        if (spidev->dc == SDSPI_HOST_ID) reinit_sdspi_dev(spidev->spihost);  // sdspi host
        else {
            // ** Other hosts, ReInitialize SPI bus
            esp_err_t ret = reinit_spidev(spidev);
            if (ret != ESP_OK) {
                if (spi_utils_mutex) xSemaphoreGive(spi_utils_mutex);
                return ret;
            }

            // Restore this device handle & host
            if (spidev->handle == NULL) {
                if (spi_utils_mutex) xSemaphoreGive(spi_utils_mutex);
                return ESP_ERR_INVALID_ARG;
            }
            handle = spidev->handle;
            host=(spi_host_t*)handle->host;
            // find device's host slot
            for (curr_dev=0; curr_dev<NO_CS; curr_dev++) {
                if (host->device[curr_dev] == handle) break;
            }
            if (curr_dev == NO_CS) {
                ESP_LOGE(TAG, "Select: Error, device not found");
                if (spi_utils_mutex) xSemaphoreGive(spi_utils_mutex);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    if (spidev->dc != SDSPI_HOST_ID) {
        // === Reconfigure according to device settings, but only if the device was changed or reconfiguration forced. ===
        if ((force) || (curr_dev != host->prev_cs)) {
            const int apbclk=APB_CLK_FREQ;
            handle->clk_cfg.eff_clk = spi_cal_clock(apbclk, spidev->devcfg.clock_speed_hz, handle->cfg.duty_cycle_pos, (uint32_t*)&handle->clk_cfg.reg);
            int effclk=handle->clk_cfg.eff_clk;
            spi_set_clock(host->hw, handle->clk_cfg.reg);

            //Configure bit order
            host->hw->ctrl.rd_bit_order=(handle->cfg.flags & SPI_DEVICE_RXBIT_LSBFIRST)?1:0;
            host->hw->ctrl.wr_bit_order=(handle->cfg.flags & SPI_DEVICE_TXBIT_LSBFIRST)?1:0;

            //Configure polarity
            //SPI interface needs to be configured for a delay in some cases.
            int nodelay=0;
            if ((host->flags&SPICOMMON_BUSFLAG_NATIVE_PINS)!=0) {
                if (effclk >= apbclk/2) {
                    nodelay=1;
                }
            } else {
                uint32_t delay_limit = apbclk/4;
                if (effclk >= delay_limit) {
                    nodelay=1;
                }
            }

            if (handle->cfg.mode==0) {
                host->hw->pin.ck_idle_edge=0;
                host->hw->user.ck_out_edge=0;
                host->hw->ctrl2.miso_delay_mode=nodelay?0:2;
            } else if (handle->cfg.mode==1) {
                host->hw->pin.ck_idle_edge=0;
                host->hw->user.ck_out_edge=1;
                host->hw->ctrl2.miso_delay_mode=nodelay?0:1;
            } else if (handle->cfg.mode==2) {
                host->hw->pin.ck_idle_edge=1;
                host->hw->user.ck_out_edge=1;
                host->hw->ctrl2.miso_delay_mode=nodelay?0:1;
            } else if (handle->cfg.mode==3) {
                host->hw->pin.ck_idle_edge=1;
                host->hw->user.ck_out_edge=0;
                host->hw->ctrl2.miso_delay_mode=nodelay?0:2;
            }

            //Configure misc stuff
            host->hw->user.doutdin=(handle->cfg.flags & SPI_DEVICE_HALFDUPLEX)?0:1;
            host->hw->user.sio=(handle->cfg.flags & SPI_DEVICE_3WIRE)?1:0;

            host->hw->ctrl2.setup_time=handle->cfg.cs_ena_pretrans-1;
            host->hw->user.cs_setup=handle->cfg.cs_ena_pretrans?1:0;
            host->hw->ctrl2.hold_time=handle->cfg.cs_ena_posttrans-1;
            host->hw->user.cs_hold=(handle->cfg.cs_ena_posttrans)?1:0;

            host->cur_cs = curr_dev;

            //Configure CS pin
            if (handle->cfg.spics_io_num >= 0) curr_dev = -1;
            host->hw->pin.cs0_dis = (curr_dev==0) ? 0:1;
            host->hw->pin.cs1_dis = (curr_dev==1) ? 0:1;
            host->hw->pin.cs2_dis = (curr_dev==2) ? 0:1;
        }
        host->prev_cs = curr_dev;
    }

    // If the device uses external CS, activate it
    if ((handle->cfg.spics_io_num < 0) && (spidev->cs >= 0)) {
        gpio_set_level(spidev->cs, 0);
        ESP_LOGV(TAG, "Select: extCS:%d", spidev->cs);
    }

    spidev->selected = 1;

    return ESP_OK;
}

//----------------------------------------------------------
esp_err_t spi_device_deselect(exspi_device_handle_t *spidev)
{
    if (spidev->handle == NULL) return ESP_ERR_INVALID_ARG;
    if (spidev->selected == 0) return ESP_OK; // already deselected

    _wait_trans_finish(spidev);
    int currDev;
    spi_host_t *host=(spi_host_t*)spidev->handle->host;

    // find device's host slot
    for (currDev=0; currDev<NO_CS; currDev++) {
        if (host->device[currDev] == spidev->handle) break;
    }
    if (currDev == NO_CS) {
        if (spi_utils_mutex) xSemaphoreGive(spi_utils_mutex);
        ESP_LOGE(TAG, "DeSelect: Error, device not found");
        return ESP_ERR_INVALID_ARG;
    }

    // If the device uses external CS, deactivate it
    if ((spidev->handle->cfg.spics_io_num < 0) && (spidev->cs >= 0)) {
        gpio_set_level(spidev->cs, 1);
        ESP_LOGV(TAG, "DeSelect: extCS=%d ==", spidev->cs);
    }

    spidev->selected = 0;
    prev_bus = spidev->spihost;
    host->cur_cs = NO_CS;

    if (spi_utils_mutex) xSemaphoreGive(spi_utils_mutex);

    return ESP_OK;
}

// -----------------------------
// Direct (no DMA) data transfer
//----------------------------------------------------------------------------------------
esp_err_t spi_transfer_data_nodma(exspi_device_handle_t *spidev, spi_transaction_t *trans)
{
    if (!spidev->handle) return ESP_ERR_INVALID_ARG;

    // *** For now we can only handle 8-bit bytes transmission
    if (((trans->length % 8) != 0) || ((trans->rxlength % 8) != 0)) return ESP_ERR_INVALID_ARG;

    spi_device_handle_t handle = spidev->handle;
    spi_host_t *host=(spi_host_t*)spidev->handle->host;
    esp_err_t ret;
    uint8_t do_deselect = 0;
    const uint8_t *txbuffer = NULL;
    uint8_t *rxbuffer = NULL;

    // find device's host slot
    int dev_num;
    //spi_device_t *dev=NULL;
    for (dev_num=0; dev_num<NO_CS; dev_num++) {
        if (host->device[dev_num] == handle) {
            //dev=host->device[dev_num];
            break;
        }
    }
    if (dev_num==NO_CS) return ESP_ERR_INVALID_ARG;

    if (trans->flags & SPI_TRANS_USE_TXDATA) {
        // Send data from 'trans->tx_data'
        txbuffer=(uint8_t*)&trans->tx_data[0];
    } else {
        // Send data from 'trans->tx_buffer'
        txbuffer=(uint8_t*)trans->tx_buffer;
    }
    if (trans->flags & SPI_TRANS_USE_RXDATA) {
        // Receive data to 'trans->rx_data'
        rxbuffer=(uint8_t*)&trans->rx_data[0];
    } else {
        // Receive data to 'trans->rx_buffer'
        rxbuffer=(uint8_t*)trans->rx_buffer;
    }

    // ** Set transmit & receive length in bytes
    uint32_t txlen = trans->length / 8;
    uint32_t rxlen = trans->rxlength / 8;

    if (txbuffer == NULL) txlen = 0;
    if (rxbuffer == NULL) rxlen = 0;
    if ((rxlen == 0) && (txlen == 0)) {
        // ** NOTHING TO SEND or RECEIVE, return
        return ESP_ERR_INVALID_ARG;
    }

    // If using 'trans->tx_data' and/or 'trans->rx_data', maximum 4 bytes can be sent/received
    if ((txbuffer == &trans->tx_data[0]) && (txlen > 4)) return ESP_ERR_INVALID_ARG;
    if ((rxbuffer == &trans->rx_data[0]) && (rxlen > 4)) return ESP_ERR_INVALID_ARG;

    // --- Wait for SPI bus ready ---
    while (host->hw->cmd.usr);

    // --- If the device was not selected, select it ---
    if (spidev->selected == 0) {
        ret = spi_device_select(spidev, 0);
        if (ret) return ret;
        do_deselect = 1;     // We will deselect the device after the operation !
    }

    // --- Call pre-transmission callback, if any ---
    if (handle->cfg.pre_cb) handle->cfg.pre_cb(trans);

    // Test if operating in full duplex mode
    uint8_t duplex = 1;
    if (handle->cfg.flags & SPI_DEVICE_HALFDUPLEX) duplex = 0; // Half duplex mode !

    uint32_t bits, rdbits;
    uint32_t wd;
    uint8_t bc, rdidx;
    uint32_t rdcount = rxlen;	// Total number of bytes to read
    uint32_t count = 0;			// number of bytes transmitted
    uint32_t rd_read = 0;		// Number of bytes read so far

    host->hw->user.usr_mosi_highpart = 0; // use the whole spi buffer

    // --- Check if address and/or command phase will be used ---
    int cmdlen;
    if ( trans->flags & SPI_TRANS_VARIABLE_CMD ) {
        cmdlen = ((spi_transaction_ext_t*)trans)->command_bits;
    } else {
        cmdlen = handle->cfg.command_bits;
    }
    int addrlen;
    if ( trans->flags & SPI_TRANS_VARIABLE_ADDR ) {
        addrlen = ((spi_transaction_ext_t*)trans)->address_bits;
    } else {
        addrlen = handle->cfg.address_bits;
    }
    host->hw->user1.usr_addr_bitlen=addrlen-1;
    host->hw->user2.usr_command_bitlen=cmdlen-1;
    host->hw->user.usr_addr=addrlen?1:0;
    host->hw->user.usr_command=cmdlen?1:0;

    ESP_LOGV(TAG, "Starting no-dma transaction on %d", dev_num);
    // Check if we have to transmit some data
    if (txlen > 0) {
        host->hw->user.usr_mosi = 1;
        uint8_t idx;
        bits = 0;				// remaining bits to send
        idx = 0;				// index to spi hw data_buf (16 32-bit words, 64 bytes, 512 bits)

        // ** Transmit 'txlen' bytes
        while (count < txlen) {
            wd = 0;
            for (bc=0;bc<32;bc+=8) {
                wd |= (uint32_t)txbuffer[count] << bc;
                count++;                    // Increment sent data count
                bits += 8;                  // Increment bits count
                if (count == txlen) break;  // If all transmit data pushed to hw spi buffer break from the loop
            }
            host->hw->data_buf[idx] = wd;
            idx++;
            if (idx == 16) {
                // hw SPI buffer full (all 64 bytes filled, START THE TRANSSACTION
                host->hw->mosi_dlen.usr_mosi_dbitlen=bits-1;            // Set mosi dbitlen

                if ((duplex) && (rdcount > 0)) {
                    // In full duplex mode we are receiving while sending !
                    host->hw->miso_dlen.usr_miso_dbitlen = bits-1;      // Set miso dbitlen
                    host->hw->user.usr_miso = 1;
                }
                else {
                    host->hw->miso_dlen.usr_miso_dbitlen = 0;           // In half duplex mode nothing will be received
                    host->hw->user.usr_miso = 0;
                }

                // ** Start the transaction ***
                host->hw->cmd.usr=1;
                // Wait the transaction to finish
                while (host->hw->cmd.usr);

                if ((duplex) && (rdcount > 0)) {
                    // *** in full duplex mode transfer received data to input buffer ***
                    rdidx = 0;
                    while (bits > 0) {
                        wd = host->hw->data_buf[rdidx];
                        rdidx++;
                        for (bc=0;bc<32;bc+=8) { // get max 4 bytes
                            rxbuffer[rd_read++] = (uint8_t)((wd >> bc) & 0xFF);
                            rdcount--;
                            bits -= 8;
                            if (rdcount == 0) {
                                bits = 0;
                                break; // Finished reading data
                            }
                        }
                    }
                }
                bits = 0;   // nothing in hw spi buffer yet
                idx = 0;    // start from the beginning of the hw spi buffer
            }
        }
        // *** All transmit data are sent or pushed to hw spi buffer
        // bits > 0  IF THERE ARE SOME DATA STILL WAITING IN THE HW SPI TRANSMIT BUFFER
        if (bits > 0) {
            // ** WE HAVE SOME DATA IN THE HW SPI TRANSMIT BUFFER
            host->hw->mosi_dlen.usr_mosi_dbitlen = bits-1;          // Set mosi dbitlen

            if ((duplex) && (rdcount > 0)) {
                // In full duplex mode we are receiving while sending !
                host->hw->miso_dlen.usr_miso_dbitlen = bits-1;      // Set miso dbitlen
                host->hw->user.usr_miso = 1;
            }
            else {
                host->hw->miso_dlen.usr_miso_dbitlen = 0;           // In half duplex mode nothing will be received
                host->hw->user.usr_miso = 0;
            }

            // ** Start the transaction ***
            host->hw->cmd.usr=1;
            // Wait the transaction to finish
            while (host->hw->cmd.usr);

            if ((duplex) && (rdcount > 0)) {
                // *** in full duplex mode transfer received data to input buffer ***
                rdidx = 0;
                while (bits > 0) {
                    wd = host->hw->data_buf[rdidx];
                    rdidx++;
                    for (bc=0;bc<32;bc+=8) { // get max 4 bytes
                        rxbuffer[rd_read++] = (uint8_t)((wd >> bc) & 0xFF);
                        rdcount--;
                        bits -= 8;
                        if (bits == 0) break;
                        if (rdcount == 0) {
                            bits = 0;
                            break; // Finished reading data
                        }
                    }
                }
            }
        }
        //if (duplex) rdcount = 0;  // In duplex mode receive only as many bytes as was transmitted
    }

    // ------------------------------------------------------------------------
    // *** If rdcount = 0 we have nothing to receive and we exit the function
    //     This is true if no data receive was requested,
    //     or all the data was received in Full duplex mode during the transmission
    // ------------------------------------------------------------------------
    if (rdcount > 0) {
        // ----------------------------------------------------------------------------------------------------------------
        // *** rdcount > 0, we have to receive some data
        //     This is true if we operate in Half duplex mode when receiving after transmission is done,
        //     or not all data was received in Full duplex mode during the transmission (trans->rxlength > trans->txlength)
        // ----------------------------------------------------------------------------------------------------------------
        host->hw->user.usr_mosi = 0;  // do not send
        host->hw->user.usr_miso = 1;  // do receive
        while (rdcount > 0) {
            if (rdcount <= 64) rdbits = rdcount * 8;
            else rdbits = 64 * 8;

            // Load receive buffer
            host->hw->mosi_dlen.usr_mosi_dbitlen=0;
            host->hw->miso_dlen.usr_miso_dbitlen=rdbits-1;

            // ** Start the transaction ***
            host->hw->cmd.usr=1;
            // Wait the transaction to finish
            while (host->hw->cmd.usr);

            // *** transfer received data to input buffer ***
            rdidx = 0;
            while (rdbits > 0) {
                wd = host->hw->data_buf[rdidx];
                rdidx++;
                for (bc=0;bc<32;bc+=8) {
                    rxbuffer[rd_read++] = (uint8_t)((wd >> bc) & 0xFF);
                    rdcount--;
                    rdbits -= 8;
                    if (rdcount == 0) {
                        rdbits = 0;
                        break;
                    }
                }
            }
        }
    }

    // --- Call post-transmission callback, if any ---
    if (handle->cfg.post_cb) handle->cfg.post_cb(trans);

    if (do_deselect) {
        // Spi device was selected in this function, deselect it now
        ret = spi_device_deselect(spidev);
        if (ret) return ret;
    }

    return ESP_OK;
}

//---------------------------------------------------
uint32_t spi_get_speed(exspi_device_handle_t *spidev)
{
    if (spidev->handle == NULL) return 0;

    spi_device_handle_t handle = spidev->handle;
    return handle->clk_cfg.eff_clk;
}

//-------------------------------------------------------------------
uint32_t spi_set_speed(exspi_device_handle_t *spidev, uint32_t speed)
{
    if (spidev->handle == NULL) return 0;

    spi_device_handle_t handle = spidev->handle;
    if (spidev->curr_clock != speed) {
        spi_device_deselect(spidev);

        spi_device_handle_t handle = spidev->handle;
        spi_host_t *host=(spi_host_t*)handle->host;

        int apbclk=APB_CLK_FREQ;
        handle->clk_cfg.eff_clk = spi_cal_clock(apbclk, speed, handle->cfg.duty_cycle_pos, (uint32_t*)&handle->clk_cfg.reg);
        spi_set_clock(host->hw, handle->clk_cfg.reg);
        spidev->devcfg.clock_speed_hz = handle->clk_cfg.eff_clk;
        spidev->curr_clock = handle->clk_cfg.eff_clk;

        // select the device, force clock reconfigure
        spi_device_select(spidev, 1);
        spi_device_deselect(spidev);
    }

    return handle->clk_cfg.eff_clk;
}

//---------------------------------------------------
bool spi_uses_native_pins(spi_device_handle_t handle)
{
    return ((handle->host->flags & SPICOMMON_BUSFLAG_NATIVE_PINS) != 0);
}

// Start hardware SPI data transfer, spi device must be selected
// No address or command phase is executed
//----------------------------------------------------------------------------------------
void IRAM_ATTR _spi_transfer_start(exspi_device_handle_t *spi_dev, int wrbits, int rdbits)
{
    while (spi_dev->handle->host->hw->cmd.usr); // Wait for SPI bus ready

    spi_dev->handle->host->hw->user1.usr_addr_bitlen = 0;
    spi_dev->handle->host->hw->user2.usr_command_bitlen = 0;
    spi_dev->handle->host->hw->user.usr_addr = 0;
    spi_dev->handle->host->hw->user.usr_command = 0;
    spi_dev->handle->host->hw->user.usr_mosi_highpart = 0;

    if (wrbits) {
        spi_dev->handle->host->hw->mosi_dlen.usr_mosi_dbitlen = wrbits-1;
        spi_dev->handle->host->hw->user.usr_mosi = 1;
    }
    else {
        spi_dev->handle->host->hw->mosi_dlen.usr_mosi_dbitlen = 0;
        spi_dev->handle->host->hw->user.usr_mosi = 0;
    }
    if (rdbits) {
        spi_dev->handle->host->hw->miso_dlen.usr_miso_dbitlen = rdbits-1;
        spi_dev->handle->host->hw->user.usr_miso = 1;
    }
    else {
        spi_dev->handle->host->hw->miso_dlen.usr_miso_dbitlen = 0;
        spi_dev->handle->host->hw->user.usr_miso = 0;
    }
    // Start transfer
    spi_dev->handle->host->hw->cmd.usr = 1;
}

//-----------------------------------------------------------------------------------
void IRAM_ATTR _dma_send(exspi_device_handle_t *spi_dev, uint8_t *data, uint32_t size)
{
    //Fill DMA descriptors

    spicommon_dmaworkaround_transfer_active(spi_dev->handle->host->dma_chan); //mark channel as active
    spicommon_setup_dma_desc_links(spi_dev->handle->host->dmadesc_tx, size, data, false);
    spi_dev->handle->host->hw->user.usr_addr = 0;
    spi_dev->handle->host->hw->user.usr_command = 0;
    spi_dev->handle->host->hw->user.usr_mosi_highpart=0;
    spi_dev->handle->host->hw->dma_out_link.addr=(int)(&spi_dev->handle->host->dmadesc_tx[0]) & 0xFFFFF;
    spi_dev->handle->host->hw->dma_out_link.start=1;
    spi_dev->handle->host->hw->user.usr_mosi_highpart=0;

    spi_dev->handle->host->hw->mosi_dlen.usr_mosi_dbitlen = (size * 8) - 1;

    _dma_sending = 1;
    // Start transfer
    spi_dev->handle->host->hw->cmd.usr = 1;
}

//--------------------------------------------------------------------
esp_err_t IRAM_ATTR _wait_trans_finish(exspi_device_handle_t *spi_dev)
{
    while (spi_dev->handle->host->hw->cmd.usr); // Wait for SPI bus ready
    if (_dma_sending) {
        //Tell common code DMA workaround that our DMA channel is idle. If needed, the code will do a DMA reset.
        if (spi_dev->handle->host->dma_chan) spicommon_dmaworkaround_idle(spi_dev->handle->host->dma_chan);

        // Reset DMA
        spi_dev->handle->host->hw->dma_conf.val |= SPI_OUT_RST|SPI_IN_RST|SPI_AHBM_RST|SPI_AHBM_FIFO_RST;
        spi_dev->handle->host->hw->dma_out_link.start=0;
        spi_dev->handle->host->hw->dma_in_link.start=0;
        spi_dev->handle->host->hw->dma_conf.val &= ~(SPI_OUT_RST|SPI_IN_RST|SPI_AHBM_RST|SPI_AHBM_FIFO_RST);
        spi_dev->handle->host->hw->dma_conf.out_data_burst_en=1;
        _dma_sending = 0;
    }
    return ESP_OK;
}


