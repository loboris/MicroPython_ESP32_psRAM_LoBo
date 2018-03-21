/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Adapted from https://github.com/lllucius/esp32_littleflash, see the Copyright and license notice below
 *
 */

// Copyright 2017-2018 Leland Lucius
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sdkconfig.h"

#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "libs/littleflash.h"

static const char *TAG = "littleflash";

littleFlash_t littleFlash = {0};

// ============================================================================
// LFS disk interface for internal flash
// ============================================================================

//-------------------------------------------------------------------------------------------------------------------
static int internal_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    ESP_LOGV(TAG, "%s - block=0x%08x off=0x%08x size=%d", __func__, block, off, size);

    littleFlash_t *self = (littleFlash_t *) c->context;
    esp_err_t err = esp_partition_read(self->part, (block * self->sector_sz) + off, buffer, size);

    return err == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

//-------------------------------------------------------------------------------------------------------------------------
static int internal_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    ESP_LOGV(TAG, "%s - block=0x%08x off=0x%08x size=%d", __func__, block, off, size);

    littleFlash_t *self = (littleFlash_t *) c->context;
    esp_err_t err = esp_partition_write(self->part, (block * self->sector_sz) + off, buffer, size);

    return err == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

//----------------------------------------------------------------------
static int internal_erase(const struct lfs_config *c, lfs_block_t block)
{
    ESP_LOGV(TAG, "%s - block=0x%08x", __func__, block);

    littleFlash_t *self = (littleFlash_t *) c->context;

    // Check if the sector is already erased
    uint8_t f = 1;
    esp_err_t err = 0;
    uint8_t *buff = heap_caps_malloc(self->sector_sz, MALLOC_CAP_DMA);
    if (buff) {
        err = esp_partition_read(self->part, (block * self->sector_sz), buff, self->sector_sz);
        if (err == ESP_OK) {
            f = 0;
            for (int i=0; i<self->sector_sz; i++) {
                if (buff[i] != 0xFF) {
                    f = 1;
                    break;
                }
            }
        }
        free(buff);
        err = ESP_OK;
    }
	if (f) {
		err = esp_partition_erase_range(self->part, block * self->sector_sz, self->sector_sz);
	}

    return err == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

//--------------------------------------------------
static int internal_sync(const struct lfs_config *c)
{
    ESP_LOGV(TAG, "%s", __func__);

    return LFS_ERR_OK;
}


// ============================================================================
// ESP32 VFS implementation
// ============================================================================

typedef struct
{
    DIR dir;                // must be first...ESP32 VFS expects it...
    struct dirent dirent;
    lfs_dir_t lfs_dir;
    long off;
} vfs_lfs_dir_t;

//-------------------------------
static int map_lfs_error(int err)
{
    if (err == LFS_ERR_OK)
    {
        return 0;
    }

    switch (err)
    {
        case LFS_ERR_IO:
            errno = EIO;
        break;
        case LFS_ERR_CORRUPT:
            errno = EIO;
        break;
        case LFS_ERR_NOENT:
            errno = ENOENT;
        break;
        case LFS_ERR_EXIST:
            errno = EEXIST;
        break;
        case LFS_ERR_NOTDIR:
            errno = ENOTDIR;
        break;
        case LFS_ERR_ISDIR:
            errno = EISDIR;
        break;
        case LFS_ERR_NOTEMPTY:
            errno = ENOTEMPTY;
        break;
        case LFS_ERR_INVAL:
            errno = EINVAL;
        break;
        case LFS_ERR_NOSPC:
            errno = ENOSPC;
        break;
        case LFS_ERR_NOMEM:
            errno = ENOMEM;
        break;
        default:
            errno = EINVAL;
        break;
    }

    return -1;
}

//----------------------
static int get_free_fd()
{
    for (int i = 0; i < littleFlash.open_files; i++) {
        if (littleFlash.fds[i].file == NULL) return i;
    }

    return -1;
}

//----------------------------------------------------------------------
static ssize_t write_p(void *ctx, int fd, const void *data, size_t size)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    lfs_ssize_t written = lfs_file_write(&self->lfs, self->fds[fd].file, data, size);

    _lock_release(&self->lock);

    if (written < 0)
    {
        return map_lfs_error(written);
    }

    return written;
}

//-----------------------------------------------------------
static off_t lseek_p(void *ctx, int fd, off_t size, int mode)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    int lfs_mode = 0;
    if (mode == SEEK_SET)
    {
        lfs_mode = LFS_SEEK_SET;
    }
    else if (mode == SEEK_CUR)
    {
        lfs_mode = LFS_SEEK_CUR;
    }
    else if (mode == SEEK_END)
    {
        lfs_mode = LFS_SEEK_END;
    }
    else
    {
        errno = EINVAL;
        return -1;
    }

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    lfs_soff_t pos = lfs_file_seek(&self->lfs, self->fds[fd].file, size, lfs_mode);

    if (pos >= 0)
    {
        pos = lfs_file_tell(&self->lfs, self->fds[fd].file);
    }

    _lock_release(&self->lock);

    if (pos < 0)
    {
        return map_lfs_error(pos);
    }

    return pos;
}

//--------------------------------------------------------------
static ssize_t read_p(void *ctx, int fd, void *dst, size_t size)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    lfs_ssize_t read = lfs_file_read(&self->lfs, self->fds[fd].file, dst, size);

    _lock_release(&self->lock);

    if (read < 0)
    {
        return map_lfs_error(read);
    }

    return read;
}

//-----------------------------------------------------------------
static int open_p(void *ctx, const char *path, int flags, int mode)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    int lfs_flags = 0;
    if ((flags & O_ACCMODE) == O_RDONLY)
    {
        lfs_flags = LFS_O_RDONLY;
    }
    else if ((flags & O_ACCMODE) == O_WRONLY)
    {
        lfs_flags = LFS_O_WRONLY;
    }
    else if ((flags & O_ACCMODE) == O_RDWR)
    {
        lfs_flags = LFS_O_RDWR;
    }

    if (flags & O_CREAT)
    {
        lfs_flags |= LFS_O_CREAT;
    }

    if (flags & O_EXCL)
    {
        lfs_flags |= LFS_O_EXCL;
    }

    if (flags & O_TRUNC)
    {
        lfs_flags |= LFS_O_TRUNC;
    }
    
    if (flags & O_APPEND)
    {
        lfs_flags |= LFS_O_APPEND;
    }

    lfs_file_t *file = (lfs_file_t *) malloc(sizeof(lfs_file_t));
    if (file == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    char *name = strdup(path);
    if (name == NULL)
    {
        free(file);
        errno = ENOMEM;
        return -1;
    }

    _lock_acquire(&self->lock);

    int fd = get_free_fd();
    if (fd == -1)
    {
        _lock_release(&self->lock);
        free(name);
        free(file);
        errno = ENFILE;
        return -1;
    }

    int err = lfs_file_open(&self->lfs, file, path, lfs_flags);
    if (err < 0)
    {
        _lock_release(&self->lock);
        free(name);
        free(file);
        return map_lfs_error(err);
    }

    self->fds[fd].file = file;
    self->fds[fd].name = name;

    _lock_release(&self->lock);

    return fd;
}

//-----------------------------------
static int close_p(void *ctx, int fd)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    int err = lfs_file_close(&self->lfs, self->fds[fd].file);

    free(self->fds[fd].name);
    free(self->fds[fd].file);
    memset(&self->fds[fd], 0 , sizeof(vfs_fd_t));

    _lock_release(&self->lock);

    return map_lfs_error(err);
}

//----------------------------------------------------
static int fstat_p(void *ctx, int fd, struct stat *st)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    struct lfs_info lfs_info;;
    int err = lfs_stat(&self->lfs, self->fds[fd].name, &lfs_info);

    _lock_release(&self->lock);

    if (err < 0)
    {
        return map_lfs_error(err);
    }

    st->st_size = lfs_info.size;
    if (lfs_info.type == LFS_TYPE_DIR)
    {
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    }
    else
    {
        st->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
    }
    st->st_mtime = lfs_info.time;
    st->st_atime = 0;
    st->st_ctime = 0;

    return 0;
}

//-------------------------------------------------------------
static int stat_p(void *ctx, const char *path, struct stat *st)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    struct lfs_info lfs_info;
    int err = lfs_stat(&self->lfs, path, &lfs_info);

    _lock_release(&self->lock);

    if (err < 0)
    {
        return map_lfs_error(err);
    }

    st->st_size = lfs_info.size;
    if (lfs_info.type == LFS_TYPE_DIR)
    {
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    }
    else
    {
        st->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
    }
    st->st_mtime = lfs_info.time;
    st->st_atime = 0;
    st->st_ctime = 0;

    return 0;
}

//----------------------------------------------
static int unlink_p(void *ctx, const char *path)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    int err = lfs_remove(&self->lfs, path);

    _lock_release(&self->lock);

    return map_lfs_error(err);
}

//--------------------------------------------------------------
static int rename_p(void *ctx, const char *src, const char *dst)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    int err = lfs_rename(&self->lfs, src, dst);

    _lock_release(&self->lock);

    return map_lfs_error(err);
}

//------------------------------------------------
static DIR *opendir_p(void *ctx, const char *name)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) malloc(sizeof(vfs_lfs_dir_t));
    if (vfs_dir == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    //*vfs_dir = {};

    _lock_acquire(&self->lock);

    int err = lfs_dir_open(&self->lfs, &vfs_dir->lfs_dir, name);

    _lock_release(&self->lock);

    if (err != LFS_ERR_OK)
    {
        free(vfs_dir);
        vfs_dir = NULL;
        map_lfs_error(err);
    }

    return (DIR *) vfs_dir;
}

//--------------------------------------------------------------------------------------------
static int readdir_r_p(void *ctx, DIR *pdir, struct dirent *entry, struct dirent **out_dirent)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL)
    {
        errno = EBADF;
        return errno;
    }

    _lock_acquire(&self->lock);

    struct lfs_info lfs_info;
    int err = lfs_dir_read(&self->lfs, &vfs_dir->lfs_dir, &lfs_info);

    _lock_release(&self->lock);

    if (err == 0)
    {
        *out_dirent = NULL;
        return 0;
    }

    if (err < 0)
    {
        map_lfs_error(err);
        return errno;
    }

    entry->d_ino = 0;
    if (lfs_info.type == LFS_TYPE_REG)
    {
        entry->d_type = DT_REG;
    }
    else if (lfs_info.type == LFS_TYPE_DIR)
    {
        entry->d_type = DT_DIR;
    }
    else
    {
        entry->d_type = DT_UNKNOWN;
    }
    size_t len = strlcpy(entry->d_name, lfs_info.name, sizeof(entry->d_name));

    // This "shouldn't" happen, but the LFS name length can be customized and may
    // be longer than what's provided in "struct dirent"
    if (len >= sizeof(entry->d_name))
    {
        errno = ENAMETOOLONG;
        return errno;
    }

    vfs_dir->off++;

    *out_dirent = entry;

    return 0;
}

//---------------------------------------------------
static struct dirent *readdir_p(void *ctx, DIR *pdir)
{
    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL)
    {
        errno = EBADF;
        return NULL;
    }

    struct dirent *out_dirent = NULL;

    int err = readdir_r_p(ctx, pdir, &vfs_dir->dirent, &out_dirent);
    if (err != 0)
    {
        errno = err;
    }

    return out_dirent;
}

//-----------------------------------------
static long telldir_p(void *ctx, DIR *pdir)
{
    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL)
    {
        errno = EBADF;
        return errno;
    }

    return vfs_dir->off;
}

//------------------------------------------------------
static void seekdir_p(void *ctx, DIR *pdir, long offset)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL)
    {
        errno = EBADF;
        return;
    }

    _lock_acquire(&self->lock);

    // ESP32 VFS expects simple 0 to n counted directory offsets but lfs
    // doesn't so we need to "translate"...
    int err = lfs_dir_rewind(&self->lfs, &vfs_dir->lfs_dir);
    if (err >= 0)
    {
        for (vfs_dir->off = 0; vfs_dir->off < offset; ++vfs_dir->off)
        {
            struct lfs_info lfs_info;
            err = lfs_dir_read(&self->lfs, &vfs_dir->lfs_dir, &lfs_info);
            if (err < 0)
            {
                break;
            }
        }
    }

    _lock_release(&self->lock);

    if (err < 0)
    {
        map_lfs_error(err);
        return;
    }

    return;
}

//-----------------------------------------
static int closedir_p(void *ctx, DIR *pdir)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    vfs_lfs_dir_t *vfs_dir = (vfs_lfs_dir_t *) pdir;
    if (vfs_dir == NULL)
    {
        errno = EBADF;
        return -1;
    }

    _lock_acquire(&self->lock);

    int err = lfs_dir_close(&self->lfs, &vfs_dir->lfs_dir);

    _lock_release(&self->lock);

    free(vfs_dir);

    return map_lfs_error(err);
}

//----------------------------------------------------------
static int mkdir_p(void *ctx, const char *name, mode_t mode)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    int err = lfs_mkdir(&self->lfs, name);

    _lock_release(&self->lock);

    return map_lfs_error(err);
}

//---------------------------------------------
static int rmdir_p(void *ctx, const char *name)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    int err = lfs_remove(&self->lfs, name);

    _lock_release(&self->lock);

    return map_lfs_error(err);
}

//-----------------------------------
static int fsync_p(void *ctx, int fd)
{
    littleFlash_t *self = (littleFlash_t *) ctx;

    _lock_acquire(&self->lock);

    if (self->fds[fd].file == NULL)
    {
        _lock_release(&self->lock);
        errno = EBADF;
        return -1;
    }

    int err = lfs_file_sync(&self->lfs, self->fds[fd].file);

    _lock_release(&self->lock);

    return map_lfs_error(err);
}


// ============================================================================
// LittleFlash global functions
// ============================================================================

//=============================================================
esp_err_t littleFlash_init(const little_flash_config_t *config)
{
    ESP_LOGV(TAG, "%s", __func__);

    _lock_init(&littleFlash.lock);

    littleFlash.open_files = config->open_files;
    littleFlash.part = config->part;

    memset(&littleFlash.lfs, 0, sizeof(lfs_t));
    littleFlash.sector_sz = SPI_FLASH_SEC_SIZE;
    littleFlash.block_cnt = littleFlash.part->size / littleFlash.sector_sz;

    littleFlash.lfs_cfg.read  = &internal_read;
    littleFlash.lfs_cfg.prog  = &internal_prog;
    littleFlash.lfs_cfg.erase = &internal_erase;
    littleFlash.lfs_cfg.sync  = &internal_sync;

    littleFlash.lfs_cfg.read_size   = littleFlash.sector_sz;
    littleFlash.lfs_cfg.prog_size   = littleFlash.sector_sz;
    littleFlash.lfs_cfg.block_size  = littleFlash.sector_sz;
    littleFlash.lfs_cfg.block_count = littleFlash.block_cnt;
    littleFlash.lfs_cfg.lookahead   = config->lookahead;
    littleFlash.lfs_cfg.context = (void *)&littleFlash;

    int err = lfs_mount(&littleFlash.lfs, &littleFlash.lfs_cfg);
    if (err < 0)
    {
        lfs_unmount(&littleFlash.lfs);
        if (!config->auto_format)
        {
            ESP_LOGE(TAG, "Error mounting, auto format not requested");
            return ESP_FAIL;
        }

        memset(&littleFlash.lfs, 0, sizeof(lfs_t));
        ESP_LOGW(TAG, "Error mounting, auto format requested");
        err = lfs_format(&littleFlash.lfs, &littleFlash.lfs_cfg);
        if (err < 0)
        {
            lfs_unmount(&littleFlash.lfs);
            ESP_LOGE(TAG, "Error formating - %d", err);
            return ESP_FAIL;
        }

        memset(&littleFlash.lfs, 0, sizeof(lfs_t));
        err = lfs_mount(&littleFlash.lfs, &littleFlash.lfs_cfg);
        if (err < 0)
        {
            lfs_unmount(&littleFlash.lfs);
            ESP_LOGE(TAG, "Error mounting after format");
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Formated and mounted");
    }
    littleFlash.mounted = true;

    littleFlash.fds = malloc(sizeof(vfs_fd_t) * littleFlash.open_files);
    if (littleFlash.fds == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < littleFlash.open_files; i++)
    {
    	littleFlash.fds[i].file = NULL;
    	littleFlash.fds[i].name = NULL;
    }

    esp_vfs_t vfs = {0};

    vfs.flags = ESP_VFS_FLAG_CONTEXT_PTR;
    vfs.write_p = &write_p;
    vfs.lseek_p = &lseek_p;
    vfs.read_p = &read_p;
    vfs.open_p = &open_p;
    vfs.close_p = &close_p;
    vfs.fstat_p = &fstat_p;
    vfs.stat_p = &stat_p;
    vfs.unlink_p = &unlink_p;
    vfs.rename_p = &rename_p;
    vfs.opendir_p = &opendir_p;
    vfs.readdir_p = &readdir_p;
    vfs.readdir_r_p = &readdir_r_p;
    vfs.telldir_p = &telldir_p;
    vfs.seekdir_p = &seekdir_p;
    vfs.closedir_p = &closedir_p;
    vfs.mkdir_p = &mkdir_p;
    vfs.rmdir_p = &rmdir_p;
    vfs.fsync_p = &fsync_p;

    esp_err_t esperr = esp_vfs_register(config->base_path, &vfs, &littleFlash);
    if (esperr != ESP_OK)
    {
        return err;
    }

    littleFlash.registered = true;

    return ESP_OK;
}

//================================================
void littleFlash_term(const char* partition_label)
{
    ESP_LOGV(TAG, "%s", __func__);

    if (littleFlash.registered)
    {
        for (int i = 0; i < littleFlash.open_files; i++)
        {
            if (littleFlash.fds[i].file)
            {
                close_p(&littleFlash, i);
                littleFlash.fds[i].file = NULL;
            }
        }

        esp_vfs_unregister(partition_label);
        littleFlash.registered = false;
    }

    if (littleFlash.fds)
    {
        free(littleFlash.fds);
        littleFlash.fds = NULL;
    }

    if (littleFlash.mounted)
    {
        lfs_unmount(&littleFlash.lfs);
        littleFlash.mounted = false;
    }

    _lock_close(&littleFlash.lock);
}

//--------------------------------------------
static int lfs_count(void *p, lfs_block_t b) {
    *(lfs_size_t *)p += 1;
    return 0;
}

//==================================
uint32_t littleFlash_getUsedBlocks()
{
	lfs_size_t in_use = 0;
	lfs_traverse(&littleFlash.lfs, lfs_count, &in_use);
	return in_use;
}

#endif
