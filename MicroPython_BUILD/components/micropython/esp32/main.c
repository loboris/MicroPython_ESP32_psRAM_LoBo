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

#include <stdio.h>
#include <string.h>
#include <time.h>

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
#include "esp_pm.h"

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

#include "driver/uart.h"
#include "rom/uart.h"
#include "sdkconfig.h"


// =========================================
// MicroPython runs as a task under FreeRTOS
// =========================================

#define NVS_NAMESPACE       "MPY_NVM"
#define MP_TASK_PRIORITY	CONFIG_MICROPY_TASK_PRIORITY
#define MP_TASK_STACK_LEN	3072

static StaticTask_t DRAM_ATTR mp_task_tcb;
static StackType_t *mp_task_stack;
static uint8_t *mp_task_heap;
static int mp_task_stack_len = 4092;

int MainTaskCore = 0;


//===============================
void mp_task(void *pvParameter) {
    volatile uint32_t sp = (uint32_t)get_sp();

    uart_config_t uartcfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
		.use_ref_tick = true
    };
    uart_param_config(UART_NUM_0, &uartcfg);
   	uart_set_baudrate(UART_NUM_0, CONFIG_CONSOLE_UART_BAUDRATE);

   	/*
    // ---- esp-idf PM bug! ----------------------------------------------------------------------------------------
	#if defined(CONFIG_PM_ENABLE) && !defined(CONFIG_PM_DFS_INIT_AUTO) && defined(CONFIG_ESP32_DEFAULT_CPU_FREQ_240)
    esp_pm_config_esp32_t pm_config;
	pm_config.max_cpu_freq = RTC_CPU_FREQ_160M;
   	pm_config.min_cpu_freq = RTC_CPU_FREQ_XTAL;
   	pm_config.light_sleep_enable = false;
   	esp_pm_configure(&pm_config);
    rtc_clk_cpu_freq_set(RTC_CPU_FREQ_160M);
   	uart_set_baudrate(UART_NUM_0, CONFIG_CONSOLE_UART_BAUDRATE);
	pm_config.max_cpu_freq = RTC_CPU_FREQ_240M;
   	esp_pm_configure(&pm_config);
    rtc_clk_cpu_freq_set(RTC_CPU_FREQ_240M);
   	uart_set_baudrate(UART_NUM_0, CONFIG_CONSOLE_UART_BAUDRATE);
	#endif
    // -------------------------------------------------------------------------------------------------------------
	*/

    uart_init();

	#if CONFIG_BOOT_SET_LED >= 0
	// Deactivate boot led
	gpio_pad_select_gpio(CONFIG_BOOT_SET_LED);
	GPIO_OUTPUT_SET(CONFIG_BOOT_SET_LED, CONFIG_BOOT_LED_ON ^ 1);
	#endif

	// Get and print reset & wakeup reasons
    mpsleep_init0();

    if (mpsleep_get_reset_cause() != MPSLEEP_DEEPSLEEP_RESET) rtc_init0();

    mp_thread_preinit(&mp_task_stack[0], mp_task_stack_len);

	// Thread init
	mp_thread_init();

    // Initialize the stack pointer for the main thread
    mp_stack_set_top((void *)sp);
	#ifdef CONFIG_MICROPY_USE_THREADED_REPL
    mp_stack_set_limit(mp_task_stack_len - 256);
	#else
    mp_stack_set_limit(mp_task_stack_len - 1024);
	#endif

    // initialize the mp heap
    gc_init(mp_task_heap, mp_task_heap + mpy_heap_size);

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
    else ESP_LOGE("MicroPython", "Error mounting Flash file system");

    // === Print some info ===
    char sbuff[24] = { 0 };
    gc_info_t info;
    gc_info(&info);
    // set gc.threshold to 80% of usable heap
	MP_STATE_MEM(gc_alloc_threshold) = ((info.total / 10) * 8) / MICROPY_BYTES_PER_GC_BLOCK;

	#if CONFIG_FREERTOS_UNICORE
    	printf("\nFreeRTOS running only on FIRST CORE.\n");
	#else
		#if CONFIG_MICROPY_USE_BOTH_CORES
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

	#ifdef CONFIG_MICROPY_USE_THREADED_REPL
	printf("   REPL stack: %d bytes\n", mpy_repl_stack_size);
	#else
	printf("    uPY stack: %d bytes\n", mp_task_stack_len);
	#endif

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
   	MP_THREAD_GIL_EXIT();

	#ifdef CONFIG_MICROPY_USE_TASK_WDT
	// Enable watchdog for MicroPython main task
	esp_task_wdt_init(CONFIG_TASK_WDT_TIMEOUT_S, false);
	esp_task_wdt_add(MainTaskHandle);
	esp_task_wdt_reset();
	#endif

	#ifdef CONFIG_MICROPY_USE_THREADED_REPL
   	// === Start REPL in separate thread ===
	pyexec_frozen_module("modREPL.py");
	#ifdef CONFIG_MICROPY_USE_TASK_WDT
	if (ReplTaskHandle) {
		// Enable watchdog for MicroPython main task
		esp_task_wdt_init(CONFIG_TASK_WDT_TIMEOUT_S, false);
		esp_task_wdt_add(ReplTaskHandle);
		esp_task_wdt_reset();
	}
	#endif
	if (!ReplTaskHandle) {
        printf("!! Error starting threaded REPL, Halted. !!\n");
	}
	while (1) {
		// Main task just waits doing nothing
		vTaskDelay(CONFIG_TASK_WDT_TIMEOUT_S * 500 / portTICK_RATE_MS);
		#ifdef CONFIG_MICROPY_USE_TASK_WDT
		esp_task_wdt_reset();
		#endif
	}
	#else
	// === Start REPL in main task ===
	ReplTaskHandle = MainTaskHandle;
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
	#endif
	// ================================================

    //ToDo: Remember the REPL mode (is it needed ?) !!
    prepareSleepReset(0, "ESP32: soft reboot\r\n");
    esp_restart(); // no return !!
}


//============================
void micropython_entry(void)
{
	// === Set esp32 log levels while running MicroPython ===
	if (CONFIG_MICRO_PY_LOG_LEVEL < CONFIG_LOG_DEFAULT_LEVEL) esp_log_level_set("*", CONFIG_MICRO_PY_LOG_LEVEL);
	if ((CONFIG_LOG_DEFAULT_LEVEL > ESP_LOG_WARN) && (CONFIG_MICRO_PY_LOG_LEVEL > ESP_LOG_WARN)){
		esp_log_level_set("wifi", ESP_LOG_WARN);
		esp_log_level_set("rmt", ESP_LOG_WARN);
		esp_log_level_set("tcpip_adapter", ESP_LOG_WARN);
		esp_log_level_set("event", ESP_LOG_WARN);
		esp_log_level_set("nvs", ESP_LOG_WARN);
		esp_log_level_set("phy_init", ESP_LOG_WARN);
		esp_log_level_set("wl_flash", ESP_LOG_WARN);
		esp_log_level_set("RTC_MODULE", ESP_LOG_WARN);
	}
	#ifdef CONFIG_MICROPY_USE_OTA
	if (CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_DEBUG) esp_log_level_set("OTA_UPDATE", ESP_LOG_DEBUG);
	else esp_log_level_set("OTA_UPDATE", CONFIG_LOG_DEFAULT_LEVEL);
	#endif

    #ifdef CONFIG_MICROPY_USE_MQTT
	esp_log_level_set(MQTT_TAG, CONFIG_MQTT_LOG_LEVEL);
	#endif
	#ifdef CONFIG_MICROPY_USE_FTPSERVER
	esp_log_level_set(FTP_TAG, CONFIG_FTPSERVER_LOG_LEVEL);
	#endif

    nvs_flash_init();

    // === Check and allocate stack ===
    // Open NVS name space
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &mpy_nvs_handle) != ESP_OK) {
    	mpy_nvs_handle = 0;
    	ESP_LOGE("MicroPython","Error while opening MicroPython NVS name space");
    }
    if (mpy_nvs_handle != 0) {
    	// Get stack size from NVS
        if (ESP_ERR_NVS_NOT_FOUND == nvs_get_i32(mpy_nvs_handle, "MPY_StackSize", &mpy_repl_stack_size)) {
        	mpy_repl_stack_size = CONFIG_MICROPY_STACK_SIZE * 1024;
        }
        else {
            if ((mpy_repl_stack_size < MPY_MIN_STACK_SIZE) || (mpy_repl_stack_size > MPY_MAX_STACK_SIZE)) {
            	mpy_repl_stack_size = CONFIG_MICROPY_STACK_SIZE * 1024;
            	ESP_LOGE("MicroPython","Wrong Stack size set in NVS: %d (set to configured: %d)", mpy_heap_size, CONFIG_MICROPY_HEAP_SIZE * 1024);
            }
            else {
            	ESP_LOGW("MicroPython","Stack size set from NVS: %d (configured: %d)", mpy_repl_stack_size, CONFIG_MICROPY_STACK_SIZE * 1024);
            }
        }
        // restore time zone
		tz_fromto_NVS(mpy_time_zone, NULL);
		if (strlen(mpy_time_zone) > 0) {
			setenv("TZ", mpy_time_zone, 1);
			tzset();
		}
    }
    mpy_repl_stack_size &= 0x7FFFFFFC;

    #ifdef CONFIG_MICROPY_USE_THREADED_REPL
	mp_task_stack_len = MP_TASK_STACK_LEN;
	#else
	mp_task_stack_len = mpy_repl_stack_size;
	#endif
	mp_task_stack = heap_caps_malloc(mp_task_stack_len, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
	if (mp_task_stack == NULL) {
        printf("Error allocating stack, Halted.\n");
        return;
	}

    // ==== Allocate heap memory ====
    if (mpy_nvs_handle != 0) {
        // Get heap size from NVS
        if (ESP_ERR_NVS_NOT_FOUND == nvs_get_i32(mpy_nvs_handle, "MPY_HeapSize", &mpy_heap_size)) {
        	mpy_heap_size = CONFIG_MICROPY_HEAP_SIZE * 1024;
        }
        else {
            if ((mpy_heap_size < MPY_MIN_HEAP_SIZE) || (mpy_heap_size > MPY_MAX_HEAP_SIZE)) {
            	mpy_heap_size = CONFIG_MICROPY_HEAP_SIZE * 1024;
            	ESP_LOGE("MicroPython","Wrong Heap size set in NVS: %d (set to configured: %d)", mpy_heap_size, CONFIG_MICROPY_HEAP_SIZE * 1024);
            }
            else {
            	ESP_LOGW("MicroPython","Heap size set from NVS: %d (configured: %d)", mpy_heap_size, CONFIG_MICROPY_HEAP_SIZE * 1024);
            }
        }
    }
    mpy_heap_size &= 0x7FFFFFFC;
    #if CONFIG_SPIRAM_SUPPORT
		// ## USING SPI RAM FOR HEAP ##
		#if CONFIG_SPIRAM_USE_CAPS_ALLOC
		mp_task_heap = heap_caps_malloc(mpy_heap_size, MALLOC_CAP_SPIRAM);
		#elif SPIRAM_USE_MEMMAP
		mp_task_heap = (uint8_t *)0x3f800000;
		#else
		mp_task_heap = malloc(mpy_heap_size);
		#endif
    #else
		// ## USING DRAM FOR HEAP ##
		mp_task_heap = malloc(mpy_heap_size);
    #endif

    if (mp_task_heap == NULL) {
        printf("Error allocating heap, Halted.\n");
        return;
    }

    // Workaround for possible bug in i2c driver !?
    periph_module_disable(PERIPH_I2C0_MODULE);
    periph_module_enable(PERIPH_I2C0_MODULE);

    printf("\n[=== MicroPython started ===]\n");
    // ==== Create and start main MicroPython task ====
	#if CONFIG_FREERTOS_UNICORE
		MainTaskCore = 0;
		MainTaskHandle = xTaskCreateStaticPinnedToCore(&mp_task, "mp_task", mp_task_stack_len, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb, 0);
	#else
		MainTaskCore = 1;
		#if CONFIG_MICROPY_USE_BOTH_CORES
			MainTaskHandle = xTaskCreateStatic(&mp_task, "mp_task", mp_task_stack_len, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb);
		#else
			MainTaskHandle = xTaskCreateStaticPinnedToCore(&mp_task, "mp_task", mp_task_stack_len, NULL, MP_TASK_PRIORITY, &mp_task_stack[0], &mp_task_tcb, 1);
		#endif
	#endif
}

//-----------------------------
void nlr_jump_fail(void *val) {
    printf("RESET: NLR jump failed, val=%p\n", val);
    prepareSleepReset(1, NULL);
    esp_restart();
}

