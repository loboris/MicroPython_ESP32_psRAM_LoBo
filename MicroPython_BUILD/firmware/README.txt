
=== Prebuilt firmwares ===


Default configuration is used.

Available firmwares:

-------------------------------------------------------------------------------------------------------
Directory           Description
-------------------------------------------------------------------------------------------------------
esp32               MicroPython, single partition layout, 4MB Flash
esp32_ota           MicroPython, dual partition layout, OTA enabled, 4MB Flash
esp32_psram         MicroPython, single partition layout, 4MB Flash & 4MB SPIRAM
esp32_psram_ota     MicroPython, dual partition layout, OTA enabled, 4MB Flash & 4MB SPIRAM
esp32_all           MicroPython, single partition layout, 4MB Flash, ALL modules included
esp32_psram_all     MicroPython, single partition layout, 4MB Flash & 4MB SPIRAM, ALL modules included
-------------------------------------------------------------------------------------------------------


All firmwares are configured with 1 MB SPIFFS file system.<br>
Telnet server, FTP server, mDNS and Mqtt are enabled.

To flash, use 'esptool.py'.
If you don't have it installed, install it using `pip`:

pip install esptool
or
pip3 install esptool


You can use the 'flash.sh' script to flash the firmware:

Change you working directory to the selected firmware directory (the one containing 'MicroPython.bin') and run:

../flash.sh -p <your_comm_port> -b <baud_rate>


-p & -b options are optional, default port is /dev/ttyUSB0, default baud rate is 460800.
