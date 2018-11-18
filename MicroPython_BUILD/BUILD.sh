#!/bin/bash

# #################################################################
# This script makes it easy to build MicroPython firmware for ESP32
# #################################################################

# Usage:
#   ./BUILD.sh [<options>] [<command> ... <command>]

# Commands:
#                     - run the build, create MicroPython firmware
#   all               - run the build, create MicroPython firmware
#   menuconfig        - run menuconfig to configure ESP32/MicroPython
#   clean             - clean the build
#   flash             - flash MicroPython firmware to ESP32
#   erase             - erase the whole ESP32 Flash
#   makefs            - create spiffs image
#   flashfs           - create and flash spiffs image to ESP32
#   copyfs            - flash prebuilt spiffs image to ESP32
#   makefatfs         - create FatFS image
#   flashfatfs        - create and flash FatFS image to ESP32
#   copyfatfs         - flash prebuilt FatFS image to ESP32
#   makelfsfs         - create LittleFS image
#   flashlfsfs        - create and flash LittleFS image to ESP32
#   copylfsfs         - flash prebuilt LittleFS image to ESP32
#   size              - display static memory footprint of the firmware
#   size-components   - display detailed memory footprint of the firmware
#   size-files        - display detailed memory footprint of the firmware
#   miniterm          - start pySerial command line program miniterm
#   monitor           - start esp-idf terminal emulator

# Options:
#   -jN                                           - make with multicore option, N should be the number of cores used 
#   -v             | --verbose                    - enable verbose output, default: quiet output
#   -f8            | --flashsize8                 - declare the Flash size of 8 MB
#   -f16           | --flashsize16                - declare the Flash size of 16 MB
#   -fs <FS_size>  | --fssize=<FS_size>           - declare the size of Flash file system in KB
#                                                   default: fit the Flash size
#   -a <app_size>  | --appsize=<app_size>         - declare the size of application partition in KB
#                                                   default: auto detect needed size
#                                                   the actual size will be 128 KB smaller then the declared size
#   -p <comm_port> | --port=<comm_port>           - overwritte configured comm port, use the specified instead
#   -b <bdrate>    | --bdrate=<bdrate>            - overwritte configured baud rate, use the specified instead

# Note:
#   Multiple options and commands can be given


# #################################################################
# Author: Boris Lovosevic; https://github.com/loboris; 11/2017
# #################################################################


#=======================
TOOLS_VER=ver20180827.id
BUILD_VER=ver20180904.id
#=======================

# -----------------------------
# --- Set default variables ---
# -----------------------------
J_OPTION=""
FLASH_SIZE=4
FS_SIZE=0
APP_SIZE=0
MP_SHOW_PROGRESS=no
FORCE_2PART="no"
FORCE_3PART="no"
POSITIONAL_ARGS=()
BUILD_TYPE=""
BUILD_BASE_DIR=${PWD}
BUILD_COMPORT=""
BUILD_BDRATE=""

# ---------------------------------------
# Include functions used in build process
# ---------------------------------------
. "build_func.sh"
# ---------------------------------------


# --------------------------------------
# --- Get build commands and options ---
# --------------------------------------
get_arguments "$@"

n_args=${#POSITIONAL_ARGS[@]}
if [ ${n_args} -eq 0 ]; then
    POSITIONAL_ARGS+=("all")
fi

# ----------------------
# Check Operating System
# ----------------------
check_OS
if [ $? -ne 0 ]; then
    exit 1
fi

# Goto base repository directory
cd ../

# -----------------------------------
# --- Check the build environment ---
# -----------------------------------
check_Environment
if [ $? -ne 0 ]; then
    exit 1
fi

# return to base build directory
cd ${BUILD_BASE_DIR}

# Original OS PATH is used for building
# mpycross, mkspiffs and mkfatfs
export orig_PATH=${PATH}
# ----------------------------------
# --- SET XTENSA & ESP-IDF PATHS ---
# ----------------------------------
cd ../
# Add Xtensa toolchain path to system path, and export path to esp-idf
export xtensa_PATH=${PWD}/Tools/xtensa-esp32-elf/bin:$PATH
export PATH=${xtensa_PATH}
export IDF_PATH=${PWD}/Tools/esp-idf

export HOST_PLATFORM=${machine}
export CROSS_COMPILE=xtensa-esp32-elf-

cd ${BUILD_BASE_DIR}

# -----------------------------
# Create partitions layout file
# -----------------------------

if [ -f "sdkconfig" ]; then
    SDK_PSRAM=$(grep -e CONFIG_SPIRAM_SUPPORT=y sdkconfig)
    if [ "${SDK_PSRAM}" == "CONFIG_SPIRAM_SUPPORT=y" ]; then
        BUILD_TYPE=" with psRAM support"
    fi
else
    if [ -f _sdkconfig.saved ]; then
        cp -f _sdkconfig.saved sdkconfig > /dev/null 2>&1
        rm -f _sdkconfig.saved > /dev/null 2>&1
        echo ""
        echo "** Restored 'sdkconfig' **'"
        echo ""
    fi
    make menuconfig 2>/dev/null

    if [ -f "sdkconfig" ]; then
        SDK_PSRAM=$(grep -e CONFIG_SPIRAM_SUPPORT=y sdkconfig)
        if [ "${SDK_PSRAM}" == "CONFIG_SPIRAM_SUPPORT=y" ]; then
            BUILD_TYPE=" with psRAM support"
        fi
    else
        echo ""
        echo "==========================================="
        echo "Error creatimg 'sdkconfig', cannot continue"
        echo "==========================================="
        echo ""
    fi
fi

echo ""
if [ "${BUILD_TYPE}" == "" ]; then
    echo "---------------------"
    echo "MicroPython for ESP32"
    echo "---------------------"
else
    echo "----------------------------------------"
    echo "MicroPython for ESP32 with psRAM support"
    echo "----------------------------------------"
fi
echo ""


export PATH=${xtensa_PATH}
export CROSS_COMPILE=xtensa-esp32-elf-


# ===================================
# ==== Execute the build command ====
# ===================================

for arg in "${POSITIONAL_ARGS[@]}"
do
    if [ "${arg}" == "menuconfig" ]; then
        check_config
        result=$?
        if [ $result -eq 1 ]; then
            make menuconfig 2>/dev/null
        fi
    fi
    if [ "${arg}" == "all" ] || [ "${arg}" == "flash" ] || [ "${arg}" == "makefs" ] || [ "${arg}" == "flashfs" ] || [ "${arg}" == "makefatfs" ] || [ "${arg}" == "flashfatfs" ] || [ "${arg}" == "makelfsfs" ] || [ "${arg}" == "flashlfsfs" ]; then
        set_partitions ${APP_SIZE}
        if [ $? -ne 0 ]; then
            exit 1
        fi
        if [ "${arg}" == "all" ] || [ "${arg}" == "flash" ]; then
            check_config
        fi
    fi

    if [ "${arg}" == "flash" ]; then
        if [ ! -f "build/MicroPython.bin" ]; then
            echo "'build/MicroPython.bin' not found"
            echo "The firmware must be built before flashing!"
            echo ""
            exit 1
        fi
    fi

    executeCommand
    result=$?

    if [ $result -ne 0 ] && [ "${arg}" == "all" ] && [ "${J_OPTION}" != "" ]; then
        echo "Restarting build ..."
        sleep 2
        if [ "${MP_SHOW_PROGRESS}" == "yes" ]; then
            make ${J_OPTION} ${arg}
        else
            make ${J_OPTION} ${arg} > /dev/null 2>&1
        fi
        result=$?
    fi

    if [ $result -eq 0 ]; then
        echo "OK."
        if [ "${arg}" == "all" ]; then
            set_partitions ${APP_SIZE}
            echo "--------------------------------"
            echo "Build complete."
            echo "You can now run ./BUILD.sh flash"
            echo "to deploy the firmware to ESP32"
            echo "--------------------------------"
        fi
        echo ""
    else
        echo "'make ${arg}' FAILED!"
        echo ""
        exit 1
    fi

done

