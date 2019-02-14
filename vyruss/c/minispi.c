// based on esp32-hal-spi.c
// https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-spi.c
//
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <soc/dport_reg.h>
#include <soc/dport_access.h>
#include <soc/spi_reg.h>
#include <soc/spi_struct.h>
#include <soc/io_mux_reg.h>
#include <driver/gpio.h>

#define HSPI_PIN_CLK  GPIO_NUM_14
#define HSPI_PIN_MOSI GPIO_NUM_13

typedef union {
    uint32_t regValue;
    struct {
        unsigned regL :6;
        unsigned regH :6;
        unsigned regN :6;
        unsigned regPre :13;
        unsigned regEQU :1;
    };
} spiClk_t;

spi_dev_t* dev;

#define ClkRegToFreq(reg) (CPU_CLK_FREQ / (((reg)->regPre + 1) * ((reg)->regN + 1)))
#define ESP_REG(addr) *((volatile uint32_t *)(addr))

uint32_t spiFrequencyToClockDiv(uint32_t freq) {

    if(freq >= CPU_CLK_FREQ) {
        return SPI_CLK_EQU_SYSCLK;
    }

    const spiClk_t minFreqReg = { 0x7FFFF000 };
    uint32_t minFreq = ClkRegToFreq((spiClk_t*) &minFreqReg);
    if(freq < minFreq) {
        return minFreqReg.regValue;
    }

    uint8_t calN = 1;
    spiClk_t bestReg = { 0 };
    int32_t bestFreq = 0;

    while(calN <= 0x3F) {
        spiClk_t reg = { 0 };
        int32_t calFreq;
        int32_t calPre;
        int8_t calPreVari = -2;

        reg.regN = calN;

        while(calPreVari++ <= 1) {
            calPre = (((CPU_CLK_FREQ / (reg.regN + 1)) / freq) - 1) + calPreVari;
            if(calPre > 0x1FFF) {
                reg.regPre = 0x1FFF;
            } else if(calPre <= 0) {
                reg.regPre = 0;
            } else {
                reg.regPre = calPre;
            }
            reg.regL = ((reg.regN + 1) / 2);
            calFreq = ClkRegToFreq(&reg);
            if(calFreq == (int32_t) freq) {
                memcpy(&bestReg, &reg, sizeof(bestReg));
                break;
            } else if(calFreq < (int32_t) freq) {
                if(abs(freq - calFreq) < abs(freq - bestFreq)) {
                    bestFreq = calFreq;
                    memcpy(&bestReg, &reg, sizeof(bestReg));
                }
            }
        }
        if(calFreq == (int32_t) freq) {
            break;
        }
        calN++;
    }
    return bestReg.regValue;
}

void spiStopBus() {
    dev->slave.trans_done = 0;
    dev->slave.slave_mode = 0;
    dev->pin.val = 0;
    dev->user.val = 0;
    dev->user1.val = 0;
    dev->ctrl.val = 0;
    dev->ctrl1.val = 0;
    dev->ctrl2.val = 0;
    dev->clock.val = 0;
}

void spiStartBus(uint32_t freq) {
    dev = (volatile spi_dev_t *)(DR_REG_SPI2_BASE);
    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_SPI_CLK_EN);
    DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_SPI_RST);

    spiStopBus();
    // mode SPI_MODE0
    dev->pin.ck_idle_edge = 0;
    dev->user.ck_out_edge = 0;
    // bitorder SPI_MSBFIRST
    dev->ctrl.wr_bit_order = 0;
    dev->ctrl.rd_bit_order = 0;
    // clock speed
    dev->clock.val = spiFrequencyToClockDiv(freq);

    dev->user.usr_mosi = 1;
    dev->user.usr_miso = 1;
    dev->user.doutdin = 1;

    int i;
    for(i=0; i<16; i++) {
        dev->data_buf[i] = 0x00000000;
    }

    uint32_t pinFunction = 0;
    pinFunction |= ((uint32_t)2 << FUN_DRV_S);//what are the drivers?
    pinFunction |= FUN_IE;//input enable but required for output as well? 
    pinFunction |= ((uint32_t)2 << MCU_SEL_S);
    
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    GPIO.enable_w1ts = ((uint32_t)1 << 14);
    ESP_REG(DR_REG_IO_MUX_BASE + 0x30) = pinFunction;
    gpio_matrix_out(GPIO_NUM_14, HSPICLK_OUT_IDX, false, false);

    gpio_set_direction(GPIO_NUM_13, GPIO_MODE_OUTPUT);
    GPIO.enable_w1ts = ((uint32_t)1 << 13);
    ESP_REG(DR_REG_IO_MUX_BASE + 0x38) = pinFunction;
    gpio_matrix_out(GPIO_NUM_13, HSPID_IN_IDX, false, false);

//    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_INPUT);
}

void spiWriteNL(const void * data_in, size_t len){
    size_t longs = len >> 2;
    if(len & 3){
        longs++;
    }
    uint32_t * data = (uint32_t*)data_in;
    size_t c_len = 0, c_longs = 0;

    while(len){
        c_len = (len>64)?64:len;
        c_longs = (longs > 16)?16:longs;

        dev->mosi_dlen.usr_mosi_dbitlen = (c_len*8)-1;
        dev->miso_dlen.usr_miso_dbitlen = 0;
        for (int i=0; i<c_longs; i++) {
            dev->data_buf[i] = data[i];
        }
        dev->cmd.usr = 1;
        while(dev->cmd.usr);

        data += c_longs;
        longs -= c_longs;
        len -= c_len;
    }
}
