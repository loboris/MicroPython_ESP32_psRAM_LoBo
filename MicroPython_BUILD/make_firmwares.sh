#!/bin/bash

# BUILD standard firmwares

mv -f sdkconfig sdkconfig.bkp

# Build firmwaware for esp32, no psRAM, no OTA
cp sdkconfig.fw_esp32 sdkconfig
./BUILD.sh -j8 clean all
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi
./BUILD.sh -fs 1024 flash > /dev/null 2>&1
./BUILD.sh firmware
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi

# Build firmwaware for esp32, no psRAM, OTA
cp sdkconfig.fw_esp32_ota sdkconfig
./BUILD.sh -j8 clean all
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi
./BUILD.sh -fs 1024 flash > /dev/null 2>&1
./BUILD.sh firmware
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi

# Build firmwaware for esp32, psRAM, no OTA
cp sdkconfig.fw_psram sdkconfig
./BUILD.sh -j8 clean all
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi
./BUILD.sh -fs 1024 flash > /dev/null 2>&1
./BUILD.sh firmware
if [ $? -ne 0 ]; then
    exit 1
fi

# Build firmwaware for esp32, psRAM, OTA
cp sdkconfig.fw_psram_ota sdkconfig
./BUILD.sh -j8 clean all
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi
./BUILD.sh -fs 1024 flash > /dev/null 2>&1
./BUILD.sh firmware
if [ $? -ne 0 ]; then
    mv -f sdkconfig.bkp sckconfig
    exit 1
fi

mv -f sdkconfig.bkp sckconfig

cd firmware
rm -f MicroPython_LoBo_Firmwares.zip > /dev/null 2>&1
zip MicroPython_LoBo_Firmwares.zip -9 -r esp32 esp32_ota esp32_psram esp32_psram_ota README.md

cd ..

