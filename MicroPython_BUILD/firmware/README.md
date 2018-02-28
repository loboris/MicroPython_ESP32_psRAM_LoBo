
## Prebuilt firmwares


Default configuration is used.

Available firmwares:

| Directory | Description |
| - | - |
|**esp32** | MicroPython, single partition layout, 4MB Flash |
|**esp32_ota** | MicroPython, dual partition layout, OTA enabled, 4MB Flash |
|**esp32_psram** | MicroPython, single partition layout, 4MB Flash & 4MB SPIRAM |
|**esp32_psram_ota** | MicroPython, dual partition layout, OTA enabled, 4MB Flash & 4MB SPIRAM |
|**esp32_all** | MicroPython, single partition layout, 4MB Flash, ALL modules included |
|**esp32_psram_all** | MicroPython, single partition layout, 4MB Flash & 4MB SPIRAM, ALL modules included |


All firmwares are configured with 1 MB SPIFFS file system.<br>
Telnet server, FTP server, mDNS and Mqtt are enabled.

To flash, use **esptool.py**.

You can use the `flash.sh` script, run it in its directory.

**Edit the** `flash.sh` **and set the correct usb port for your board.**
