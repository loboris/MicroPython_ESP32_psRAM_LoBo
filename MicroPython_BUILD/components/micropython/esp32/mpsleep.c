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

#include <stdint.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/runtime.h"
#include "py/mphal.h"

#include "sdkconfig.h"
#include "rom/rtc.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "mpsleep.h"

STATIC mpsleep_reset_cause_t mpsleep_reset_cause = MPSLEEP_UNKNOWN_RESET;
STATIC mpsleep_wake_reason_t mpsleep_wake_reason = MPSLEEP_NONE_WAKE;

//-------------------------
void mpsleep_init0 (void) {
    // check the reset cause
	RESET_REASON reason = rtc_get_reset_reason(0);
    switch (reason) {
        case TG0WDT_SYS_RESET:
            mpsleep_reset_cause = MPSLEEP_WDT_RESET;
            break;
        case DEEPSLEEP_RESET:
            mpsleep_reset_cause = MPSLEEP_DEEPSLEEP_RESET;
            break;
        case RTCWDT_BROWN_OUT_RESET:
            mpsleep_reset_cause = MPSLEEP_BROWN_OUT_RESET;
            break;
        case TG1WDT_SYS_RESET:
            mpsleep_reset_cause = MPSLEEP_HARD_RESET;
            break;
        case SW_CPU_RESET:         // machine.reset()
            mpsleep_reset_cause = MPSLEEP_SOFT_CPU_RESET;
            break;
        case POWERON_RESET:
            mpsleep_reset_cause = MPSLEEP_PWRON_RESET;
            break;
        case RTCWDT_RTC_RESET:
            mpsleep_reset_cause = MPSLEEP_RTCWDT_RESET;
            break;
        default:
            mpsleep_reset_cause = MPSLEEP_UNKNOWN_RESET;
        	ESP_LOGD("[RESET]", "Reset reason: %d", reason);
            break;
    }

    // check the wakeup reason
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0:
            mpsleep_wake_reason = MPSLEEP_GPIO0_WAKE;
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            mpsleep_wake_reason = MPSLEEP_GPIO1_WAKE;
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            mpsleep_wake_reason = MPSLEEP_TOUCH_WAKE;
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            mpsleep_wake_reason = MPSLEEP_RTC_WAKE;
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            mpsleep_wake_reason = MPSLEEP_NONE_WAKE;
            break;
    }
}

//-------------------------------------
void mpsleep_signal_soft_reset (void) {
    mpsleep_reset_cause = MPSLEEP_SOFT_RESET;
}

//----------------------------------------------------
mpsleep_reset_cause_t mpsleep_get_reset_cause (void) {
    return mpsleep_reset_cause;
}

//----------------------------------------------------
mpsleep_wake_reason_t mpsleep_get_wake_reason (void) {
    return mpsleep_wake_reason;
}

//------------------------------------------------
void mpsleep_get_reset_desc (char *reset_reason) {
    switch (mpsleep_reset_cause) {
        case MPSLEEP_PWRON_RESET:
            sprintf(reset_reason, "Power on reset");
            break;
        case MPSLEEP_RTCWDT_RESET:
            sprintf(reset_reason, "RTC WDT reset");
            break;
        case MPSLEEP_HARD_RESET:
            sprintf(reset_reason, "Hard reset");
            break;
        case MPSLEEP_WDT_RESET:
            sprintf(reset_reason, "WDT reset");
            break;
        case MPSLEEP_DEEPSLEEP_RESET:
            sprintf(reset_reason, "Deepsleep wake-up");
            break;
        case MPSLEEP_SOFT_CPU_RESET:
            sprintf(reset_reason, "Soft CPU reset");
            break;
        case MPSLEEP_SOFT_RESET:
            sprintf(reset_reason, "Soft reset");
            break;
        case MPSLEEP_BROWN_OUT_RESET:
            sprintf(reset_reason, "Brownout reset");
            break;
        default:
            sprintf(reset_reason, "Unknown reset reason");
            break;
    }
}

//----------------------------------------------
void mpsleep_get_wake_desc (char *wake_reason) {
    switch (mpsleep_wake_reason) {
        case MPSLEEP_NONE_WAKE:
            sprintf(wake_reason, "No wake-up reason");
            break;
        case MPSLEEP_GPIO0_WAKE:
            sprintf(wake_reason, "EXT_0 wake-up");
            break;
        case MPSLEEP_GPIO1_WAKE:
            sprintf(wake_reason, "EXT_1 wake-up");
            break;
        case MPSLEEP_TOUCH_WAKE:
            sprintf(wake_reason, "Touchpad wake-up");
            break;
        case MPSLEEP_RTC_WAKE:
            sprintf(wake_reason, "RTC wake-up");
            break;
        case MPSLEEP_ULP_WAKE:
            sprintf(wake_reason, "ULP wake-up");
            break;
        default:
            sprintf(wake_reason, "Unknown wake reason");
            break;
    }
}
