/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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
 * Originally created by SHA2017 Badge Team (https://github.com/SHA2017-badge/micropython-esp32)
 *
 * Modified by LoBo (https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo)
 *
 * Added support for SD Card and some changes to make it work better
 *
 */

// ======== CD Card support ===========================================================================

/*
 * Using SDCard with sdmmc driver connection:

ESP32 pin     | SD card pin    | SPI pin | Notes
--------------|----------------|---------|------------
              |      SD    uSD |         |
--------------|----------------|---------|------------
GPIO14 (MTMS) | CLK  5     5   | SCK     | 10k pullup in SD mode
GPIO15 (MTDO) | CMD  2     3   | MOSI    | 10k pullup, both in SD and SPI modes
GPIO2         | D0   7     7   | MISO    | 10k pullup in SD mode, pull low to go into download mode (see note below!)
GPIO4         | D1   8     8   | N/C     | not used in 1-line SD mode; 10k pullup in 4-line SD mode
GPIO12 (MTDI) | D2   9     1   | N/C     | not used in 1-line SD mode; 10k pullup in 4-line SD mode (see note below!)
GPIO13 (MTCK) | D3   1     2   | CS      | not used in 1-line SD mode, but card's D3 pin must have a 10k pullup
N/C           | CD             |         | optional, not used
N/C           | WP             |         | optional, not used
VDD     3.3V  | VSS  4      4  |
GND     GND   | GND  3&6    6  |

SDcard pinout                 uSDcard pinout
                 Contacts view
 _________________             1 2 3 4 5 6 7 8
|                 |            _______________
|                 |           |# # # # # # # #|
|                 |           |               |
|                 |           |               |
|                 |           /               |
|                 |          /                |
|                 |         |_                |
|                 |           |               |
|                #|          /                |
|# # # # # # # # /          |                 |
|_______________/           |                 |
 8 7 6 5 4 3 2 1 9          |_________________|

 */

#include "py/lexer.h"
#include "py/obj.h"
#include "py/objint.h"
#include "extmod/vfs.h"

#if MICROPY_USE_SPIFFS
#define VFS_NATIVE_MOUNT_POINT			"/_#!#_spiffs"
#else
#define VFS_NATIVE_MOUNT_POINT			"/_#!#_spiflash"
#endif
#define VFS_NATIVE_SDCARD_MOUNT_POINT	"/_#!#_sdcard"
#define VFS_NATIVE_INTERNAL_MP			"/flash"
#define VFS_NATIVE_EXTERNAL_MP			"/sd"
#define VFS_NATIVE_TYPE_SPIFLASH		0
#define VFS_NATIVE_TYPE_SDCARD			1


typedef struct _fs_user_mount_t {
    mp_obj_base_t base;
    mp_int_t device;
} fs_user_mount_t;

extern const mp_obj_type_t mp_native_vfs_type;

bool native_vfs_mounted[2];


int physicalPath(const char *path, char *ph_path);
char *getcwd(char *buf, size_t size);
const char * mkabspath(fs_user_mount_t *vfs, const char *path, char *absbuf, int buflen);
mp_import_stat_t native_vfs_import_stat(struct _fs_user_mount_t *vfs, const char *path);
mp_obj_t nativefs_builtin_open_self(mp_obj_t self_in, mp_obj_t path, mp_obj_t mode);
int mount_vfs(int type, char *chdir_to);
MP_DECLARE_CONST_FUN_OBJ_KW(mp_builtin_open_obj);
//MP_DECLARE_CONST_FUN_OBJ_2(native_vfs_chdir_obj);

int internalUmount();
int externalUmount();

mp_obj_t native_vfs_ilistdir2(struct _fs_user_mount_t *vfs, const char *path, bool is_str_type);
