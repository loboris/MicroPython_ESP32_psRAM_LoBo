/*
 * Image creator for the littlefs
 *
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 * 
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 * 
 * The MIT License (MIT)
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

#include "lfs.h"
#include "lfs_util.h"

#include <stdio.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

//#include "wear_levelling.h"

static struct lfs_config config = {0};
static lfs_t lfs;
static uint8_t *lfs_image = NULL;

static uint32_t block_size = 0;
static uint32_t block_count = 0;
static uint32_t lookahead = 32;
static bool use_wl = false;
static char image_name[256] = {0};
static char image_dir[256] = {0};
static uint32_t fs_offset = 0;

static uint32_t *erase_log = NULL;
static uint32_t *prog_log = NULL;
static uint32_t *read_log = NULL;


// === Image access functions ===

//-------------------------------------------------------------------------------------------------------------
int lfs_img_read(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    memcpy((uint8_t *)buffer, lfs_image + fs_offset + ((off_t)block * cfg->block_size) + (off_t)off, (size_t)size);
    if (read_log) read_log[block]++;
    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
int lfs_img_prog(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    memcpy(lfs_image + fs_offset + ((off_t)block * cfg->block_size) + (off_t)off, (uint8_t *)buffer, (size_t)size);
    if (size != cfg->block_size) printf("SIZE=%d, BLOCK=%d, OFFSET=%d\r\n", size, block, off);
    if (prog_log) prog_log[block]++;
    return 0;
}

//----------------------------------------------------------------
int lfs_img_erase(const struct lfs_config *cfg, lfs_block_t block)
{
	memset(lfs_image + fs_offset + ((off_t)block * cfg->block_size), 0xFF, cfg->block_size);
    if (erase_log) erase_log[block]++;
    return 0;
}

//--------------------------------------------
int lfs_img_sync(const struct lfs_config *cfg)
{
    // do nothing
    return 0;
}

// === File functions ===================

//---------------------------------------
int addFile(char* name, const char* path)
{
    FILE* src = fopen(path, "rb");
    if (!src) {
        printf("error: failed to open '%s' for reading\r\n", path);
        return 1;
    }

    lfs_file_t *file = (lfs_file_t *) malloc(sizeof(lfs_file_t));
    if (file == NULL) {
        printf("error: failed to open lfs file '%s' for writting\r\n", name);
        return 2;
    }

    int lfs_flags = LFS_O_WRONLY | LFS_O_CREAT;
    int err = lfs_file_open(&lfs, file, name, lfs_flags);
    if (err < 0)
    {
        free(file);
        return 3;
    }

    // read file size
    fseek(src, 0, SEEK_END);
    size_t size = ftell(src);
    fseek(src, 0, SEEK_SET);

    size_t left = size;
    uint8_t data_byte;
    while (left > 0){
        if (1 != fread(&data_byte, 1, 1, src)) {
            printf("fread error!\r\n");

            fclose(src);
            lfs_file_close(&lfs, file);
            free(file);
            return 1;
        }
        lfs_ssize_t res = lfs_file_write(&lfs, file, &data_byte, 1);
        if (res < 0) {
            printf("SPIFFS_write error (%d)\r\n", res);

            fclose(src);
            lfs_file_close(&lfs, file);
            free(file);
            return 1;
        }
        left -= 1;
    }

    lfs_file_close(&lfs, file);
    free(file);

    return 0;
}

//--------------------
int addDir(char* name)
{
    int err = lfs_mkdir(&lfs, name);
    return err;
}

//----------------------------------------------------
int addFiles(const char* dirname, const char* subPath)
{
    DIR *dir;
    struct dirent *ent;
    bool error = false;
    char dirPath[512] = {0};
    char dirpath[512] = {0};
    char newSubPath[512] = {0};
    char filepath[512] = {0};
    char fullpath[512] = {0};

    sprintf(dirPath, "%s", dirname);
    strcat(dirPath, subPath);

    // Open directory
    if ((dir = opendir(dirPath)) != NULL) {
        // Read files from directory.
        while ((ent = readdir (dir)) != NULL) {

            // Ignore directory itself.
            if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0)) {
                continue;
            }

            sprintf(fullpath, "%s", dirPath);
            strcat(fullpath, ent->d_name);
            struct stat path_stat;
            stat(fullpath, &path_stat);

            if (!S_ISREG(path_stat.st_mode)) {
                // Check if path is a directory.
                if (S_ISDIR(path_stat.st_mode)) {
                    sprintf(dirpath, "%s", subPath);
                    strcat(dirpath, ent->d_name);
                    printf("%s [D]\r\n", dirpath);
                    int res = addDir(dirpath);
                    if (res != 0) {
                        printf("error adding directory (open)!\r\n");
                        error = true;
                        break;
                    }
                    // Prepare new sub path.
                    sprintf(newSubPath, "%s", subPath);
                    strcat(newSubPath, ent->d_name);
                    strcat(newSubPath, "/");

                    if (addFiles(dirname, newSubPath) != 0) {
                        printf("Error for adding content from '%s' !\r\n", ent->d_name);
                    }

                    continue;
                }
                else {
                    printf("skipping '%s'\r\n", ent->d_name);
                    continue;
                }
            }

            // File path with directory name as root folder.
            sprintf(filepath, "%s", subPath);
            strcat(filepath, ent->d_name);
            printf("%s\r\n", filepath);

            // Add File to image.
            if (addFile(filepath, fullpath) != 0) {
                printf("error adding file!\r\n");
                error = true;
                break;
            }
        } // end while
        closedir(dir);
    }
    else {
        printf("warning: can't read source directory\r\n");
        return 1;
    }

    return (error) ? 1 : 0;
}


//----------------------------------------
int lfs_img_create(struct lfs_config *cfg)
{
    lfs_image = malloc(cfg->block_size * cfg->block_count);
    if (lfs_image == NULL) return -1;

    for (int i=0; i<cfg->block_count; i++) {
        memset(lfs_image+(i*cfg->block_size), 0xFF, cfg->block_size);
    }

    // setup function pointers
    cfg->read  = lfs_img_read;
    cfg->prog  = lfs_img_prog;
    cfg->erase = lfs_img_erase;
    cfg->sync  = lfs_img_sync;

    return 0;
}

//----------------------
int lfs_img_format(void)
{
    int err = lfs_img_create(&config);
    if (err) {
        return err;
    }

    err = lfs_format(&lfs, &config);

    return err;
}

//------------------
int save_image(void)
{
    FILE* img_file = fopen(image_name, "wb");
    if (!img_file) {
        printf("error: failed to open '%s'\r\n", image_name);
        return 1;
    }
    fwrite(lfs_image, 1, block_size * block_count, img_file);
    fclose(img_file);

    return 0;
}

//---------------------
int lfs_img_mount(void)
{
    int err = 0;

    if (use_wl) fs_offset = 4096;
    else fs_offset = 0;

    config.lookahead = lookahead;
    config.block_count = block_count;
    config.block_size = block_size;
    config.prog_size = block_size;
    config.read_size = block_size;

    err = lfs_img_create(&config);
    if (err) {
        printf("Error creating image (%d)\r\n", err);
        return err;
    }

    err = lfs_format(&lfs, &config);
    if (err) {
        printf("Error formating image (%d)\r\n", err);
        return err;
    }

    err = lfs_mount(&lfs, &config);
    if (err) {
        printf("Error mounting image (%d)\r\n", err);
        return err;
    }
    return 0;
}

//------------------------
int lfs_create_image(void)
{
    int err = 0;

    err = lfs_img_mount();
    if (err) return err;

    printf("\r\nAdding files from image directory:\r\n");
    printf("  '%s'\r\n", image_dir);
    printf("----------------------------------\r\n\r\n");
    addFiles(image_dir, "/");
    printf("\r\n");

    err = lfs_unmount(&lfs);
    if (err) {
        printf("Error unmounting image (%d)\r\n", err);
    }

    save_image();

    return 0;
}


//===============================
int main(int argc, char **argv) {
    // parse options
    int c;
    char *cvalue = NULL;
    char *ptr;

    printf("\r\n");
    while ( (c = getopt(argc, argv, "b:c:l:wT")) != -1) {
        switch (c) {
        case 'b':
            cvalue = optarg;
            block_size = (uint32_t)strtol(cvalue, &ptr, 10);
            break;
        case 'c':
            cvalue = optarg;
            block_count = (uint32_t)strtol(cvalue, &ptr, 10);
            break;
        case 'l':
            cvalue = optarg;
            lookahead = (uint32_t)strtol(cvalue, &ptr, 10);
            break;
        case 'w':
            use_wl = true;
            break;
        case '?':
            break;
        default:
            printf ("?? getopt returned character code 0%o ??\r\n", c);
        }
    }

    if (argc < 2) {
        printf("Error: image directory and image name arguments are mandatory\r\n");
        printf("\r\n");
        return 1;
    }

    sprintf(image_dir, "%s", argv[optind]);
    sprintf(image_name, "%s", argv[optind+1]);

    printf("Creating LittleFS image\r\n");
    printf("=======================\r\n");
    printf("Image directory:\r\n  '%s'\r\n", image_dir);
    printf("Image name:\r\n  '%s'\r\n", image_name);
    printf("Block size=%u, Block count=%u, lookahead=%u, use wear leveling: %s\r\n", block_size, block_count, lookahead, use_wl ? "True" : "False");

    int err = lfs_create_image();
    printf("=======================\r\n");
    printf("\r\n");
    
    return err;
}
