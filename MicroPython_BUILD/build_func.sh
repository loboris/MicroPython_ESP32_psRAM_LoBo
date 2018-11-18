
fs_type_fat="no"
fs_fat_sect=4096
fs_use_wl=""
linplatform=""
linprocessor=""
linmachine=""

#----------------
get_arguments() {
    POSITIONAL_ARGS=()
    local key="$1"
    J_OPTION=""

    while [[ $# -gt 0 ]]
    do
    local key="$1"
    case $key in
        -v|--verbose)
        export MP_SHOW_PROGRESS="yes"
        shift # past argument
        ;;
        -f8|--flashsize8)
        FLASH_SIZE="8"
        shift # past argument
        ;;
        -f16|--flashsize16)
        FLASH_SIZE="16"
        shift # past argument
        ;;
        -fs|--fssize)
        FS_SIZE="$2"
        shift # past argument
        shift # past value
        ;;
        -p|--port)
        BUILD_COMPORT="$2"
        BUILD_COMPORT=" ESPPORT=${BUILD_COMPORT}"

        shift # past argument
        shift # past value
        ;;
        -b|--bdrate)
        BUILD_BDRATE="$2"
        BUILD_BDRATE=" ESPBAUD=${BUILD_BDRATE}"

        shift # past argument
        shift # past value
        ;;
        -a|--appsize)
        APP_SIZE="$2"
        APP_SIZE=$(( ${APP_SIZE} - 128 ));

        shift # past argument
        shift # past value
        ;;
        --force2p)
        FORCE_2PART="yes"
        shift # past argument
        ;;
        --force3p)
        FORCE_3PART="yes"
        shift # past argument
        ;;
        *)    # unknown option
        local opt="$1"
        if [ "${opt:0:2}" == "-j" ]; then
            J_OPTION=${opt}
        else
            POSITIONAL_ARGS+=("$1") # save it in an array for later
        fi
        shift # past argument
        ;;
    esac
    done
    #set -- "${POSITIONAL_ARGS[@]}" # restore positional parameters
}


#---------------
get_bin_size() {
    if [ -f "build/MicroPython.bin" ]; then
        BIN_FILE_SIZE=$(wc -c < build/MicroPython.bin)
    else
        BIN_FILE_SIZE=0
    fi
}


#----------------------
get_config_flash_sz() {
    local cfg_fsfat=$(grep -e CONFIG_LITTLEFLASH_USE_WEAR_LEVELING=y sdkconfig)
    if [ "${cfg_fsfat}" != "" ]; then
        fs_use_wl="-w"
    fi

    cfg_fsfat=$(grep -e CONFIG_MICROPY_FILESYSTEM_TYPE=1 sdkconfig)
    if [ "${cfg_fsfat}" == "CONFIG_MICROPY_FILESYSTEM_TYPE=1" ]; then
        # FatFS
        fs_type_fat="yes"
        cfg_fsfat=$(grep -e CONFIG_WL_SECTOR_SIZE_512=y sdkconfig)
        if [ "${cfg_fsfat}" == "CONFIG_WL_SECTOR_SIZE_512=y" ]; then
            fs_fat_sect=512
        fi
    else
        cfg_fsfat=$(grep -e CONFIG_MICROPY_FILESYSTEM_TYPE=2 sdkconfig)
        if [ "${cfg_fsfat}" == "CONFIG_MICROPY_FILESYSTEM_TYPE=2" ]; then
            # LittleFS
            fs_type_fat="yes"
            cfg_fsfat=$(grep -e CONFIG_WL_SECTOR_SIZE_512=y sdkconfig)
            if [ "${cfg_fsfat}" == "CONFIG_WL_SECTOR_SIZE_512=y" ]; then
                if [ "${fs_use_wl}" == "-w" ]; then
                    fs_fat_sect=512
                fi
            fi
        fi
    fi

    if [ ${FLASH_SIZE} -eq 4 ]; then
        # Flash size was not set by options, check config file
        local cfg_flashsz=$(grep -e CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y sdkconfig)
        if [ "${cgf_flashsz}" == "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" ]; then
            FLASH_SIZE="8"
            return
        fi
        cfg_flashsz=$(grep -e CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y sdkconfig)
        if [ "${cgf_flashsz}" == "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y" ]; then
            FLASH_SIZE="16"
        fi
    fi
}

#---------------
check_config() {
    # If BT is enabled, check and modify, if needed, some configuration
    local TMPVAR1=""
    local USE_RFCOMM=$(grep -e CONFIG_MICROPY_USE_RFCOMM=y sdkconfig)
    local USE_BT=$(grep -e CONFIG_MICROPY_USE_BLUETOOTH=y sdkconfig)

    if [ "${USE_RFCOMM}" == "CONFIG_MICROPY_USE_RFCOMM=y" ] || [ "${USE_BT}" == "CONFIG_MICROPY_USE_BLUETOOTH=y" ]; then
        # change to RELEASE optimization level to decrease IRAM usage
        TMPVAR1=$(grep -e CONFIG_OPTIMIZATION_LEVEL_DEBUG=y sdkconfig)
        if [ "${TMPVAR1}" == "CONFIG_OPTIMIZATION_LEVEL_DEBUG=y" ]; then
            sed --in-place='.bak1' 's/CONFIG_OPTIMIZATION_LEVEL_DEBUG=y/CONFIG_OPTIMIZATION_LEVEL_DEBUG=/g' sdkconfig
            sed --in-place='.bak2' 's/CONFIG_OPTIMIZATION_LEVEL_RELEASE=/CONFIG_OPTIMIZATION_LEVEL_RELEASE=y/g' sdkconfig
            rm -f sdkconfig.bak1 > /dev/null 2>&1
            rm -f sdkconfig.bak2 > /dev/null 2>&1
            return 1
        fi
   fi
   return 0
}

#-----------------
set_partitions() {
    get_bin_size
    get_config_flash_sz

    local size=$(( (($1 / 64) + 1) * 64 ))
    local fs_size=256
    local flash_sz=$(( $FLASH_SIZE * 1024 ))
    local fbin_size=$(( ($BIN_FILE_SIZE / 1024) + 1 ))
    local min_fs_size=256
    local PART_SUB_TYPE="spiffs," 
    if [ "${fs_type_fat}" == "yes" ]; then
        PART_SUB_TYPE="fat,   " 
        if [ ${fs_fat_sect} -eq 4096 ]; then
            min_fs_size=512
        fi
    fi

    # === Create partitions configuration file ===
    local USE_OTA_PART=$(grep -e CONFIG_MICROPY_USE_OTA=y sdkconfig)
    if [ "${USE_OTA_PART}" == "CONFIG_MICROPY_USE_OTA=y" ]; then
        # --- OTA partition layout is used ---
        local part_lay3="no"
        if [ ${FORCE_2PART} != "yes" ]; then
            if [ ${flash_sz} -gt 8000 ] || [ ${FORCE_3PART} == "yes" ]; then
                part_lay3="yes"
            fi
        fi

        if [ ${size} -le 64 ]; then size=1024; fi
        if [ ${fbin_size} -gt ${size} ]; then
            size=$(( (($fbin_size / 64) + 1) * 64 ));
        fi
        if [ ${part_lay3} == "yes" ]; then
            fs_size=$(( $flash_sz - $size - $size - $size - 64))
            fs_start=$(( (64 + $size + $size + $size) * 1024 ))
        else
            fs_size=$(( $flash_sz - $size - $size - 64))
            fs_start=$(( (64 + $size + $size) * 1024 ))
        fi
        if [ ${fs_size} -lt ${min_fs_size} ]; then
            local nspc=""
            if [ ${FLASH_SIZE} -lt 5 ]; then nspc=" "; fi
            echo "==========================================="
            echo "== Firmware does not fit in ${FLASH_SIZE}MB Flash${nspc}   =="
            if [ ${min_fs_size} -eq 512 ]; then
                echo "== Min FatFS size (4096 sect size): 512K =="
            fi
            echo "==========================================="
            echo ""
            return 1
        fi
        if [ ${FS_SIZE} -gt 256 ]; then
            if [ ${fs_size} -gt ${FS_SIZE} ]; then
                fs_size=${FS_SIZE}
            fi
        fi
        
        echo "# -------------------------------------------------------"  > partitions_mpy.csv
        echo "# -    Partition layout generated by BUILD.sh script    -" >> partitions_mpy.csv
        echo "# -------------------------------------------------------" >> partitions_mpy.csv
        echo "# Name,         Type, SubType, Offset,  Size,       Flags" >> partitions_mpy.csv
        echo "# -------------------------------------------------------" >> partitions_mpy.csv
        echo "nvs,            data, nvs,     0x9000,  16K,"         >> partitions_mpy.csv
        echo "otadata,        data, ota,     0xd000,  8K,"          >> partitions_mpy.csv
        echo "phy_init,       data, phy,     0xf000,  4K,"          >> partitions_mpy.csv
        if [ ${part_lay3} == "yes" ]; then
        echo "MicroPython,    app,  factory, 0x10000, ${size}K,"    >> partitions_mpy.csv
        echo "MicroPython_1,  app,  ota_0,   ,        ${size}K,"    >> partitions_mpy.csv
        else
        echo "MicroPython_1,  app,  ota_0,   0x10000, ${size}K,"    >> partitions_mpy.csv
        fi
        echo "MicroPython_2,  app,  ota_1,   ,        ${size}K,"    >> partitions_mpy.csv
        echo "internalfs,     data, ${PART_SUB_TYPE}  ,        ${fs_size}K," >> partitions_mpy.csv
    else
        # --- Single partition layout is used ---
        if [ ${size} -le 64 ]; then size=1024; fi
        if [ ${fbin_size} -gt ${size} ]; then
            size=$(( (($fbin_size / 64) + 1) * 64 ));
        fi

        fs_size=$(( $flash_sz - $size - 64))
        fs_start=$(( (64 + $size ) * 1024 ))
        if [ ${fs_size} -lt ${min_fs_size} ]; then
            local nspc=""
            if [ ${FLASH_SIZE} -lt 5 ]; then nspc=" "; fi
            echo "==========================================="
            echo "== Firmware does not fit in ${FLASH_SIZE}MB Flash${nspc}   =="
            if [ ${min_fs_size} -eq 512 ]; then
                echo "== Min FatFS size (4096 sect size): 512K =="
            fi
            echo "==========================================="
            echo ""
            return 1
        fi
        if [ ${FS_SIZE} -gt 256 ]; then
            if [ ${fs_size} -gt ${FS_SIZE} ]; then
                fs_size=${FS_SIZE}
            fi
        fi

        echo "# -------------------------------------------------------"  > partitions_mpy.csv
        echo "# -    Partition layout generated by BUILD.sh script    -" >> partitions_mpy.csv
        echo "# -------------------------------------------------------" >> partitions_mpy.csv
        echo "# Name,         Type, SubType, Offset,  Size,       Flags" >> partitions_mpy.csv
        echo "# -------------------------------------------------------" >> partitions_mpy.csv
        echo "nvs,            data, nvs,     0x9000,  24K,"         >> partitions_mpy.csv
        echo "phy_init,       data, phy,     0xf000,  4K,"          >> partitions_mpy.csv
        echo "MicroPython,    app,  factory, 0x10000, ${size}K,"    >> partitions_mpy.csv
        echo "internalfs,     data, ${PART_SUB_TYPE}  ,        ${fs_size}K," >> partitions_mpy.csv
    fi
    fs_start_hex=$(printf "%x\n" $fs_start)
    local fs_sector_count=$(( ($fs_size * 1024) / $fs_fat_sect ))

    export CONFIG_MICROPY_INTERNALFS_START="0x${fs_start_hex}"
    export CONFIG_MICROPY_INTERNALFS_SIZE=${fs_size}
    export CONFIG_MICROPY_BLOCK_SIZE=${fs_fat_sect}
    export CONFIG_MICROPY_BLOCK_COUNT=${fs_sector_count}
    export CONFIG_MICROPY_USE_WL=${fs_use_wl}
    return 0
}

# ----------------------
# Check Operating System
# ----------------------
check_OS() {
    local unameOut="$(uname -s)"
    case "${unameOut}" in
        Linux*)     machine=Linux;;
        Darwin*)    machine=MacOS;;
        CYGWIN*)    machine=Win;;
        MINGW*)     machine=Win;;
        *)          machine=UNKNOWN
    esac

    if [ "${machine}" == "UNKNOWN" ]; then
        if [ "${machine:0:5}" == "MINGW" ]; then
            machine=Win
        fi
    fi

    if [ "${machine}" == "UNKNOWN" ]; then
        echo ""
        echo "Unsupported OS detected."
        echo ""
        return 1
    fi

    if [ "${machine}" == "Win" ]; then
        if [ ! -f "/usr/bin/tar.exe" ]; then
            echo "Installing tar..."
            pacman -S --noconfirm tar > /dev/null 2>&1
            if [ $? -eq 0 ]; then
                echo "OK."
            else
                echo "FAILED"
                return 1
            fi
        fi
        if [ ! -f "/usr/bin/tar.exe" ]; then
            echo ""
            echo "'tar.exe' needed for toolchain extraction not found!"
            echo ""
            return 1
        fi
        MK_SPIFFS_BIN="mkspiffs.exe"
        MK_FATFS_BIN="mkfatfs.exe"
        MK_LITTLEFS_BIN="mklfs.exe"
    else
	    if [ "${machine}" == "Linux" ]; then
		    linplatform="$(uname -i)"
		    linprocessor="$(uname -p)"
		    linmachine="$(uname -m)"
		fi
        MK_SPIFFS_BIN="mkspiffs"
        MK_FATFS_BIN="mkfatfs"
        MK_LITTLEFS_BIN="mklfs"
    fi
    return 0
}


# =================================================
# Test if toolchains are unpacked and right version
# Clean directories
# =================================================
#--------------------
check_Environment() {
    # --------------------------------------------------
    # Remove directories from early MicroPython versions
    # --------------------------------------------------
    if [ -d "esp-idf" ]; then
        rm -rf esp-idf/ > /dev/null 2>&1
        rmdir esp-idf > /dev/null 2>&1
    fi
    if [ -d "esp-idf_psram" ]; then
        rm -rf esp-idf_psram/ > /dev/null 2>&1
        rmdir esp-idf_psram > /dev/null 2>&1
    fi
    if [ -d "xtensa-esp32-elf" ]; then
        rm -rf xtensa-esp32-elf/ > /dev/null 2>&1
        rmdir xtensa-esp32-elf > /dev/null 2>&1
    fi
    if [ -d "xtensa-esp32-elf_psram" ]; then
        rm -rf xtensa-esp32-elf_psram/ > /dev/null 2>&1
        rmdir xtensa-esp32-elf_psram > /dev/null 2>&1
    fi


    cd Tools
    # -----------------------------------------
    # _psram directories are not needed anymore
    # -----------------------------------------
    if [ -d "esp-idf_psram" ]; then
        rm -rf esp-idf_psram/ > /dev/null 2>&1
        rmdir esp-idf_psram > /dev/null 2>&1
    fi
    if [ -d "xtensa-esp32-elf_psram" ]; then
        rm -rf xtensa-esp32-elf_psram/ > /dev/null 2>&1
        rmdir xtensa-esp32-elf_psram > /dev/null 2>&1
    fi


    # ------------------------------------------
    # Check esp-idf and xtensa toolchain version
    # ------------------------------------------
    if [ ! -f "${TOOLS_VER}" ]; then
        echo "Removing old tools versions and cleaning build..."
        # Remove directories from previous version
        if [ -d "esp-idf" ]; then
            rm -rf esp-idf/ > /dev/null 2>&1
            rmdir esp-idf > /dev/null 2>&1
        fi
        if [ -d "xtensa-esp32-elf" ]; then
            rm -rf xtensa-esp32-elf/ > /dev/null 2>&1
            rmdir xtensa-esp32-elf > /dev/null 2>&1
        fi
        rm -f *.id > /dev/null 2>&1
        touch ${TOOLS_VER}
        echo "toolchains & esp-idf version" > ${TOOLS_VER}
        rm -f ${BUILD_BASE_DIR}/*.id > /dev/null 2>&1
        touch ${BUILD_BASE_DIR}/${BUILD_VER}
        echo "Build ID, do not delete this file" > ${BUILD_BASE_DIR}/${BUILD_VER}
        # remove executables
        rm -f ${BUILD_BASE_DIR}/components/mpy_cross_build/mpy-cross/mpy-cross > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mpy_cross_build/mpy-cross/mpy-cross.exe > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/micropython/mpy-cross/mpy-cross > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/micropython/mpy-cross/mpy-cross.exe > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mkspiffs/src/mkspiffs > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mkspiffs/src/mkspiffs.exe > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mkfatfs/src/mkfatfs > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mkfatfs/src/mkfatfs.exe > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mklittlefs/mklfs > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/components/mklittlefs/mklfs.exe > /dev/null 2>&1
        # remove build directory and configs
        cp -f ${BUILD_BASE_DIR}/sdkconfig ${BUILD_BASE_DIR}/_sdkconfig.saved > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/sdkconfig > /dev/null 2>&1
        rm -f ${BUILD_BASE_DIR}/sdkconfig.old > /dev/null 2>&1
        rm -rf ${BUILD_BASE_DIR}/build/ > /dev/null 2>&1
        rmdir ${BUILD_BASE_DIR}/build > /dev/null 2>&1
    else
        # -----------------------
        # Check the build version
        # -----------------------
        if [ ! -f "${BUILD_BASE_DIR}/${BUILD_VER}" ]; then
            echo "Build updated, running menuconfig is needed..."
            rm -f ${BUILD_BASE_DIR}/*.id > /dev/null 2>&1
            touch ${BUILD_BASE_DIR}/${BUILD_VER}
            echo "Build ID, do not delete this file" > ${BUILD_BASE_DIR}/${BUILD_VER}
            # remove build directory and configs
            cp -f ${BUILD_BASE_DIR}/sdkconfig ${BUILD_BASE_DIR}/_sdkconfig.saved > /dev/null 2>&1
            rm -f ${BUILD_BASE_DIR}/sdkconfig > /dev/null 2>&1
            rm -f ${BUILD_BASE_DIR}/sdkconfig.old > /dev/null 2>&1
            rm -rf ${BUILD_BASE_DIR}/build/ > /dev/null 2>&1
            rmdir ${BUILD_BASE_DIR}/build > /dev/null 2>&1
        fi
    fi

    if [ ! -d "esp-idf" ]; then
        echo "unpacking 'esp-idf'"
        tar -xf esp-idf.tar.xz > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "unpacking 'esp-idf' FAILED"
            if [ "${machine}" == "Win" ]; then
                echo "On Windows, it may be a false error, run './BUILD.sh menuconfig' again!"
            fi
            return 1
        fi
    fi
    if [ ! -d "xtensa-esp32-elf" ]; then
        if [ "${machine}" == "Linux" ]; then
            if [ "${linplatform}" == "i686" ]; then
                echo "unpacking ${machine}/xtensa-esp32-elf (32bit)"
                tar -xf ${machine}/xtensa-esp32-elf_32bit.tar.xz > /dev/null 2>&1
            elif [ "${linplatform}" == "armv7l" ] || [ "${linprocessor}" == "armv7l" ] || [ "${linmachine}" == "armv7l" ]; then
                echo "unpacking ${machine}/xtensa-esp32-elf (arm)"
                tar -xf ${machine}/xtensa-esp32-elf_arm.tar.xz > /dev/null 2>&1
            elif [ "${linmachine}" == "aarch64" ]; then
                echo "unpacking ${machine}/xtensa-esp32-elf (aarch64)"
                tar -xf ${machine}/xtensa-esp32-elf_aarch64.tar.xz > /dev/null 2>&1
            else
                echo "unpacking ${machine}/xtensa-esp32-elf"
                tar -xf ${machine}/xtensa-esp32-elf.tar.xz > /dev/null 2>&1
            fi
        else
            echo "unpacking ${machine}/xtensa-esp32-elf"
            tar -xf ${machine}/xtensa-esp32-elf.tar.xz > /dev/null 2>&1
        fi
        if [ $? -ne 0 ]; then
            echo "unpacking 'xtensa-esp32-elf' FAILED"
            return 1
        fi
    fi
    return 0
}


# ===========================================================================
# Build MicroPython cross compiler which compiles .py scripts into .mpy files
# ===========================================================================
#-----------------
build_MPyCross() {
    if [ ! -f "components/micropython/mpy-cross/mpy-cross" ]; then
        export CROSS_COMPILE=""
        export PATH=${orig_PATH}
        cd components/mpy_cross_build/mpy-cross
        echo "=================="
        echo "Building mpy-cross"
        make clean > /dev/null 2>&1
        make > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            cp -f mpy-cross ../../micropython/mpy-cross > /dev/null 2>&1
            make clean > /dev/null 2>&1
            echo "OK."
            echo "=================="
        else
            echo "FAILED"
            echo "=================="
            return 1
        fi
        cd ${BUILD_BASE_DIR}
        if [ ! -f "components/micropython/mpy-cross/mpy-cross" ]; then
            echo "FAILED"
            echo "=================="
            return 1
        fi
        export PATH=${xtensa_PATH}
        export CROSS_COMPILE=xtensa-esp32-elf-
    fi
    return 0
}


# ================================================================
# Build mkspiffs program which creates spiffs image from directory
# ================================================================
#-----------------
build_MKSPIFFS() {
    if [ ! -f "components/mkspiffs/${MK_SPIFFS_BIN}" ]; then
        export CROSS_COMPILE=""
        export PATH=${orig_PATH}
        # create 'sdkconfig.h' needed for building
        grep "CONFIG_SPIFFS_"  ${BUILD_BASE_DIR}/build/include/sdkconfig.h  > components/mkspiffs/include/sdkconfig.h
        echo "================="
        echo "Building mkspiffs"
        make -C components/mkspiffs > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "OK."
            echo "================="
        else
            echo "FAILED"
            echo "=================="
            return 1
        fi
        export PATH=${xtensa_PATH}
        export CROSS_COMPILE=xtensa-esp32-elf-
    fi
    return 0
}


# ==============================================================
# Build mkfatfs program which creates fatfs image from directory
# ==============================================================
#----------------
build_MKFatFS() {
    if [ ! -f "components/mkfatfs/src/${MK_FATFS_BIN}" ]; then
        export CROSS_COMPILE=""
        export PATH=${orig_PATH}
        # create 'sdkconfig.h' needed for building
        grep "CONFIG_WL_SECTOR_SIZE"  ${BUILD_BASE_DIR}/build/include/sdkconfig.h  > components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_WL_SECTOR_MODE" ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_FATFS_CODEPAGE"  ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_FATFS_LFN_HEAP"  ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_FATFS_LFN_STACK" ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_FATFS_MAX_LFN"   ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h
        grep "CONFIG_SPI_FLASH_ENABLE_COUNTERS" ${BUILD_BASE_DIR}/build/include/sdkconfig.h >> components/mkfatfs/src/sdkconfig.h

        export BUILD_DIR_BASE=${BUILD_BASE_DIR}/build
        echo "================"
        echo "Building mkfatfs"
        make -C components/mkfatfs/src > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "OK."
            echo "================"
        else
            echo "FAILED"
            echo "================"
            return 1
        fi
        export PATH=${xtensa_PATH}
        export CROSS_COMPILE=xtensa-esp32-elf-
    fi
    return 0
}

# ===============================================================
# Build mklfs program which creates LittleFS image from directory
# ===============================================================
#----------------
build_MKlfsFS() {
    if [ ! -f "components/mklittlefs/${MK_LITTLEFS_BIN}" ]; then
        export CROSS_COMPILE=""
        export PATH=${orig_PATH}

        export BUILD_DIR_BASE=${BUILD_BASE_DIR}/build
        echo "==============="
        echo "Building mklfs"
        make -C components/mklittlefs > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "OK."
            echo "==============="
        else
            echo "FAILED"
            echo "==============="
            return 1
        fi
        export PATH=${xtensa_PATH}
        export CROSS_COMPILE=xtensa-esp32-elf-
    fi
    return 0
}


# ===================
# Check build command
# ===================
#---------------
checkCommand() {
    if [ "${arg}" == "makefatfs" ] || [ "${arg}" == "flashfatfs" ] || [ "${arg}" == "copyfatfs" ]; then
        if [ ! -f "build/include/sdkconfig.h" ]; then
            echo ""
            echo "Run './BUILD menuconfig' first."
            echo ""
            return 1
        fi
    fi

    if [ "${arg}" == "firmware" ] || [ "${arg}" == "size" ] || [ "${arg}" == "size-components" ] || [ "${arg}" == "size-files" ]; then
        if [ ! -f "build/MicroPython.bin" ]; then
            echo ""
            echo "Build firmware first."
            echo ""
            return 1
        fi
    fi

    # Test if valid sdkconfig exists
    if [ "${arg}" == "all" ] || [ "${arg}" == "clean" ]; then
        if [ ! -f "sdkconfig" ]; then
            echo "'sdkconfig' NOT FOUND, RUN ./BUILD.sh menuconfig FIRST."
            echo ""
            return 1
        fi
    fi

    if [ $? -ne 0 ]; then
        return 1
    fi

    return 0
}


# ===================================
# ==== Execute the build command ====
# ===================================
#-----------------
executeCommand() {

    checkCommand
    if [ $? -ne 0 ]; then
        return 1
    fi

    # ------------------------------
    if [ "${arg}" == "flash" ]; then
        echo "========================================="
        echo "Flashing MicroPython firmware to ESP32..."
        echo "========================================="
        make ${J_OPTION} ${arg}${BUILD_COMPORT}${BUILD_BDRATE} 2>/dev/null

    # ---------------------------------
    elif [ "${arg}" == "erase" ]; then
        echo "======================"
        echo "Erasing ESP32 Flash..."
        echo "======================"
        make erase_flash${BUILD_COMPORT}${BUILD_BDRATE}

    # ------------------------------------
    elif [ "${arg}" == "miniterm" ]; then
        echo "====================="
        echo "Executing miniterm..."
        echo "====================="
        make simple_monitor${BUILD_COMPORT}

    # ----------------------------------
    elif [ "${arg}" == "monitor" ]; then
        echo "============================"
        echo "Executing esp-idf monitor..."
        echo "============================"
        make monitor${BUILD_COMPORT}

    # ---------------------------------
    elif [ "${arg}" == "clean" ]; then
        echo "============================="
        echo "Cleaning MicroPython build..."
        echo "============================="

        if [ "${MP_SHOW_PROGRESS}" == "yes" ]; then
            make clean
        else
            make clean > /dev/null 2>&1
        fi
        sleep 1
        #rm -f components/micropython/mpy-cross/mpy-cross > /dev/null 2>&1
        rm -rf build/ > /dev/null 2>&1
        rmdir build > /dev/null 2>&1
        rm -f components/mkspiffs/src/*.o > /dev/null 2>&1
        rm -f components/mkspiffs/src/spiffs/*.o > /dev/null 2>&1
        rm -f components/mkfatfs/src/*.o > /dev/null 2>&1
        rm -f components/mkfatfs/src/fatfs/*.o > /dev/null 2>&1
        rm -f components/mkfatfs/src/spi_flash/*.o > /dev/null 2>&1
        rm -f components/mkfatfs/src/vfs/*.o > /dev/null 2>&1
        rm -f components/mkfatfs/src/wear_leveling/*.o > /dev/null 2>&1

    # -------------------------------------
    elif [ "${arg}" == "menuconfig" ]; then
        if [ -f _sdkconfig.saved ]; then
            cp -f _sdkconfig.saved sdkconfig > /dev/null 2>&1
            rm -f _sdkconfig.saved > /dev/null 2>&1
            echo "** Restored 'sdkconfig' **'"
        fi
        make menuconfig 2>/dev/null
        check_config
        result=$?
        if [ $result -eq 1 ]; then
            make menuconfig 2>/dev/null
        fi

    # ---------------------------------
    elif [ "${arg}" == "makefs" ]; then
        build_MKSPIFFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "========================"
        echo "Creating SPIFFS image..."
        echo "========================"
        make makefs

    # -----------------------------------
    elif [ "${arg}" == "flashfs" ]; then
        build_MKSPIFFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "================================="
        echo "Flashing SPIFFS image to ESP32..."
        echo "================================="
        make flashfs

    # ---------------------------------
    elif [ "${arg}" == "copyfs" ]; then
        echo "========================================="
        echo "Flashing default SPIFFS image to ESP32..."
        echo "========================================="
        make copyfs

    # -------------------------------------
    elif [ "${arg}" == "makefatfs" ]; then
        build_MKFatFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "======================="
        echo "Creating FatFS image..."
        echo "======================="
        make makefatfs

    # --------------------------------------
    elif [ "${arg}" == "flashfatfs" ]; then
        build_MKFatFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "================================"
        echo "Flashing FatFS image to ESP32..."
        echo "================================"
        make flashfatfs

    # ------------------------------------
    elif [ "${arg}" == "copyfatfs" ]; then
        echo "========================================"
        echo "Flashing default FatFS image to ESP32..."
        echo "========================================"
        make copyfatfs

    # ------------------------------------
    elif [ "${arg}" == "makelfsfs" ]; then
        build_MKlfsFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "=========================="
        echo "Creating LittleFS image..."
        echo "=========================="
        make makelfsfs

    # -------------------------------------
    elif [ "${arg}" == "flashlfsfs" ]; then
        build_MKlfsFS
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "==================================="
        echo "Flashing LittleFS image to ESP32..."
        echo "==================================="
        make flashlfsfs

    # -------------------------------------
    elif [ "${arg}" == "copylfsfs" ]; then
        echo "==========================================="
        echo "Flashing default LittleFS image to ESP32..."
        echo "==========================================="
        make copylfsfs

    # -------------------------------
    elif [ "${arg}" == "size" ]; then
        echo "======================================="
        echo "Static memory footprint of the firmware"
        echo "======================================="
        make size 2>/dev/null

    # --------------------------------------------------------------------------
    elif [ "${arg}" == "size-components" ] || [ "${arg}" == "size-files" ]; then
        echo "========================================="
        echo "Detailed memory footprint of the firmware"
        echo "========================================="
        make ${arg} 2>/dev/null

    # -------------------------------------------------------------------------------------------------------
    elif [ "${arg}" == "firmware" ] || [ "${arg}" == "firmwareall" ] || [ "${arg}" == "firmwareallbt" ]; then
        echo "======================="
        echo "Saving the firmware ..."
        echo "======================="
        local esp32dir="esp32"
        if [ "${BUILD_TYPE}" != "" ]; then
            esp32dir="esp32_psram"
        fi
        if [ "${arg}" == "firmwareall" ]; then
            if [ "${BUILD_TYPE}" != "" ]; then
                esp32dir="esp32_psram_all"
            else
                esp32dir="esp32_all"
            fi
        fi
        if [ "${arg}" == "firmwareallbt" ]; then
            if [ "${BUILD_TYPE}" != "" ]; then
                esp32dir="esp32_psram_all_bt"
            else
                esp32dir="esp32_all_bt"
            fi
        fi
        local useota=$(grep -e CONFIG_MICROPY_USE_OTA=y sdkconfig)
        if [ "${useota}" == "CONFIG_MICROPY_USE_OTA=y" ]; then
            esp32dir="${esp32dir}_ota"
        fi
        cp -f build/MicroPython.bin firmware/${esp32dir} > /dev/null 2>&1
        cp -f build/bootloader/bootloader.bin firmware/${esp32dir}/bootloader > /dev/null 2>&1
        cp -f build/partitions_mpy.bin firmware/${esp32dir} > /dev/null 2>&1
        cp -f partitions_mpy.csv firmware/${esp32dir} > /dev/null 2>&1
        cp -f build/phy_init_data.bin firmware/${esp32dir} > /dev/null 2>&1
        cp -f sdkconfig firmware/${esp32dir} > /dev/null 2>&1
        rm -f firmware/${esp32dir}/flash.sh > /dev/null 2>&1
        return 0

    # -------------------------------
    elif [ "${arg}" == "all" ]; then
        build_MPyCross
        if [ $? -ne 0 ]; then
            return 1
        fi
        echo "================================"
        echo "Building MicroPython firmware..."
        echo "================================"

        if [ "${MP_SHOW_PROGRESS}" == "yes" ]; then
            make ${J_OPTION} ${arg}
        else
            make ${J_OPTION} ${arg} > /dev/null 2>&1
        fi

    #---
    else
        echo "================================"
        echo "Unknown build command '${arg}' !"
        echo "================================"
        return 1
    fi
}
