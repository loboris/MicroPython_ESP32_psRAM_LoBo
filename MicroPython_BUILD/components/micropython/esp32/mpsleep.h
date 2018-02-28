/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Author: LoBo (loboris@gmail.com / https://github.com/loboris)
 * Modification date: 10/2017
 * Code based on MicroPython port by Pycom Limited
 */

#ifndef MPSLEEP_H_
#define MPSLEEP_H_

typedef enum {
    MPSLEEP_PWRON_RESET = 0,
    MPSLEEP_HARD_RESET,
    MPSLEEP_WDT_RESET,
    MPSLEEP_DEEPSLEEP_RESET,
    MPSLEEP_SOFT_RESET,
    MPSLEEP_BROWN_OUT_RESET,
    MPSLEEP_SOFT_CPU_RESET,
    MPSLEEP_RTCWDT_RESET,
    MPSLEEP_UNKNOWN_RESET,
} mpsleep_reset_cause_t;

typedef enum {
    MPSLEEP_NONE_WAKE = 0,
    MPSLEEP_GPIO0_WAKE,
    MPSLEEP_GPIO1_WAKE,
    MPSLEEP_TOUCH_WAKE,
    MPSLEEP_RTC_WAKE,
    MPSLEEP_ULP_WAKE
} mpsleep_wake_reason_t;


void mpsleep_init0 (void);
void mpsleep_signal_soft_reset (void);
mpsleep_reset_cause_t mpsleep_get_reset_cause (void);
mpsleep_wake_reason_t mpsleep_get_wake_reason (void);
void mpsleep_get_reset_desc (char *reset_reason);
void mpsleep_get_wake_desc (char *wake_reason);

#endif /* MPSLEEP_H_ */
