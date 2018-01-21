/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 *
 * Copyright (c) 2017 Boris Lovosevic (External SPIRAM support)
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

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_task.h"
#include "soc/cpu.h"
#include "esp_log.h"
#include "driver/periph_ctrl.h"
#include "esp_ota_ops.h"

#include "py/stackctrl.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "extmod/vfs_native.h"
#include "lib/mp-readline/readline.h"
#include "lib/utils/pyexec.h"
#include "uart.h"
#include "modmachine.h"
#include "mpthreadport.h"
#include "mpsleep.h"
#include "machine_rtc.h"
#ifdef CONFIG_MICROPY_USE_FTPSERVER
#include "libs/ftp.h"
#endif
#ifdef CONFIG_MICROPY_USE_MQTT
#include "mqtt.h"
#endif

#include "sdkconfig.h"


// =========================================
// MicroPython runs as a task under FreeRTOS
// =========================================

#define NVS_NAMESPACE       "MPY_NVM"
#define MP_TASK_PRIORITY	CONFIG_MICROPY_TASK_PRIORITY
#define MP_TASK_STACK_SIZE	(CONFIG_MICROPY_STACK_SIZE * 1024)
#define MP_TASK_HEAP_SIZE	(CONFIG_MICROPY_HEAP_SIZE * 1024)
#define MP_TASK_STACK_LEN	(MP_TASK_STACK_SIZE / sizeof(StackType_t))

STATIC TaskHandle_t MainTaskHandle = NULL;

STATIC StaticTask_t DRAM_ATTR mp_task_tcb;
STATIC StackType_t DRAM_ATTR mp_task_stack[MP_TASK_STACK_LEN] __attribute__((aligned (8)));

STATIC uint8_t *mp_task_heap;

int MainTaskCore = 0;

//===============================
void mp_task(void *pvParameter) {
    volatile uint32_t sp = (uint32_t)get_sp();

	#ifdef CONFIG_MICROPY_USE_TASK_WDT
    // Enable watchdog for MicroPython main task
    esp_task_wdt_init(CONFIG_TASK_WDT_TIMEOUT_S, false);
    esp_task_wdt_add(MainTaskHandle);
    esp_task_wdt_reset();
	#endif

    uart_init();

    // Check and open NVS name space
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &mpy_nvs_handle) != ESP_OK) {
    	mpy_nvs_handle = 0;
        printf("Error while opening MicroPython NVS name space\n");
    }

    // Get and print reset & wakeup reasons
    mpsleep_init0();

    if (mpsleep_get_reset_cause() != MPSLEEP_DEEPSLEEP_RESET) rtc_init0();

    mp_thread_preinit(&mp_task_stack[0], MP_TASK_STACK_LEN);

soft_reset:
	// Thread init
	mp_thread_init();

    // Initialize the stack pointer for the main thread
    mp_stack_set_top((void *)sp);
    mp_stack_set_limit(MP_TASK_STACK_SIZE - 1024);

    // initialize the mp heap
    gc_init(mp_task_heap, mp_task_heap + MP_TASK_HEAP_SIZE);

    mp_init();
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    mp_obj_list_init(mp_sys_argv, 0);

    readline_init0();

	// Initialize peripherals
    machine_pins_init();

    // === Mount internal flash file system ===
    int res = mount_vfs(VFS_NATIVE_TYPE_SPIFLASH, VFS_NATIVE_INTERNAL_MP);
    if (res == 0) {
    	// run boot-up script 'boot.py'
        pyexec_file("boot.py");
        if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
        	// Check if 'main.py' exists and run it
        	FILE *fd;
        	fd = fopen(VFS_NATIVE_MOUNT_POINT"/main.py", "rb");
            if (fd) {
            	fclose(fd);
            	pyexec_file("main.py");
            }
        }
    }
    else printf("Error mounting Flash file system\n");

    // === Print some info ===
    char sbuff[24] = { 0 };
    gc_info_t info;
    gc_info(&info);
    // set gc.threshold to 87.5% of usable heap
	MP_STATE_MEM(gc_alloc_threshold) = ((info.total / 8) * 7) / MICROPY_BYTES_PER_GC_BLOCK;

	#if CONFIG_FREERTOS_UNICORE
    	printf("\nFreeRTOS running only on FIRST CORE.\n");
	#else
		#if CONFIG_SPIRAM_SUPPORT
    		printf("\nFreeRTOS running on BOTH CORES, MicroPython task running on both cores.\n");
		#else
    		printf("\nFreeRTOS running on BOTH CORES, MicroPython task started on App Core.\n");
		#endif
	#endif

    // Print partition info
	const esp_partition_t *running_partition = esp_ota_get_running_partition();
	if (running_partition != NULL) {
		if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) sprintf(sbuff, "Factory ");
		else if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) sprintf(sbuff, "OTA_0 ");
		else if (running_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) sprintf(sbuff, "OTA_1 ");
		else sbuff[0] = '\0';
		printf("Running from %s%spartition starting at 0x%X, [%s].\n",
				((running_partition->encrypted) ? "encrypted " : ""), sbuff, running_partition->address, running_partition->label);
	}

	mpsleep_get_reset_desc(sbuff);
	if (mpsleep_get_wake_reason() != MPSLEEP_NONE_WAKE) printf(" ");
	printf("\n Reset reason: %s\n", sbuff);
	if (mpsleep_get_wake_reason() != MPSLEEP_NONE_WAKE) {
		mpsleep_get_wake_desc(sbuff);
		printf("Wakeup source: %s\n", sbuff);
	}

	printf("    uPY stack: %d bytes\n", MP_TASK_STACK_LEN-1024);

	#if CONFIG_SPIRAM_SUPPORT
		// ## USING SPI RAM FOR HEAP ##
		#if CONFIG_SPIRAM_USE_CAPS_ALLOC
		printf("     uPY heap: %u/%u/%u bytes (in SPIRAM using heap_caps_malloc)\n\n", info.total, info.used, info.free);
		#elif SPIRAM_USE_MEMMAP
		printf("     uPY heap: %u/%u/%u bytes (in SPIRAM using MEMMAP)\n\n", info.total, info.used, info.free);
		#else
		printf("     uPY heap: %u/%u/%u bytes (in SPIRAM using malloc)\n\n", info.total, info.used, info.free);
		#endif
	#else
		// ## USING DRAM FOR HEAP ##
		printf("     uPY heap: %u/%u/%u bytes\n\n", info.total, info.used, info.free);
	#endif

	// === Main loop ==================================
    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        }
        else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }
	// ================================================

    prepareSleepReset(0, "ESP32: soft reboot\r\n");

    goto soft_reset;
}


//============================
void micropython_entry(void) {
    nvs_flash_init();

    // === Set esp32 log levels while running MicroPython ===
	esp_log_level_set("*", CONFIG_MICRO_PY_LOG_LEVEL);
	esp_log_level_set("wifi", 1);
	esp_log_level_set("rmt", 1);
	#ifdef CONFIG_MICROPY_USE_OTA
	esp_log_level_set("OTA_UPDATE", 4);
	#endif

	#ifdef CONFIG_MICROPY_USE_MQTT
	esp_log_level_set(MQTT_TAG, CONFIG_MQTT_LOG_LEVEL);
	#endif
	#ifdef CONFIG_MICROPY_USE_FTPSERVER
	esp_log_level_set(FTP_TAG, CONFIG_FTPSERVER_LOG_LEVEL);
	#endif

    // ==== Allocate heap memory ====
    #if CONFIG_SPIRAM_SUPPORT
		// ## USING SPI RAM FOR HEAP ##
		#if CONFIG_SPIRAM_USE_CAPS_ALLOC
		mp_task_heap = heap_caps_malloc(MP_TASK_HEAP_SIZE, MALLOC_CAP_SPIRAM);
		#elif SPIRAM_USE_MEMMAP
		mp_task_heap = (uint8_t *)0x3f800000;
		#else
		mp_task_heap = malloc(MP_TASK_HEAP_SIZE);
		#endif
    #else
		// ## USING DRAM FOR HEAP ##
		mp_task_heap = malloc(MP_TASK_HEAP_SIZE);
    #endif

    if (mp_task_heap == NULL) {
        printf("Error allocating heap, Halted.\n");
        return;
    }

    // Workaround for possible bug in i2c driver !?
    periph_module_disable(PERIPH_I2C0_MODULE);
    periph_module_enable(PERIPH_I2C0_MODULE);

    // ==== Create and start main MicroPython task ====
	#if CONFIG_FREERTOS_UNICORE
		MainTaskCore = 0;
		MainTaskHandle = xTaskCreateStaticPinnedToCore(&mp_task, "mp_task", MP_TASK_STACK_LEN, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb, 0);
	#else
		MainTaskCore = 1;
		#if CONFIG_MICROPY_USE_BOTH_CORES
			MainTaskHandle = xTaskCreateStatic(&mp_task, "mp_task", MP_TASK_STACK_LEN, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb);
		#else
			MainTaskHandle = xTaskCreateStaticPinnedToCore(&mp_task, "mp_task", MP_TASK_STACK_LEN, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb, 1);
		#endif
	#endif
}

//-----------------------------
void nlr_jump_fail(void *val) {
    printf("RESET: NLR jump failed, val=%p\n", val);
    prepareSleepReset(1, NULL);
    esp_restart();
}

