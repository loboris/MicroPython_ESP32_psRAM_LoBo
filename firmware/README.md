
## Prebuilt firmwares


Default configuration is used.

Available firmwares:

| Directory | Description |
| --- | --- |
|**esp32** | Generic default build. uPython on both cores, 4MB single partition flash layout |
|**m5stack** | M5Stack default build. C-like (arduino) menu system on core0 and uPython on core1. 4MB single partition flash layout. |

All firmwares are configured with variable sized SPIFFS file system; its size depends from the system size and the amount of OTA partitions.

To flash you can:
- use **esptool.py**.
- use the **flash.sh** script from its directory after editing to set the correct usb port for your board.
