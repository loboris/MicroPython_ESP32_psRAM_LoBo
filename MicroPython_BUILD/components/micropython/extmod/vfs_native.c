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

#include "py/mpconfig.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "ff.h"
#include "ffconf.h"

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "driver/sdmmc_types.h"
#include "driver/sdmmc_defs.h"
#include "diskio.h"
#include <fcntl.h>
//#include "diskio_spiflash.h"
#include "diskio_wl.h"
#include "esp_partition.h"

#include "py/nlr.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/vfs_native.h"
#include "lib/timeutils/timeutils.h"
#include "sdkconfig.h"

#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2
#include "libs/littleflash.h"
#endif

// esp32 partition configuration
static esp_partition_t * fs_partition = NULL;
#if CONFIG_MICROPY_FILESYSTEM_TYPE == 1
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
#endif

STATIC const byte fresult_to_errno_table[20] = {
    [FR_OK] = 0,
    [FR_DISK_ERR] = MP_EIO,
    [FR_INT_ERR] = MP_EIO,
    [FR_NOT_READY] = MP_EBUSY,
    [FR_NO_FILE] = MP_ENOENT,
    [FR_NO_PATH] = MP_ENOENT,
    [FR_INVALID_NAME] = MP_EINVAL,
    [FR_DENIED] = MP_EACCES,
    [FR_EXIST] = MP_EEXIST,
    [FR_INVALID_OBJECT] = MP_EINVAL,
    [FR_WRITE_PROTECTED] = MP_EROFS,
    [FR_INVALID_DRIVE] = MP_ENODEV,
    [FR_NOT_ENABLED] = MP_ENODEV,
    [FR_NO_FILESYSTEM] = MP_ENODEV,
    [FR_MKFS_ABORTED] = MP_EIO,
    [FR_TIMEOUT] = MP_EIO,
    [FR_LOCKED] = MP_EIO,
    [FR_NOT_ENOUGH_CORE] = MP_ENOMEM,
    [FR_TOO_MANY_OPEN_FILES] = MP_EMFILE,
    [FR_INVALID_PARAMETER] = MP_EINVAL,
};

#if FF_MAX_SS == FF_MIN_SS
#define SECSIZE(fs) (FF_MIN_SS)
#else
#define SECSIZE(fs) ((fs)->ssize)
#endif

#define mp_obj_native_vfs_t fs_user_mount_t

STATIC const char *TAG = "vfs_native";

sdcard_config_t sdcard_config = {
#if CONFIG_SDCARD_MODE == 1
        SDMMC_FREQ_DEFAULT,
#else
        SDMMC_FREQ_HIGHSPEED,
#endif
		CONFIG_SDCARD_MODE,
#if CONFIG_SDCARD_MODE == 1
		CONFIG_SDCARD_CLK,
		CONFIG_SDCARD_MOSI,
		CONFIG_SDCARD_MISO,
		CONFIG_SDCARD_CS,
#else
		-1,
		-1,
		-1,
		-1,
#endif
		VSPI_HOST
};

bool native_vfs_mounted[2] = {false, false};
STATIC sdmmc_card_t *sdmmc_card;


// esp-idf doesn't seem to have a cwd; create one.
char cwd[MICROPY_ALLOC_PATH_MAX + 1] = { 0 };


//-----------------------------------------------
int physicalPath(const char *path, char *ph_path)
{
    if (path[0] == '/') {
    	// absolute path
    	if (strstr(path, VFS_NATIVE_INTERNAL_MP) == path) {
			sprintf(ph_path, "%s%s", VFS_NATIVE_MOUNT_POINT, path+6);
    	}
    	else if (strstr(path, VFS_NATIVE_EXTERNAL_MP) == path) {
			sprintf(ph_path, "%s%s", VFS_NATIVE_SDCARD_MOUNT_POINT, path+3);
    	}
    	else return -3;
    }
    else {
    	strcpy(ph_path, cwd);
    	if (ph_path[strlen(ph_path)-1] != '/') strcat(ph_path, "/");
		strcat(ph_path, path);
    }
    return 0;
}

//-------------------------------------
int vfs_chdir(const char *path, int device)
{
	ESP_LOGV(TAG, "vfs_chdir() path: '%s'", path);

	int f = 1;
	if ((device == VFS_NATIVE_TYPE_SDCARD) && (strcmp(path,  VFS_NATIVE_SDCARD_MOUNT_POINT"/") == 0)) f = 0;
	else if (strcmp(path, VFS_NATIVE_MOUNT_POINT"/") == 0) f = 0;

	if (f) {
		struct stat buf;
		int res = stat(path, &buf);
		if (res < 0) {
			return -1;
		}
		if ((buf.st_mode & S_IFDIR) == 0)
		{
			errno = ENOTDIR;
			return -2;
		}
		if (strlen(path) >= sizeof(cwd))
		{
			errno = ENAMETOOLONG;
			return -3;
		}
	}

	strncpy(cwd, path, sizeof(cwd));

	ESP_LOGV(TAG, "cwd set to '%s' from path '%s'", cwd, path);
	return 0;
}

//----------------------------------
char *getcwd(char *buf, size_t size)
{
	if (size <= strlen(cwd))
	{
		errno = ENAMETOOLONG;
		return NULL;
	}
	strcpy(buf, cwd);
	if (strstr(buf, VFS_NATIVE_MOUNT_POINT) != NULL) {
		memmove(buf, buf + strlen(VFS_NATIVE_MOUNT_POINT), 1+strlen(buf+strlen(VFS_NATIVE_MOUNT_POINT)));
	}
	else if (strstr(cwd, VFS_NATIVE_SDCARD_MOUNT_POINT) != NULL) {
		memmove(buf, buf + strlen(VFS_NATIVE_SDCARD_MOUNT_POINT), 1+strlen(buf+strlen(VFS_NATIVE_SDCARD_MOUNT_POINT)));
	}

	return buf;
}

// Return absolute path un Flash filesystem
// It always starts with VFS_NATIVE_[xxx_]MOUNT_POINT (/spiflash/ | /spiffs/ | /sdcard/)
// with 'path' stripped of leading '/', './', '../', '..'
// On input 'path' DOES NOT contain MPY mount point ('/flash' or 'sd')
//-------------------------------------------------------------------------------------
const char *mkabspath(fs_user_mount_t *vfs, const char *path, char *absbuf, int buflen)
{
	ESP_LOGV(TAG, "abspath '%s' in cwd '%s'", path, cwd);

	if (path[0] == '/')
	{ // path is already absolute
		if (vfs->device == VFS_NATIVE_TYPE_SDCARD) sprintf(absbuf, "%s%s", VFS_NATIVE_SDCARD_MOUNT_POINT, path);
		else sprintf(absbuf, "%s%s", VFS_NATIVE_MOUNT_POINT, path);
		ESP_LOGV(TAG, " path '%s' is absolute `-> '%s'", path, absbuf);
		return absbuf;
	}

	int len;
	char buf[strlen(cwd) + 16];

	if (vfs->device == VFS_NATIVE_TYPE_SDCARD) {
		if (strstr(cwd, VFS_NATIVE_SDCARD_MOUNT_POINT) != cwd) {
			strcpy(buf, VFS_NATIVE_SDCARD_MOUNT_POINT);
			if (cwd[0] != '/') strcat(buf, "/");
			strcat(buf, cwd);
		}
		else strcpy(buf, cwd);
	}
	else {
		if (strstr(cwd, VFS_NATIVE_MOUNT_POINT) != cwd)	{
			strcpy(buf, VFS_NATIVE_MOUNT_POINT);
			if (cwd[0] != '/') strcat(buf, "/");
			strcat(buf, cwd);
		}
		else strcpy(buf, cwd);
	}
	if (buf[strlen(buf)-1] == '/') buf[strlen(buf)-1] = 0; // remove trailing '/' from cwd

	len = strlen(buf);
	while (1) {
		// handle './' and '../'
		if (path[0] == 0)
			break;
		if (path[0] == '.' && path[1] == 0) { // '.'
			path = &path[1];
			break;
		}
		if (path[0] == '.' && path[1] == '/') { // './'
			path = &path[2];
			continue;
		}
		if (path[0] == '.' && path[1] == '.' && path[2] == 0) { // '..'
			path = &path[2];
			while (len > 0 && buf[len] != '/') len--; // goto cwd parrent dir
			buf[len] = 0;
			break;
		}
		if (path[0] == '.' && path[1] == '.' && path[2] == '/') { // '../'
			path = &path[3];
			while (len > 0 && buf[len] != '/') len--; // goto cwd parrent dir
			buf[len] = 0;
			continue;
		}
		if (strlen(buf) >= buflen-1) {
			errno = ENAMETOOLONG;
			return NULL;
		}
		break;
	}

	if (strlen(buf) + strlen(path) >= buflen) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	ESP_LOGV(TAG, " cwd: '%s'  path: '%s'", buf, path);
	strcpy(absbuf, buf);
	if ((strlen(path) > 0) && (path[0] != '/')) strcat(absbuf, "/");
	strcat(absbuf, path);

	// If root is selected, add trailing '/'
	if ((vfs->device == VFS_NATIVE_TYPE_SDCARD) && (strcmp(absbuf, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0)) strcat(absbuf, "/");
	else if (strcmp(absbuf, VFS_NATIVE_MOUNT_POINT) == 0) strcat(absbuf, "/");

	ESP_LOGV(TAG, " '%s' -> '%s'", path, absbuf);
	return absbuf;
}

//=========================================================================================================
mp_obj_t native_vfs_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
	mp_arg_check_num(n_args, n_kw, 1, 2, false);

	mp_int_t dev_type = mp_obj_get_int(args[0]);
	if ((dev_type != VFS_NATIVE_TYPE_SPIFLASH) && (dev_type != VFS_NATIVE_TYPE_SDCARD)) {
		ESP_LOGV(TAG, "Unknown device type (%d)", dev_type);
		mp_raise_OSError(ENXIO);
	}

	// create new object
	fs_user_mount_t *vfs = m_new_obj(fs_user_mount_t);
	vfs->base.type = &mp_native_vfs_type; //type;
	vfs->device = mp_obj_get_int(args[0]);

	return MP_OBJ_FROM_PTR(vfs);
}

//-------------------------------------------------
STATIC mp_obj_t native_vfs_mkfs(mp_obj_t bdev_in) {
	ESP_LOGE(TAG, "mkfs(): NOT SUPPORTED");
	// not supported
	mp_raise_OSError(ENOENT);
	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(native_vfs_mkfs_fun_obj, native_vfs_mkfs);
STATIC MP_DEFINE_CONST_STATICMETHOD_OBJ(native_vfs_mkfs_obj, MP_ROM_PTR(&native_vfs_mkfs_fun_obj));

STATIC MP_DEFINE_CONST_FUN_OBJ_3(native_vfs_open_obj, nativefs_builtin_open_self);

//-----------------------------------------------------------------------------
STATIC mp_obj_t native_vfs_ilistdir_func(size_t n_args, const mp_obj_t *args) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(args[0]);

	bool is_str_type = true;
	const char *path;
	if (n_args == 2) {
		if (mp_obj_get_type(args[1]) == &mp_type_bytes) {
			is_str_type = false;
		}
		path = mp_obj_str_get_str(args[1]);
	} else {
		path = "";
	}

	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(self, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return native_vfs_ilistdir2(self, path, is_str_type);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_vfs_ilistdir_obj, 1, 2, native_vfs_ilistdir_func);

//--------------------------------------------------------------------
STATIC mp_obj_t native_vfs_remove(mp_obj_t vfs_in, mp_obj_t path_in) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_in);

	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(self, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	int res = unlink(path);
	if (res < 0) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_remove_obj, native_vfs_remove);

//-------------------------------------------------------------------
STATIC mp_obj_t native_vfs_rmdir(mp_obj_t vfs_in, mp_obj_t path_in) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_in);

	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(self, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	int res = rmdir(path);
	if (res < 0) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_rmdir_obj, native_vfs_rmdir);

//---------------------------------------------------------------------------------------
STATIC mp_obj_t native_vfs_rename(mp_obj_t vfs_in, mp_obj_t path_in, mp_obj_t path_out) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *old_path = mp_obj_str_get_str(path_in);
	const char *new_path = mp_obj_str_get_str(path_out);

	char old_absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	old_path = mkabspath(self, old_path, old_absbuf, sizeof(old_absbuf));
	if (old_path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	char new_absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	new_path = mkabspath(self, new_path, new_absbuf, sizeof(new_absbuf));
	if (new_path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	int res = rename(old_path, new_path);
	/*
	// FIXME: have to check if we can replace files with this
	if (res < 0 && errno == EEXISTS) {
		res = unlink(new_path);
		if (res < 0) {
			mp_raise_OSError(errno);
			return mp_const_none;
		}
		res = rename(old_path, new_path);
	}
	*/
	if (res < 0) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(native_vfs_rename_obj, native_vfs_rename);

//------------------------------------------------------------------
STATIC mp_obj_t native_vfs_mkdir(mp_obj_t vfs_in, mp_obj_t path_o) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_o);

	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(self, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	int res = mkdir(path, 0755);
	if (res < 0) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_mkdir_obj, native_vfs_mkdir);

/// Change current directory.
//-------------------------------------------------------------------
static mp_obj_t native_vfs_chdir(mp_obj_t vfs_in, mp_obj_t path_in) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_in);

	char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
	path = mkabspath(self, path, absbuf, sizeof(absbuf));
	if (path == NULL) {
		ESP_LOGV(TAG, "chdir(): Error: path is NULL");
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	int res = vfs_chdir(path, self->device);
	if (res < 0) {
		ESP_LOGV(TAG, "chdir(): Error %d (%d)", res, errno);
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_chdir_obj, native_vfs_chdir);

/// Get the current directory.
//--------------------------------------------------
STATIC mp_obj_t native_vfs_getcwd(mp_obj_t vfs_in) {
//	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);

	char buf[MICROPY_ALLOC_PATH_MAX + 1];

	char *ch = getcwd(buf, sizeof(buf));
	if (ch == NULL) {
		mp_raise_OSError(errno);
		return mp_const_none;
	}

	return mp_obj_new_str(buf, strlen(buf));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(native_vfs_getcwd_obj, native_vfs_getcwd);

/// Get the current drive.
//-------------------------------------
STATIC mp_obj_t native_vfs_getdrive() {

	char drive[32];

	if (MP_STATE_VM(vfs_cur) == MP_VFS_ROOT) {
		sprintf(drive, "/");
    }
	else {
		if (strstr(cwd, VFS_NATIVE_MOUNT_POINT) != NULL) sprintf(drive, VFS_NATIVE_INTERNAL_MP);
		else if (strstr(cwd, VFS_NATIVE_SDCARD_MOUNT_POINT) != NULL) sprintf(drive, VFS_NATIVE_EXTERNAL_MP);
		else sprintf(drive, "/");
	}

	return mp_obj_new_str(drive, strlen(drive));
}
MP_DEFINE_CONST_FUN_OBJ_0(native_vfs_getdrive_obj, native_vfs_getdrive);

/// \function stat(path)
/// Get the status of a file or directory.
//------------------------------------------------------------------
STATIC mp_obj_t native_vfs_stat(mp_obj_t vfs_in, mp_obj_t path_in) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_in);

	if ((path[0] != 0) && !((path[0] == '/') && (path[1] == 0))) {
		char absbuf[MICROPY_ALLOC_PATH_MAX + 1];
		path = mkabspath(self, path, absbuf, sizeof(absbuf));
		if (path == NULL) {
			mp_raise_OSError(errno);
			return mp_const_none;
		}
	}

	struct stat buf;
	if ((path[0] == 0) || ((path[0] == '/') && (path[1] == 0))) {
		// stat root directory
		buf.st_size = 0;
		buf.st_atime = 946684800; // Jan 1, 2000
		buf.st_mtime = buf.st_atime; // Jan 1, 2000
		buf.st_ctime = buf.st_atime; // Jan 1, 2000
		buf.st_mode = MP_S_IFDIR;
		if (self->device == VFS_NATIVE_TYPE_SPIFLASH) {
			#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
			uint32_t total, used;
		    esp_spiffs_info(VFS_NATIVE_INTERNAL_PART_LABEL, &total, &used);
			buf.st_size = total;
			#else
		    FRESULT res=0;
			FATFS *fatfs;
		    DWORD fre_clust;
			res = f_getfree(VFS_NATIVE_MOUNT_POINT, &fre_clust, &fatfs);
			if (res == 0) {
				buf.st_size = fatfs->csize * SECSIZE(fatfs) * (fatfs->n_fatent - 2);
			}
			#endif
		}
		else if (self->device == VFS_NATIVE_TYPE_SDCARD) {
		    FRESULT res=0;
			FATFS *fatfs;
		    DWORD fre_clust;
			res = f_getfree(VFS_NATIVE_SDCARD_MOUNT_POINT, &fre_clust, &fatfs);
			if (res == 0) {
				buf.st_size = fatfs->csize * SECSIZE(fatfs) * (fatfs->n_fatent - 2);
			}
		}
	}
	else {
		int res = stat(path, &buf);
		if (res < 0) {
			mp_raise_OSError(errno);
			return mp_const_none;
		}
	}

	mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
	t->items[0] = MP_OBJ_NEW_SMALL_INT(buf.st_mode);
	t->items[1] = MP_OBJ_NEW_SMALL_INT(0); // st_ino
	t->items[2] = MP_OBJ_NEW_SMALL_INT(0); // st_dev
	t->items[3] = MP_OBJ_NEW_SMALL_INT(0); // st_nlink
	t->items[4] = MP_OBJ_NEW_SMALL_INT(0); // st_uid
	t->items[5] = MP_OBJ_NEW_SMALL_INT(0); // st_gid
	t->items[6] = mp_obj_new_int(buf.st_size); // st_size
	t->items[7] = mp_obj_new_int(buf.st_atime); // st_atime
	t->items[8] = mp_obj_new_int(buf.st_mtime); // st_mtime
	t->items[9] = mp_obj_new_int(buf.st_ctime); // st_ctime

	return MP_OBJ_FROM_PTR(t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_stat_obj, native_vfs_stat);


// Get the status of a VFS.
//---------------------------------------------------------------------
STATIC mp_obj_t native_vfs_statvfs(mp_obj_t vfs_in, mp_obj_t path_in) {
	mp_obj_native_vfs_t *self = MP_OBJ_TO_PTR(vfs_in);
	const char *path = mp_obj_str_get_str(path_in);

	ESP_LOGV(TAG, "statvfs('%s') device: %d", path, self->device);

	int f_bsize=0, f_blocks=0, f_bfree=0, maxlfn=0;
    FRESULT res=0;
	FATFS *fatfs;
    DWORD fre_clust;

	if (self->device == VFS_NATIVE_TYPE_SPIFLASH) {
		#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
		uint32_t total, used;
		esp_spiffs_info(VFS_NATIVE_INTERNAL_PART_LABEL, &total, &used);
		f_bsize = 256; //SPIFFS_LOG_PAGE_SIZE;
		f_blocks = total / 256; //SPIFFS_LOG_PAGE_SIZE;
		f_bfree = (total-used) / 256; //SPIFFS_LOG_PAGE_SIZE;
		maxlfn = CONFIG_SPIFFS_OBJ_NAME_LEN;
		#elif CONFIG_MICROPY_FILESYSTEM_TYPE == 2
		maxlfn = LFS_NAME_MAX;
		uint32_t used = littleFlash_getUsedBlocks();
		f_bsize = littleFlash.lfs_cfg.block_size;
		f_blocks = littleFlash.lfs_cfg.block_count;
		f_bfree = f_blocks - used;
		#else
		res = f_getfree(VFS_NATIVE_MOUNT_POINT, &fre_clust, &fatfs);
		goto is_fat;
		#endif
	}
	else if (self->device == VFS_NATIVE_TYPE_SDCARD) {
		res = f_getfree(VFS_NATIVE_SDCARD_MOUNT_POINT, &fre_clust, &fatfs);
#if CONFIG_MICROPY_FILESYSTEM_TYPE == 1
is_fat:
#endif
	    if (res != 0) {
	    	ESP_LOGV(TAG, "statvfs('%s') Error %d", path, res);
	        mp_raise_OSError(fresult_to_errno_table[res]);
	    }
	    f_bsize = fatfs->csize * SECSIZE(fatfs);
	    f_blocks = fatfs->n_fatent - 2;
	    f_bfree = fre_clust;
	    maxlfn = FF_MAX_LFN;
	}

	mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));

    t->items[0] = MP_OBJ_NEW_SMALL_INT(f_bsize); // f_bsize
    t->items[1] = t->items[0]; // f_frsize
    t->items[2] = MP_OBJ_NEW_SMALL_INT(f_blocks); // f_blocks
    t->items[3] = MP_OBJ_NEW_SMALL_INT(f_bfree); // f_bfree
    t->items[4] = t->items[3]; // f_bavail
    t->items[5] = MP_OBJ_NEW_SMALL_INT(0); // f_files
    t->items[6] = MP_OBJ_NEW_SMALL_INT(0); // f_ffree
    t->items[7] = MP_OBJ_NEW_SMALL_INT(0); // f_favail
    t->items[8] = MP_OBJ_NEW_SMALL_INT(0); // f_flags
    t->items[9] = MP_OBJ_NEW_SMALL_INT(maxlfn); // f_namemax

    return MP_OBJ_FROM_PTR(t);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(native_vfs_statvfs_obj, native_vfs_statvfs);

//------------------------
STATIC void checkBoot_py()
{
	FILE *fd;
	struct stat buf;
	int res = stat(VFS_NATIVE_MOUNT_POINT"/boot.py", &buf);
	//fd = fopen(VFS_NATIVE_MOUNT_POINT"/boot.py", "rb");
    //if (fd == NULL) {
   	if (res < 0) {
    	fd = fopen(VFS_NATIVE_MOUNT_POINT"/boot.py", "wb");
        if (fd != NULL) {
        	char buf[128] = {'\0'};
        	sprintf(buf, "# This file is executed on every boot (including wake-boot from deepsleep)\nimport sys\nsys.path[1] = '/flash/lib'\n");
        	int len = strlen(buf);
    		int res = fwrite(buf, 1, len, fd);
    		if (res != len) {
    			ESP_LOGE(TAG, "Error writing to 'boot.py'");
    		}
    		else {
    			ESP_LOGD(TAG, "** 'boot.py' created **");
    		}
    		fclose(fd);
        }
        else {
			ESP_LOGE(TAG, "Error creating 'boot.py'");
        }
    }
    else {
		ESP_LOGV(TAG, "** 'boot.py' found **");
    	//fclose(fd);
    }
}

//---------------------------------------------------------------
STATIC void sdcard_print_info(const sdmmc_card_t* card, int mode)
{
    #if MICROPY_SDMMC_SHOW_INFO
	printf("---------------------\n");
	if (mode == 1) {
        printf(" Mode: SPI\n");
    }
	else if (mode == 2) {
        printf(" Mode:  SD (1bit)\n");
    }
	else if (mode == 3) {
        printf(" Mode:  SD (4bit)\n");
    }
	else if (mode == 3) {
        printf(" Mode:  Unknown\n");
    }
    printf("     Name: %s\n", card->cid.name);
    printf("     Type: %s\n", (card->ocr & SD_OCR_SDHC_CAP)?"SDHC/SDXC":"SDSC");
    printf("    Speed: %s (%d MHz)\n", (card->csd.tr_speed > 25000000)?"high speed":"default speed", card->csd.tr_speed/1000000);
    if (mode == 1) printf("SPI speed: %d MHz\n", card->host.max_freq_khz / 1000);
    printf("     Size: %u MB\n", (uint32_t)(((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024)));
    printf("      CSD: ver=%d, sector_size=%d, capacity=%d read_bl_len=%d\n",
            card->csd.csd_ver,
            card->csd.sector_size, card->csd.capacity, card->csd.read_block_len);
    printf("      SCR: sd_spec=%d, bus_width=%d\n\n", card->scr.sd_spec, card->scr.bus_width);
    #endif
}

//--------------------------------------------------------------------------------------------
static void _setPins(int8_t miso, int8_t mosi, int8_t clk, int8_t cs, int8_t dat1, int8_t dat2)
{
    if (miso >= 0) { // miso/dat0/dO
		gpio_pad_select_gpio(miso);
		gpio_set_direction(miso, GPIO_MODE_INPUT_OUTPUT_OD);
		gpio_set_pull_mode(miso, GPIO_PULLUP_ONLY);
        gpio_set_level(miso, 1);
    }
    if (mosi >= 0) { // mosi/cmd/dI
		gpio_pad_select_gpio(mosi);
		gpio_set_direction(mosi, GPIO_MODE_INPUT_OUTPUT_OD);
		gpio_set_pull_mode(mosi, GPIO_PULLUP_ONLY);
        gpio_set_level(mosi, 1);
    }
    if (clk >= 0) { // clk/sck
		gpio_pad_select_gpio(clk);
		gpio_set_direction(clk, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(clk, GPIO_PULLUP_ONLY);
		gpio_set_level(clk, 1);
    }
    if (cs >= 0) { // cs/dat3
        gpio_pad_select_gpio(cs);
        gpio_set_direction(cs, GPIO_MODE_INPUT_OUTPUT);
        gpio_set_pull_mode(cs, GPIO_PULLUP_ONLY);
        gpio_set_level(cs, 1);
    }
    if (dat1 >= 0) { // dat1
        gpio_pad_select_gpio(dat1);
        gpio_set_direction(dat1, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(dat1, GPIO_PULLUP_ONLY);
        gpio_set_level(dat1, 1);
    }
    if (dat2 >= 0) { // dat2
        gpio_pad_select_gpio(dat2);
        gpio_set_direction(dat2, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(dat2, GPIO_PULLUP_ONLY);
        gpio_set_level(dat2, 1);
    }
}

//-------------------------
static void _sdcard_mount()
{
    esp_err_t ret;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = CONFIG_MICROPY_FATFS_MAX_OPEN_FILES,
        .allocation_unit_size = 0
    };

	// Configure sdmmc interface
	if (sdcard_config.mode == 1) {
    	// Use SPI mode
		sdmmc_host_t host = SDSPI_HOST_DEFAULT();
		sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
		host.slot = sdcard_config.host;
		host.max_freq_khz = sdcard_config.max_speed;
		slot_config.dma_channel = 2;
		_setPins(sdcard_config.miso, sdcard_config.mosi, sdcard_config.clk, sdcard_config.cs, -1, -1);

	    slot_config.gpio_miso = sdcard_config.miso;
	    slot_config.gpio_mosi = sdcard_config.mosi;
	    slot_config.gpio_sck  = sdcard_config.clk;
	    slot_config.gpio_cs   = sdcard_config.cs;
	    ret = esp_vfs_fat_sdmmc_mount(VFS_NATIVE_SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &sdmmc_card);
	}
	else {
		sdmmc_host_t host = SDMMC_HOST_DEFAULT();
		sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        host.max_freq_khz = sdcard_config.max_speed; //(sdcard_config.max_speed > SDMMC_FREQ_DEFAULT) ? SDMMC_FREQ_HIGHSPEED : SDMMC_FREQ_DEFAULT;
		if (sdcard_config.mode == 2) {
	        // Use 1-line SD mode
		    // miso,mosi,clk,cs,dat1,dat2
		    _setPins(2, 15, 14, 13, -1, -1);
	        host.flags = SDMMC_HOST_FLAG_1BIT;
	        slot_config.width = 1;
		}
		else {
	        // Use 4-line SD mode
            // miso,mosi,clk,cs,dat1,dat2
		    _setPins(2, 15, 14, 13, 4, 12);
            host.flags = SDMMC_HOST_FLAG_4BIT;
            slot_config.width = 4;
		}
	    ret = esp_vfs_fat_sdmmc_mount(VFS_NATIVE_SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &sdmmc_card);
	}

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem on SDcard.");
        }
        else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Failed to initialize SDcard: not enough memory).");
        }
        else if (ret == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGE(TAG, "Failed to initialize SDcard: invalid response).");
        }
        else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to initialize SDcard: invalid state).");
        }
        else {
            ESP_LOGE(TAG, "Failed to initialize SDcard (%d).", ret);
        }
    	if (sdcard_config.mode == 1) sdspi_host_deinit();

    	mp_raise_OSError(MP_EIO);
    }
	ESP_LOGV(TAG, "SDCard FATFS mounted.");
    sdcard_print_info(sdmmc_card, sdcard_config.mode);
	native_vfs_mounted[VFS_NATIVE_TYPE_SDCARD] = true;
}

//------------------------------------------------------------------------------------
STATIC mp_obj_t native_vfs_mount(mp_obj_t self_in, mp_obj_t readonly, mp_obj_t mkfs) {
	fs_user_mount_t *self = MP_OBJ_TO_PTR(self_in);

	if ((self->device != VFS_NATIVE_TYPE_SPIFLASH) && (self->device != VFS_NATIVE_TYPE_SDCARD)) {
		ESP_LOGE(TAG, "Unknown device type (%d)", self->device);
		return mp_const_none;
	}

	// we will do an initial mount only on first call
	// already mounted?
	if (native_vfs_mounted[self->device]) {
		ESP_LOGW(TAG, "Device %d already mounted.", self->device);
		return mp_const_none;
	}

	if (self->device == VFS_NATIVE_TYPE_SPIFLASH) {
		// spiflash device
		esp_err_t ret;
		#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
	    fs_partition = (esp_partition_t *)esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, VFS_NATIVE_INTERNAL_PART_LABEL);
	    if (fs_partition == NULL) {
			printf("\nInternal SPIFFS: File system partition definition not found!\n");
			return mp_const_none;
	    }
	    esp_vfs_spiffs_conf_t conf = {
	      .base_path = VFS_NATIVE_MOUNT_POINT,
	      .partition_label = VFS_NATIVE_INTERNAL_PART_LABEL,
	      .max_files = CONFIG_MICROPY_FATFS_MAX_OPEN_FILES,
	      .format_if_mount_failed = true
	    };
	    ret = esp_vfs_spiffs_register(&conf);
	   	//if (spiffs_is_mounted == 0) {
	    if ((ret != ESP_OK) || (!esp_spiffs_mounted(VFS_NATIVE_INTERNAL_PART_LABEL))) {
			ESP_LOGE(TAG, "Failed to mount Flash partition as SPIFFS.");
			return mp_const_false;
	   	}
		native_vfs_mounted[self->device] = true;
		checkBoot_py();
		#elif CONFIG_MICROPY_FILESYSTEM_TYPE == 2
	    fs_partition = (esp_partition_t *)esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, VFS_NATIVE_INTERNAL_PART_LABEL);
	    if (fs_partition == NULL) {
			printf("\nInternal LittleFS: File system partition definition not found!\n");
			return mp_const_none;
	    }
	    const little_flash_config_t little_cfg = {
	        .part = fs_partition,
	        .base_path = VFS_NATIVE_MOUNT_POINT,
	        .open_files = CONFIG_MICROPY_FATFS_MAX_OPEN_FILES,
	        .auto_format = true,
	        .lookahead = 32
	    };
	    ret = littleFlash_init(&little_cfg);
	    if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to mount Flash partition as LittleFS.");
			return mp_const_false;
	   	}
		native_vfs_mounted[self->device] = true;
		checkBoot_py();
		#else
	    fs_partition = (esp_partition_t *)esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, VFS_NATIVE_INTERNAL_PART_LABEL);
	    if (fs_partition == NULL) {
			printf("\nInternal FatFS: File system partition definition not found!\n");
			return mp_const_none;
	    }

	    const esp_vfs_fat_mount_config_t mount_config = {
			.format_if_mount_failed = true,
			.max_files              = CONFIG_MICROPY_FATFS_MAX_OPEN_FILES,
			.allocation_unit_size   = 0,
		};
		// Mount spi Flash filesystem using configuration from sdkconfig.h
		esp_err_t err = esp_vfs_fat_spiflash_mount(VFS_NATIVE_MOUNT_POINT, VFS_NATIVE_INTERNAL_PART_LABEL, &mount_config, &s_wl_handle);

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Failed to mount Flash partition as FatFS(%d)", err);
			return mp_const_none;
		}
		native_vfs_mounted[self->device] = true;
		checkBoot_py();
		#endif

		#if MICROPY_SDMMC_SHOW_INFO
		int f_bsize=0, f_blocks=0, f_bfree=0;
		ret = ESP_FAIL;
		printf("\nInternal FS ");
		#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
			printf("(SPIFFS): ");
			uint32_t total, used;
			if (esp_spiffs_info(VFS_NATIVE_INTERNAL_PART_LABEL, &total, &used) == ESP_OK) {
				f_bsize = 256;
				f_blocks = total / 256;
				f_bfree = (total-used) / 256;
				ret = ESP_OK;
			}
		#elif CONFIG_MICROPY_FILESYSTEM_TYPE == 2
			printf("(LittleFS ver %d.%d): ", LFS_VERSION_MAJOR, LFS_VERSION_MINOR);
			uint32_t used = littleFlash_getUsedBlocks();
			f_bsize = littleFlash.lfs_cfg.block_size;
			f_blocks = littleFlash.lfs_cfg.block_count;
			f_bfree = f_blocks - used;
			ret = ESP_OK;
		#else
			printf("(FatFS): ");
			if (fs_partition->encrypted)	printf("[Encrypted] ");
			FATFS *fatfs;
			DWORD fre_clust;
			FRESULT res = f_getfree(VFS_NATIVE_MOUNT_POINT, &fre_clust, &fatfs);
			if (res == 0) {
				f_bsize = fatfs->csize * SECSIZE(fatfs);
				f_blocks = fatfs->n_fatent - 2;
				f_bfree = fre_clust;
				ret = ESP_OK;
			}
		#endif
		printf("Mounted on partition '%s' [size: %d; Flash address: 0x%6X]\n", fs_partition->label, fs_partition->size, fs_partition->address);
		if (ret == ESP_OK) {
			printf("----------------\n");
			printf("Filesystem size: %d B\n", f_blocks * f_bsize);
			printf("           Used: %d B\n", (f_blocks * f_bsize) - (f_bfree * f_bsize));
			printf("           Free: %d B\n", f_bfree * f_bsize);
			printf("----------------\n");
		}
		#endif
	}
	else if (self->device == VFS_NATIVE_TYPE_SDCARD) {
		_sdcard_mount();
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(native_vfs_mount_obj, native_vfs_mount);

//---------------------------------------------------
STATIC mp_obj_t native_vfs_umount(mp_obj_t self_in) {
	fs_user_mount_t *self = MP_OBJ_TO_PTR(self_in);

	if ((self->device == VFS_NATIVE_TYPE_SDCARD) && (native_vfs_mounted[self->device])) {
		esp_err_t ret = esp_vfs_fat_sdmmc_unmount();
		if (sdcard_config.mode == 1) sdspi_host_deinit();
	    if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unmount filesystem on SDcard (%d).", ret);
			mp_raise_OSError(MP_EIO);
	    }
        ESP_LOGV(TAG, "Filesystem on SDcard unmounted.");
		native_vfs_mounted[self->device] = false;
	}
	else if (self->device == VFS_NATIVE_TYPE_SPIFLASH) {
        ESP_LOGW(TAG, "Filesystem on Flash cannot be unmounted.");
		mp_raise_OSError(MP_EIO);
	}

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(native_vfs_umount_obj, native_vfs_umount);

//------------------
int internalUmount()
{
	int res = 0;
    if (native_vfs_mounted[VFS_NATIVE_TYPE_SPIFLASH]) {
		#if CONFIG_MICROPY_FILESYSTEM_TYPE == 0
    	res = esp_vfs_spiffs_unregister(VFS_NATIVE_INTERNAL_PART_LABEL);
    	if (res) res = 0;
		#elif CONFIG_MICROPY_FILESYSTEM_TYPE == 2
    	littleFlash_term(VFS_NATIVE_INTERNAL_PART_LABEL);
		#else
    	if (s_wl_handle != WL_INVALID_HANDLE) res = wl_unmount(s_wl_handle);
    	if (res) res = 0;
		#endif
    	native_vfs_mounted[VFS_NATIVE_TYPE_SPIFLASH] = false;
    }
    return res;
}

//-------------------
void externalUmount()
{
    if (native_vfs_mounted[VFS_NATIVE_TYPE_SDCARD]) {
    	esp_vfs_fat_sdmmc_unmount();
    	if (sdcard_config.mode == 1) sdspi_host_deinit();
		native_vfs_mounted[VFS_NATIVE_TYPE_SDCARD] = false;
    }
}

//-------------------------------------
bool file_noton_spi_sdcard(char *fname)
{
	if (sdcard_config.mode == 1) {
		if (strstr(fname, VFS_NATIVE_SDCARD_MOUNT_POINT)) return false;
	}
    return true;
}

//-------------------------------------
int mount_vfs(int type, char *chdir_to)
{
	char mp[16];
    mp_obj_t args1[1];
    mp_obj_t args2[2];

    if (type == VFS_NATIVE_TYPE_SPIFLASH) strcpy(mp, VFS_NATIVE_INTERNAL_MP);
    else if (type == VFS_NATIVE_TYPE_SDCARD) strcpy(mp, VFS_NATIVE_EXTERNAL_MP);
    else return -1;

    args1[0] = mp_obj_new_int(type);
    const mp_obj_type_t *vfsp = &mp_native_vfs_type;
    mp_obj_t vfso = vfsp->make_new(&mp_native_vfs_type, 1, 0, args1);

    // mount flash file system
    args2[0] = vfso;
    args2[1] = mp_obj_new_str(mp, strlen(mp));
    mp_call_function_n_kw(MP_OBJ_FROM_PTR(&mp_vfs_mount_obj), 2, 0, args2);

    if (native_vfs_mounted[type]) {
    	if (chdir_to != NULL) {
			// Change directory
			mp_call_function_1(MP_OBJ_FROM_PTR(&mp_vfs_chdir_obj), args2[1]);
    	}
    }
    else return -2;

    return 0;
}


//===============================================================
STATIC const mp_rom_map_elem_t native_vfs_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_mkfs),		MP_ROM_PTR(&native_vfs_mkfs_obj) },
	{ MP_ROM_QSTR(MP_QSTR_open),		MP_ROM_PTR(&native_vfs_open_obj) },
	{ MP_ROM_QSTR(MP_QSTR_ilistdir),	MP_ROM_PTR(&native_vfs_ilistdir_obj) },
	{ MP_ROM_QSTR(MP_QSTR_mkdir),		MP_ROM_PTR(&native_vfs_mkdir_obj) },
	{ MP_ROM_QSTR(MP_QSTR_rmdir),		MP_ROM_PTR(&native_vfs_rmdir_obj) },
	{ MP_ROM_QSTR(MP_QSTR_chdir),		MP_ROM_PTR(&native_vfs_chdir_obj) },
	{ MP_ROM_QSTR(MP_QSTR_getcwd),		MP_ROM_PTR(&native_vfs_getcwd_obj) },
	{ MP_ROM_QSTR(MP_QSTR_getdrive),	MP_ROM_PTR(&native_vfs_getdrive_obj) },
	{ MP_ROM_QSTR(MP_QSTR_remove),		MP_ROM_PTR(&native_vfs_remove_obj) },
	{ MP_ROM_QSTR(MP_QSTR_rename),		MP_ROM_PTR(&native_vfs_rename_obj) },
	{ MP_ROM_QSTR(MP_QSTR_stat),		MP_ROM_PTR(&native_vfs_stat_obj) },
	{ MP_ROM_QSTR(MP_QSTR_statvfs),		MP_ROM_PTR(&native_vfs_statvfs_obj) },
	{ MP_ROM_QSTR(MP_QSTR_mount),		MP_ROM_PTR(&native_vfs_mount_obj) },
	{ MP_ROM_QSTR(MP_QSTR_umount),		MP_ROM_PTR(&native_vfs_umount_obj) },
};
STATIC MP_DEFINE_CONST_DICT(native_vfs_locals_dict, native_vfs_locals_dict_table);

//========================================
const mp_obj_type_t mp_native_vfs_type = {
	{ &mp_type_type },
	.name = MP_QSTR_VfsNative,
	.make_new = native_vfs_make_new,
	.locals_dict = (mp_obj_dict_t*)&native_vfs_locals_dict,
};

