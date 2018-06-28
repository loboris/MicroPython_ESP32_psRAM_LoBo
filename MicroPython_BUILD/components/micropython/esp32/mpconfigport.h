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

// Options to control how MicroPython is built for this port,
// overriding defaults in py/mpconfig.h.

#include <stdint.h>
#include <alloca.h>
#include "rom/ets_sys.h"
#include "sdkconfig.h"

// ------------------------------------------------------------
// For testing only, don't change unless you want to experiment
// ------------------------------------------------------------
// Don't use alloca calls. As alloca() is not part of ANSI C, this
// workaround option is provided for compilers lacking this de-facto
// standard function. The way it works is allocating from heap, and
// relying on garbage collection to free it eventually. This is of
// course much less optimal than real alloca().
#define MICROPY_NO_ALLOCA                   (0)
// Avoid using C stack when making Python function calls.
// C stack still may be used if there's no free heap.
#define MICROPY_STACKLESS                   (0)
// Never use C stack when making Python function calls.
#define MICROPY_STACKLESS_STRICT            (0)
// Whether to build functions that print debugging info:
//   mp_bytecode_print
//   mp_parse_node_print
#define MICROPY_DEBUG_PRINTERS              (0)
// ------------------------------------------------------------

// object representation and NLR handling
#define MICROPY_OBJ_REPR                    (MICROPY_OBJ_REPR_A)
#define MICROPY_NLR_SETJMP                  (1)

// memory allocation policies
#define MICROPY_ALLOC_PATH_MAX              (128)

// emitters
#define MICROPY_PERSISTENT_CODE_LOAD        (1)
#define MICROPY_EMIT_XTENSA					(0)

// compiler configuration
#define MICROPY_COMP_MODULE_CONST           (1)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN    (1)

// optimizations
#define MICROPY_OPT_COMPUTED_GOTO           (1)
#define MICROPY_OPT_MPZ_BITWISE             (1)

// Python internal features
// Whether to return number of collected objects from gc.collect()
#ifdef CONFIG_MICROPY_GC_COLLECT_RETVAL
#define MICROPY_PY_GC_COLLECT_RETVAL        (1)
#else
#define MICROPY_PY_GC_COLLECT_RETVAL        (0)
#endif
#define MICROPY_READER_VFS                  (1)
#define MICROPY_ENABLE_GC                   (1)
// Be conservative and always clear to zero newly (re)allocated memory in the GC.
// This helps eliminate stray pointers that hold on to memory that's no longer used.
// It decreases performance due to unnecessary memory clearing.
#define MICROPY_GC_CONSERVATIVE_CLEAR       (1)
// Whether to enable finalisers in the garbage collector (ie call __del__)
#ifdef CONFIG_MICROPY_ENABLE_FINALISER
#define MICROPY_ENABLE_FINALISER            (1)
#else
#define MICROPY_ENABLE_FINALISER            (0)
#endif
#define MICROPY_STACK_CHECK                 (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_KBD_EXCEPTION               (1)
#define MICROPY_HELPER_REPL                 (1)
#define MICROPY_REPL_EMACS_KEYS             (1)
#define MICROPY_REPL_AUTO_INDENT            (1)
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
//#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_LONGLONG)
#define MICROPY_ENABLE_SOURCE_LINE          (1)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_WARNINGS                    (1)
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_COMPLEX         (1)
#define MICROPY_CPYTHON_COMPAT              (1)
#define MICROPY_STREAMS_NON_BLOCK           (1)
#define MICROPY_STREAMS_POSIX_API           (1)
#define MICROPY_MODULE_BUILTIN_INIT         (1)
#define MICROPY_MODULE_WEAK_LINKS           (1)
#define MICROPY_MODULE_FROZEN_STR           (0) // do not support frozen str modules
#define MICROPY_MODULE_FROZEN_MPY           (1)
#define MICROPY_QSTR_EXTRA_POOL             mp_qstr_frozen_const_pool
#define MICROPY_CAN_OVERRIDE_BUILTINS       (1)
#define MICROPY_USE_INTERNAL_ERRNO          (1)
#define MICROPY_USE_INTERNAL_PRINTF         (0) // ESP32 SDK requires its own printf, do NOT change
#define MICROPY_ENABLE_SCHEDULER            (1) // Do NOT change
// Maximum number of entries in the scheduler
#define MICROPY_SCHEDULER_DEPTH             (CONFIG_MICROPY_SCHEDULER_DEPTH)

#define MICROPY_VFS                         (1) // !! DO NOT CHANGE, MUST BE 1 !!
#define MICROPY_VFS_FAT                     (0) // !! DO NOT CHANGE, NOT USED  !!

// control over Python builtins
#define MICROPY_PY_FUNCTION_ATTRS           (1)
#define MICROPY_PY_STR_BYTES_CMP_WARN       (1)
#ifdef CONFIG_MICROPY_USE_UNICODE
#define MICROPY_PY_BUILTINS_STR_UNICODE     (1)
#else
#define MICROPY_PY_BUILTINS_STR_UNICODE     (0)
#endif
#define MICROPY_PY_BUILTINS_STR_CENTER      (1)
#define MICROPY_PY_BUILTINS_STR_PARTITION   (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES  (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY       (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW      (1)
#define MICROPY_PY_BUILTINS_SET             (1)
#define MICROPY_PY_BUILTINS_SLICE           (1)
#define MICROPY_PY_BUILTINS_SLICE_ATTRS     (1)
#define MICROPY_PY_BUILTINS_FROZENSET       (1)
#define MICROPY_PY_BUILTINS_PROPERTY        (1)
#define MICROPY_PY_BUILTINS_RANGE_ATTRS     (1)
#define MICROPY_PY_BUILTINS_TIMEOUTERROR    (1)
#define MICROPY_PY_ALL_SPECIAL_METHODS      (1)
#define MICROPY_PY_BUILTINS_COMPILE         (1)
#define MICROPY_PY_BUILTINS_ENUMERATE       (1)
#define MICROPY_PY_BUILTINS_EXECFILE        (1)
#define MICROPY_PY_BUILTINS_FILTER          (1)
#define MICROPY_PY_BUILTINS_REVERSED        (1)
#define MICROPY_PY_BUILTINS_NOTIMPLEMENTED  (1)
#define MICROPY_PY_BUILTINS_INPUT           (1)
#define MICROPY_PY_BUILTINS_MIN_MAX         (1)
#define MICROPY_PY_BUILTINS_POW3            (1)
#define MICROPY_PY_BUILTINS_HELP            (1)
#define MICROPY_PY_BUILTINS_HELP_TEXT       esp32_help_text
#define MICROPY_PY_BUILTINS_HELP_MODULES    (1)
#define MICROPY_PY___FILE__                 (1)
#define MICROPY_PY_MICROPYTHON_MEM_INFO     (1)
#define MICROPY_PY_ARRAY                    (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN       (1)
#define MICROPY_PY_ATTRTUPLE                (1)
#define MICROPY_PY_COLLECTIONS              (1)
#define MICROPY_PY_COLLECTIONS_DEQUE        (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_MATH                     (1)
#define MICROPY_PY_MATH_SPECIAL_FUNCTIONS   (1)
#define MICROPY_PY_CMATH                    (1)
#define MICROPY_PY_GC                       (1)
#define MICROPY_PY_IO                       (1)
#define MICROPY_PY_IO_FILEIO                (1)
#define MICROPY_PY_IO_BYTESIO               (1)
#define MICROPY_PY_IO_BUFFEREDWRITER        (1)
#define MICROPY_PY_STRUCT                   (1)
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_MAXSIZE              (1)
#define MICROPY_PY_SYS_MODULES              (1)
#define MICROPY_PY_SYS_EXIT                 (1)
#define MICROPY_PY_SYS_STDFILES             (1)
#define MICROPY_PY_SYS_STDIO_BUFFER         (1)
#define MICROPY_PY_UERRNO                   (1)
#define MICROPY_PY_USELECT                  (1)
#define MICROPY_PY_UTIME_MP_HAL             (1)
#define MICROPY_PY_THREAD                   (1)
#define MICROPY_PY_THREAD_GIL               (1)
// Number of VM jump-loops to do before releasing the GIL.
#define MICROPY_PY_THREAD_GIL_VM_DIVISOR    (CONFIG_MICROPY_PY_THREAD_GIL_VM_DIVISOR)

// extended modules
#define MICROPY_PY_UCTYPES                  (1)
#define MICROPY_PY_UZLIB                    (1)
#define MICROPY_PY_UJSON                    (1)
#define MICROPY_PY_URE                      (1)
#define MICROPY_PY_UHEAPQ                   (1)
#define MICROPY_PY_UTIMEQ                   (1)
#define MICROPY_PY_UBINASCII                (1)
#define MICROPY_PY_UBINASCII_CRC32          (1)
#define MICROPY_PY_URANDOM                  (1)
#define MICROPY_PY_URANDOM_EXTRA_FUNCS      (1)
#define MICROPY_PY_MACHINE                  (1)
#define MICROPY_PY_MACHINE_PIN_MAKE_NEW     mp_pin_make_new
#define MICROPY_PY_MACHINE_PULSE            (1)
#define MICROPY_PY_MACHINE_I2C              (1)
#define MICROPY_PY_MACHINE_SPI              (1)
#define MICROPY_PY_MACHINE_SPI_MSB          (0)
#define MICROPY_PY_MACHINE_SPI_LSB          (1)
#define MICROPY_PY_MACHINE_SPI_MAKE_NEW     machine_hw_spi_make_new
#define MICROPY_PY_USSL                     (1)
#define MICROPY_SSL_MBEDTLS                 (1)
#define MICROPY_PY_USSL_FINALISER           (0) // Crashes on gc if enabled!
#define MICROPY_PY_UHASHLIB                 (0) // We use the ESP32 version
#define MICROPY_PY_UHASHLIB_SHA1            (MICROPY_PY_USSL && MICROPY_SSL_MBEDTLS)

#ifdef CONFIG_MICROPY_USE_WEBSOCKETS
#define MICROPY_PY_WEBSOCKET                (1)
#else
#define MICROPY_PY_WEBSOCKET                (0)
#endif
#define MICROPY_PY_OS_DUPTERM               (0) // not supported, do NOT change
#define MICROPY_PY_WEBREPL                  (0) // not supported, do NOT change

#ifdef CONFIG_MICROPY_PY_FRAMEBUF
#define MICROPY_PY_FRAMEBUF                 (1)
#else
#define MICROPY_PY_FRAMEBUF                 (0)
#endif
#define MICROPY_PY_USOCKET_EVENTS           (MICROPY_PY_WEBREPL)

/*
 * Defined in 'component.mk'
#ifdef CONFIG_MICROPY_PY_USE_BTREE
#define MICROPY_PY_BTREE                    (1)
#else
#define MICROPY_PY_BTREE                    (0)
#endif
*/

// fatfs configuration
#if defined(CONFIG_FATFS_LFN_STACK)
#define MICROPY_FATFS_ENABLE_LFN            (2)
#elif defined(CONFIG_FATFS_LFN_HEAP)
#define MICROPY_FATFS_ENABLE_LFN            (3)
#else /* CONFIG_FATFS_LFN_NONE */
#define MICROPY_FATFS_ENABLE_LFN            (0)
#endif
#define MICROPY_FATFS_RPATH                 (2)
#define MICROPY_FATFS_MAX_SS                (4096)
#define MICROPY_FATFS_MAX_LFN               (CONFIG_FATFS_MAX_LFN)  // Get from sdkconfig
#define MICROPY_FATFS_LFN_CODE_PAGE         (CONFIG_FATFS_CODEPAGE) // Get from sdkconfig

#define mp_type_fileio                      nativefs_type_fileio
#define mp_type_textio                      nativefs_type_textio

// internal flash file system configuration
#ifdef CONFIG_MICROPY_INTERNALFS_ENCRIPTED
#define MICROPY_INTERNALFS_ENCRIPTED        (1)	// use encription on filesystem (UNTESTED!)
#else
#define MICROPY_INTERNALFS_ENCRIPTED        (0) // do not use encription on filesystem
#endif

// === sdcard using ESP32 sdmmc driver configuration ===
#ifdef CONFIG_MICROPY_SDMMC_SHOW_INFO
#define MICROPY_SDMMC_SHOW_INFO             (1) // show sdcard info after initialization
#else
#define MICROPY_SDMMC_SHOW_INFO             (0) // do not show sdcard info after initialization
#endif

// use vfs's functions for import stat and builtin open
#define mp_import_stat mp_vfs_import_stat
#define mp_builtin_open mp_vfs_open
#define mp_builtin_open_obj mp_vfs_open_obj

// extra built in names to add to the global namespace
#define MICROPY_PORT_BUILTINS \
    { MP_OBJ_NEW_QSTR(MP_QSTR_input), (mp_obj_t)&mp_builtin_input_obj }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_open), (mp_obj_t)&mp_builtin_open_obj },

// extra built in modules to add to the list of known ones
extern const struct _mp_obj_module_t utime_module;
extern const struct _mp_obj_module_t uos_module;
extern const struct _mp_obj_module_t mp_module_usocket;
extern const struct _mp_obj_module_t mp_module_machine;
extern const struct _mp_obj_module_t mp_module_network;
extern const struct _mp_obj_module_t mp_module_ymodem;

#ifdef CONFIG_MICROPY_USE_REQUESTS
extern const struct _mp_obj_module_t mp_module_requests;
#define BUILTIN_MODULE_REQUESTS { MP_OBJ_NEW_QSTR(MP_QSTR_requests), (mp_obj_t)&mp_module_requests },
#else
#define BUILTIN_MODULE_REQUESTS
#endif

#ifdef CONFIG_MICROPY_USE_CURL
extern const struct _mp_obj_module_t mp_module_curl;
#define BUILTIN_MODULE_CURL { MP_OBJ_NEW_QSTR(MP_QSTR_curl), (mp_obj_t)&mp_module_curl },
#else
#define BUILTIN_MODULE_CURL
#endif

#ifdef CONFIG_MICROPY_USE_SSH
extern const struct _mp_obj_module_t mp_module_ssh;
#define BUILTIN_MODULE_SSH { MP_OBJ_NEW_QSTR(MP_QSTR_ssh), (mp_obj_t)&mp_module_ssh },
#else
#define BUILTIN_MODULE_SSH
#endif

#ifdef CONFIG_MICROPY_USE_DISPLAY
extern const struct _mp_obj_module_t mp_module_display;
#define BUILTIN_MODULE_DISPLAY { MP_OBJ_NEW_QSTR(MP_QSTR_display), (mp_obj_t)&mp_module_display },
#else
#define BUILTIN_MODULE_DISPLAY
#endif

#ifdef CONFIG_MICROPY_USE_GSM
extern const struct _mp_obj_module_t mp_module_gsm;
#define BUILTIN_MODULE_GSM { MP_OBJ_NEW_QSTR(MP_QSTR_gsm), (mp_obj_t)&mp_module_gsm },
#else
#define BUILTIN_MODULE_GSM
#endif

#ifdef CONFIG_MICROPY_USE_OTA
extern const struct _mp_obj_module_t mp_module_ota;
#define BUILTIN_MODULE_OTA { MP_OBJ_NEW_QSTR(MP_QSTR_ota), (mp_obj_t)&mp_module_ota },
#else
#define BUILTIN_MODULE_OTA
#endif

#ifdef CONFIG_MICROPY_USE_BLUETOOTH
extern const struct _mp_obj_module_t mp_module_bluetooth;
#define BUILTIN_MODULE_BLUETOOTH { MP_OBJ_NEW_QSTR(MP_QSTR_bluetooth), (mp_obj_t)&mp_module_bluetooth },
#else
#define BUILTIN_MODULE_BLUETOOTH
#endif

#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_OBJ_NEW_QSTR(MP_QSTR_utime),    (mp_obj_t)&utime_module }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_uos),      (mp_obj_t)&uos_module }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_usocket),  (mp_obj_t)&mp_module_usocket }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_machine),  (mp_obj_t)&mp_module_machine }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_network),  (mp_obj_t)&mp_module_network }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_ymodem),   (mp_obj_t)&mp_module_ymodem }, \
	{ MP_OBJ_NEW_QSTR(MP_QSTR_uhashlib), (mp_obj_t)&mp_module_uhashlib }, \
	BUILTIN_MODULE_DISPLAY \
	BUILTIN_MODULE_CURL \
    BUILTIN_MODULE_REQUESTS \
	BUILTIN_MODULE_SSH \
	BUILTIN_MODULE_GSM \
	BUILTIN_MODULE_OTA \
	BUILTIN_MODULE_BLUETOOTH \

#define MICROPY_PORT_BUILTIN_MODULE_WEAK_LINKS \
    { MP_OBJ_NEW_QSTR(MP_QSTR_binascii), (mp_obj_t)&mp_module_ubinascii }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_collections), (mp_obj_t)&mp_module_collections }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_errno), (mp_obj_t)&mp_module_uerrno }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_hashlib), (mp_obj_t)&mp_module_uhashlib }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_heapq), (mp_obj_t)&mp_module_uheapq }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_io), (mp_obj_t)&mp_module_io }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_json), (mp_obj_t)&mp_module_ujson }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_os), (mp_obj_t)&uos_module }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_random), (mp_obj_t)&mp_module_urandom }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_re), (mp_obj_t)&mp_module_ure }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_select), (mp_obj_t)&mp_module_uselect }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_socket), (mp_obj_t)&mp_module_usocket }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_ssl), (mp_obj_t)&mp_module_ussl }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_struct), (mp_obj_t)&mp_module_ustruct }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_time), (mp_obj_t)&utime_module }, \
    { MP_OBJ_NEW_QSTR(MP_QSTR_zlib), (mp_obj_t)&mp_module_uzlib }, \

#define MP_STATE_PORT MP_STATE_VM

#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[20]; \

// type definitions for the specific machine
#define BYTES_PER_WORD (4)
#define MICROPY_MAKE_POINTER_CALLABLE(p) ((void*)((mp_uint_t)(p)))
#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)
#define MP_SSIZE_MAX (0x7fffffff)

// Note: these "critical nested" macros do not ensure cross-CPU exclusion,
// the only disable interrupts on the current CPU.  To full manage exclusion
// one should use portENTER_CRITICAL/portEXIT_CRITICAL instead.
#include "freertos/FreeRTOS.h"
#define MICROPY_BEGIN_ATOMIC_SECTION() portENTER_CRITICAL_NESTED()
#define MICROPY_END_ATOMIC_SECTION(state) portEXIT_CRITICAL_NESTED(state)

#if MICROPY_PY_THREAD
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(void); \
        mp_handle_pending(); \
        MP_THREAD_GIL_EXIT(); \
        vTaskDelay(1); \
        MP_THREAD_GIL_ENTER(); \
    } while (0);
#else
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(void); \
        mp_handle_pending(); \
        asm("waiti 0"); \
    } while (0);
#endif

#define UINT_FMT "%u"
#define INT_FMT "%d"

typedef int32_t mp_int_t; // must be pointer size
typedef uint32_t mp_uint_t; // must be pointer size
typedef long mp_off_t;
// ssize_t, off_t as required by POSIX-signatured functions in stream.h
#include <sys/types.h>

// board specifics

#define MICROPY_PY_SYS_PLATFORM "esp32_LoBo"
#define MICROPY_HW_BOARD_NAME   CONFIG_MICROPY_HW_BOARD_NAME
#define MICROPY_HW_MCU_NAME     CONFIG_MICROPY_HW_MCU_NAME
#define MICROPY_TIMEZONE        CONFIG_MICROPY_TIMEZONE
