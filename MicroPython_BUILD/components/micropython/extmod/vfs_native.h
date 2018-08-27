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
 * Originally created by SHA2017 Badge Team (https://github.com/SHA2017-badge/micropython-esp32)
 *
 * Modified by LoBo (https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo)
 *
 */

#include "sdkconfig.h"
#include "py/lexer.h"
#include "py/obj.h"
#include "py/objint.h"
#include "extmod/vfs.h"

#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
#define VFS_NATIVE_MOUNT_POINT			"/_#!#_spiffs"
#elif CONFIG_MICROPY_FILESYSTEM_TYPE == 2
#define VFS_NATIVE_MOUNT_POINT			"/_#!#_littlefs"
#else
#define VFS_NATIVE_MOUNT_POINT			"/_#!#_spiflash"
#endif
#define VFS_NATIVE_SDCARD_MOUNT_POINT	"/_#!#_sdcard"
#define VFS_NATIVE_INTERNAL_PART_LABEL	"internalfs"
#define VFS_NATIVE_INTERNAL_MP			"/flash"
#define VFS_NATIVE_EXTERNAL_MP			"/sd"
#define VFS_NATIVE_TYPE_SPIFLASH		0
#define VFS_NATIVE_TYPE_SDCARD			1


typedef struct _fs_user_mount_t {
    mp_obj_base_t base;
    mp_int_t device;
} fs_user_mount_t;

typedef struct _sdcard_config_t {
    int32_t  max_speed;
	uint8_t	mode;
    int8_t	clk;
    int8_t	mosi;
    int8_t	miso;
    int8_t	cs;
    uint8_t host;
} sdcard_config_t;

extern const mp_obj_type_t mp_native_vfs_type;
extern sdcard_config_t sdcard_config;

bool native_vfs_mounted[2];


int physicalPath(const char *path, char *ph_path);
char *getcwd(char *buf, size_t size);
const char * mkabspath(fs_user_mount_t *vfs, const char *path, char *absbuf, int buflen);
mp_import_stat_t native_vfs_import_stat(struct _fs_user_mount_t *vfs, const char *path);
mp_obj_t nativefs_builtin_open_self(mp_obj_t self_in, mp_obj_t path, mp_obj_t mode);
int mount_vfs(int type, char *chdir_to);
MP_DECLARE_CONST_FUN_OBJ_KW(mp_builtin_open_obj);
MP_DECLARE_CONST_FUN_OBJ_0(native_vfs_getdrive_obj);
//MP_DECLARE_CONST_FUN_OBJ_2(native_vfs_chdir_obj);

int internalUmount();
void externalUmount();

bool file_noton_spi_sdcard(char *fname);

mp_obj_t native_vfs_ilistdir2(struct _fs_user_mount_t *vfs, const char *path, bool is_str_type);
