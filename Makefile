#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := firmware

# ##########################################################################
# Variables for creating/flashing spiffs image file
ifeq ($(OS),Windows_NT)
	MKSPIFFS_BIN="mkspiffs.exe"
	MKFATFS_BIN="mkfatfs.exe"
else
	MKSPIFFS_BIN="mkspiffs"
	MKFATFS_BIN="mkfatfs"
endif
FILESYS_SIZE = $(shell echo $$(( $(CONFIG_MICROPY_INTERNALFS_SIZE) * 1024 )))
INTERNALFS_IMAGE_COMPONENT_PATH := $(PWD)/components/internalfs_image
# ##########################################################################


echo_flash_cmd:
	@echo "esptool.py --chip esp32 --port $(CONFIG_ESPTOOLPY_PORT) --baud $(CONFIG_ESPTOOLPY_BAUD) --before $(CONFIG_ESPTOOLPY_BEFORE) --after $(CONFIG_ESPTOOLPY_AFTER) write_flash $(if $(CONFIG_ESPTOOLPY_COMPRESSED),-z,-u) $(ESPTOOL_WRITE_FLASH_OPTIONS) 0x1000 bootloader/bootloader.bin 0xf000 phy_init_data.bin 0x10000 MicroPython.bin 0x8000 partitions_mpy.bin"

makefs:
	@echo "Making spiffs image; Flash address: $(CONFIG_MICROPY_INTERNALFS_START), Size: $(CONFIG_MICROPY_INTERNALFS_SIZE) KB ..."
	$(PROJECT_PATH)/components/mkspiffs/$(MKSPIFFS_BIN) -c $(INTERNALFS_IMAGE_COMPONENT_PATH)/image -b 4096 -p 256 -s $(FILESYS_SIZE) $(BUILD_DIR_BASE)/spiffs_image.img
	@echo "--------------------------"
	@echo "To flash to ESP32 execute:"
	@echo "--------------------------"
	@echo "$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(BUILD_DIR_BASE)/spiffs_image.img)"
	@echo "-----------------------------"
	@echo "or execute ./BUILD.sh flashfs"
	@echo "-----------------------------"

flashfs:
	@echo "Making spiffs image; Flash address: $(CONFIG_MICROPY_INTERNALFS_START), Size: $(CONFIG_MICROPY_INTERNALFS_SIZE) KB ..."
	$(PROJECT_PATH)/components/mkspiffs/$(MKSPIFFS_BIN) -c $(INTERNALFS_IMAGE_COMPONENT_PATH)/image -b 4096 -p 256 -s $(FILESYS_SIZE) $(BUILD_DIR_BASE)/spiffs_image.img
	@echo "----------------------"
	@echo "Flashing the image ..."
	@echo "----------------------"
	$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(BUILD_DIR_BASE)/spiffs_image.img
	@echo "----------------------"

copyfs: 
	@echo "-----------------------------"
	@echo "Flashing default spiffs image ..."
	@echo "-----------------------------"
	@echo "$(INTERNALFS_IMAGE_COMPONENT_PATH)/spiffs_image.img"
	@echo "-----------------------------"
	$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(INTERNALFS_IMAGE_COMPONENT_PATH)/spiffs_image.img

makefatfs:
	@echo "Making fatfs image; Flash address: $(CONFIG_MICROPY_INTERNALFS_START), Size: $(CONFIG_MICROPY_INTERNALFS_SIZE) KB ..."
	@echo "$(ESPTOOLPY_WRITE_FLASH)"
	$(PROJECT_PATH)/components/mkfatfs/src/$(MKFATFS_BIN) -c $(INTERNALFS_IMAGE_COMPONENT_PATH)/image -s $(FILESYS_SIZE) $(BUILD_DIR_BASE)/fatfs_image.img -d 2
	@echo "--------------------------"
	@echo "To flash to ESP32 execute:"
	@echo "--------------------------"
	@echo "$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(BUILD_DIR_BASE)/fatfs_image.img)"
	@echo "--------------------------------"
	@echo "or execute ./BUILD.sh flashfatfs"
	@echo "--------------------------------"

flashfatfs:
	@echo "Making fatfs image; Flash address: $(CONFIG_MICROPY_INTERNALFS_START), Size: $(CONFIG_MICROPY_INTERNALFS_SIZE) KB ..."
	@echo "$(ESPTOOLPY_WRITE_FLASH)"
	$(PROJECT_PATH)/components/mkfatfs/src/$(MKFATFS_BIN) -c $(INTERNALFS_IMAGE_COMPONENT_PATH)/image -s $(FILESYS_SIZE) $(BUILD_DIR_BASE)/fatfs_image.img -d 2
	@echo "----------------------"
	@echo "Flashing the image ..."
	@echo "----------------------"
	$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(BUILD_DIR_BASE)/fatfs_image.img
	@echo "----------------------"

copyfatfs: 
	@echo "----------------------------"
	@echo "Flashing default fatfs image ..."
	@echo "----------------------------"
	@echo "$(INTERNALFS_IMAGE_COMPONENT_PATH)/fatfs_image.img"
	@echo "-----------------------------"
	$(ESPTOOLPY_WRITE_FLASH) $(CONFIG_MICROPY_INTERNALFS_START) $(INTERNALFS_IMAGE_COMPONENT_PATH)/fatfs_image.img

otafs:
	@echo "----------------------------"
	@echo "Sending the image ..."
	@echo "----------------------------"
	@echo "$(INTERNALFS_IMAGE_COMPONENT_PATH)/spiffs_image.img"
	@echo "-----------------------------"
	curl ${ESP32_IP}:8032 --data-binary @- < $(BUILD_DIR_BASE)/spiffs_image.img

otafatfs:
	@echo "----------------------------"
	@echo "Sending the image ..."
	@echo "----------------------------"
	@echo "$(INTERNALFS_IMAGE_COMPONENT_PATH)/fatfs_image.img"
	@echo "-----------------------------"
	curl ${ESP32_IP}:8032 --data-binary @- < $(BUILD_DIR_BASE)/fatfs_image.img

include $(IDF_PATH)/make/project.mk
