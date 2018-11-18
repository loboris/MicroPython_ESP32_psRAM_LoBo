/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Josef Gajdusek
 * Copyright (c) 2016 Damien P. George
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

#include "sdkconfig.h"

#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include "driver/spi_common.h"

#include "py/mpconfig.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"
#include "mpversion.h"
#include "extmod/vfs_native.h"
#include "modmachine.h"
#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2
#include "libs/littleflash.h"
#endif

//extern const mp_obj_type_t mp_fat_vfs_type;

STATIC const qstr os_uname_info_fields[] = {
    MP_QSTR_sysname, MP_QSTR_nodename,
    MP_QSTR_release, MP_QSTR_version, MP_QSTR_machine
};
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_sysname_obj, MICROPY_PY_SYS_PLATFORM);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_nodename_obj, MICROPY_PY_SYS_PLATFORM);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_release_obj, MICROPY_VERSION_STRING);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_version_obj, MICROPY_GIT_TAG " on " MICROPY_BUILD_DATE);
STATIC const MP_DEFINE_STR_OBJ(os_uname_info_machine_obj, MICROPY_HW_BOARD_NAME " with " MICROPY_HW_MCU_NAME);

STATIC MP_DEFINE_ATTRTUPLE(
    os_uname_info_obj,
    os_uname_info_fields,
    5,
    (mp_obj_t)&os_uname_info_sysname_obj,
    (mp_obj_t)&os_uname_info_nodename_obj,
    (mp_obj_t)&os_uname_info_release_obj,
    (mp_obj_t)&os_uname_info_version_obj,
    (mp_obj_t)&os_uname_info_machine_obj
);

//------------------------------
STATIC mp_obj_t os_uname(void) {
    return (mp_obj_t)&os_uname_info_obj;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(os_uname_obj, os_uname);

//----------------------------------------
STATIC mp_obj_t os_urandom(mp_obj_t num) {
    mp_int_t n = mp_obj_get_int(num);
    vstr_t vstr;
    vstr_init_len(&vstr, n);
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        if ((i & 3) == 0) {
            r = esp_random(); // returns 32-bit hardware random number
        }
        vstr.buf[i] = r;
        r >>= 8;
    }
    return mp_obj_new_str_from_vstr(&mp_type_bytes, &vstr);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(os_urandom_obj, os_urandom);

#if MICROPY_PY_OS_DUPTERM
extern const mp_obj_type_t mp_uos_dupterm_obj;

//--------------------------------------------------
STATIC mp_obj_t os_dupterm_notify(mp_obj_t obj_in) {
    (void)obj_in;
    //mp_hal_signal_dupterm_input();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(os_dupterm_notify_obj, os_dupterm_notify);
#endif


//------------------------------------------------------------------
STATIC mp_obj_t os_mount_sdcard(size_t n_args, const mp_obj_t *args)
{
	if (n_args > 0) {
		int chd = mp_obj_get_int(args[0]);
		if (chd) {
		    mount_vfs(VFS_NATIVE_TYPE_SDCARD, VFS_NATIVE_EXTERNAL_MP);
		}
	}
	else mount_vfs(VFS_NATIVE_TYPE_SDCARD, NULL);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_mount_sdcard_obj, 0, 1, os_mount_sdcard);

//------------------------------------
STATIC mp_obj_t os_umount_sdcard(void)
{
    // umount external (sdcard) file system
	mp_obj_t sddir = mp_obj_new_str(VFS_NATIVE_EXTERNAL_MP, strlen(VFS_NATIVE_EXTERNAL_MP));
	mp_call_function_1(MP_OBJ_FROM_PTR(&mp_vfs_umount_obj), sddir);

	// Change directory to /flash
	sddir = mp_obj_new_str(VFS_NATIVE_INTERNAL_MP, strlen(VFS_NATIVE_INTERNAL_MP));
	mp_call_function_1(MP_OBJ_FROM_PTR(&mp_vfs_chdir_obj), sddir);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(os_umount_sdcard_obj, os_umount_sdcard);

//------------------------------------------------------------------------------------------
STATIC mp_obj_t os_sdcard_config(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_REQUIRED | MP_ARG_INT,  { .u_int = 0 } },
        { MP_QSTR_clk,                    MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_mosi,                   MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_miso,                   MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_cs,                     MP_ARG_OBJ,  { .u_obj = mp_const_none } },
        { MP_QSTR_maxspeed,               MP_ARG_INT,  { .u_int = -1 } },
        { MP_QSTR_spihost,                MP_ARG_INT,  { .u_int = VSPI_HOST } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int mode = args[0].u_int;
    if ((mode < 1) || (mode > 3)) {
        mp_raise_ValueError("Unsupported sdcard mode");
    }
    if (mode == 1) {
    	int clk = machine_pin_get_gpio(args[1].u_obj);
    	int mosi = machine_pin_get_gpio(args[2].u_obj);
    	int miso = machine_pin_get_gpio(args[3].u_obj);
    	int cs = machine_pin_get_gpio(args[4].u_obj);

        if (native_vfs_mounted[VFS_NATIVE_TYPE_SDCARD]) os_umount_sdcard();
    	sdcard_config.clk = clk;
    	sdcard_config.mosi = mosi;
    	sdcard_config.miso = miso;
    	sdcard_config.cs = cs;
    	sdcard_config.mode = mode;
        if ((args[6].u_int != HSPI_HOST) && (args[6].u_int != VSPI_HOST)) {
            mp_raise_ValueError("Unsupported SPI hots (1 (HSPI) or 2 (VSPI) allowed)");
        }
        sdcard_config.host = args[6].u_int;
    }
    else {
        if (native_vfs_mounted[VFS_NATIVE_TYPE_SDCARD]) os_umount_sdcard();
    	sdcard_config.mode = mode;
        sdcard_config.host = 1;
    }
    if (args[5].u_int >= 0) {
        if ((args[5].u_int == 400) || ((args[5].u_int >= 8) && (args[5].u_int <= 40))) {
            if (args[5].u_int == 400) sdcard_config.max_speed = 400;
            else sdcard_config.max_speed = args[5].u_int * 1000;
        }
        else {
            mp_raise_ValueError("Unsupported max speed (8 - 40 MHz allowed)");
        }
    }

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(os_sdcard_config_obj, 0, os_sdcard_config);

#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2
//----------------------------------------------------------
STATIC mp_obj_t os_trim(size_t n_args, const mp_obj_t *args)
{
	uint32_t nblocks = 0;
	int noerase = 0;
	if (n_args > 0) {
		nblocks = mp_obj_get_int(args[0]);
	}
	if (n_args > 1) {
		noerase = mp_obj_get_int(args[1]);
	}
	uint32_t nerased = littleFlash_trim(nblocks, noerase);

	return mp_obj_new_int(nerased);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_trim_obj, 0, 2, os_trim);

#endif

//==========================================================
STATIC const mp_rom_map_elem_t os_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),		MP_ROM_QSTR(MP_QSTR_uos) },
    { MP_ROM_QSTR(MP_QSTR_uname),			MP_ROM_PTR(&os_uname_obj) },
    { MP_ROM_QSTR(MP_QSTR_urandom),			MP_ROM_PTR(&os_urandom_obj) },
    #if MICROPY_PY_OS_DUPTERM
    { MP_ROM_QSTR(MP_QSTR_dupterm),			MP_ROM_PTR(&mp_uos_dupterm_obj) },
    { MP_ROM_QSTR(MP_QSTR_dupterm_notify),	MP_ROM_PTR(&os_dupterm_notify_obj) },
    #endif
    #if MICROPY_VFS
    { MP_ROM_QSTR(MP_QSTR_ilistdir),		MP_ROM_PTR(&mp_vfs_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_listdir),			MP_ROM_PTR(&mp_vfs_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir),			MP_ROM_PTR(&mp_vfs_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir),			MP_ROM_PTR(&mp_vfs_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir),			MP_ROM_PTR(&mp_vfs_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd),			MP_ROM_PTR(&mp_vfs_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_getdrive),		MP_ROM_PTR(&native_vfs_getdrive_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove),			MP_ROM_PTR(&mp_vfs_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename),			MP_ROM_PTR(&mp_vfs_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat),			MP_ROM_PTR(&mp_vfs_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_statvfs),			MP_ROM_PTR(&mp_vfs_statvfs_obj) },
    //{ MP_ROM_QSTR(MP_QSTR_mount),			MP_ROM_PTR(&mp_vfs_mount_obj) },
    //{ MP_ROM_QSTR(MP_QSTR_umount),		MP_ROM_PTR(&mp_vfs_umount_obj) },
    { MP_ROM_QSTR(MP_QSTR_mountsd),			MP_ROM_PTR(&os_mount_sdcard_obj) },
    { MP_ROM_QSTR(MP_QSTR_umountsd),		MP_ROM_PTR(&os_umount_sdcard_obj) },
	{ MP_ROM_QSTR(MP_QSTR_sdconfig),		MP_ROM_PTR(&os_sdcard_config_obj) },
	#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2
	{ MP_ROM_QSTR(MP_QSTR_trim),			MP_ROM_PTR(&os_trim_obj) },
	#endif
	// Constants
	{ MP_ROM_QSTR(MP_QSTR_SDMODE_SPI),		MP_ROM_INT(1) },
	{ MP_ROM_QSTR(MP_QSTR_SDMODE_1LINE),	MP_ROM_INT(2) },
	{ MP_ROM_QSTR(MP_QSTR_SDMODE_4LINE),	MP_ROM_INT(3) },
    #endif
};

STATIC MP_DEFINE_CONST_DICT(os_module_globals, os_module_globals_table);

const mp_obj_module_t uos_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&os_module_globals,
};
