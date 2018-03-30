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


#if !defined(_LITTLEFLASH_H_)
#define _LITTLEFLASH_H_ 1

#if CONFIG_MICROPY_FILESYSTEM_TYPE == 2

#include <sys/lock.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_partition.h"

#include "lfs.h"


typedef struct
{
	esp_partition_t *part;	// partition to be used
    char *base_path;		// mount point
    int open_files;			// number of open files to support
    bool auto_format;		// true=format if not valid
    lfs_size_t lookahead;	// number of LFS lookahead blocks
} little_flash_config_t;

typedef struct vfs_fd
{
	lfs_file_t *file;
    char *name;
} vfs_fd_t;

typedef struct {
	_lock_t lock;
	struct lfs_config lfs_cfg;	// littlefs configuration
	esp_partition_t *part;		// partition to be used
    int open_files;				// number of open files to support
	size_t sector_sz;			// sector size
	size_t block_cnt;			// block count
	lfs_t lfs;					// The littlefs type
	bool mounted;
	bool registered;
	vfs_fd_t *fds;
} littleFlash_t;

extern littleFlash_t littleFlash;

esp_err_t littleFlash_init(const little_flash_config_t *config);

void littleFlash_term();

uint32_t littleFlash_getUsedBlocks();

uint32_t littleFlash_trim(int max_blocks, int noerase);

#endif

#endif
