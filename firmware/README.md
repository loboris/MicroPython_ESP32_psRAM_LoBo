
## Prebuilt firmwares


Default configuration is used.

Available firmwares:

| Directory | Flash size | OTAs | psRAM | Description |
| ---       | ---        | --- | ---   | ---         |
|**esp32s** | 4MB | no | no | Generic single core. Everything on one core. Minimum modules, maximum SPIFFS possible. |
|**esp32** | 4MB | no | no | Generic dual core. C-like (arduino) menu system on core0 and uPython on core1. Minimum modules, maximum SPIFFS possible. |
|**m5stack** | 4MB | no | no | M5Stack default build. C-like (arduino) menu system on core0 and uPython on core1. Minimum modules, maximum SPIFFS possible. |
|**m5stackota** | 4MB | 1 | no | M5Stack default build. C-like (arduino) menu system on core0 and uPython on core1. All modules, SPIFFS best effort. |
|**m5stack2ota** | 4MB | 2 | no | M5Stack default build. C-like (arduino) menu system on core0 and uPython on core1. Minimum modules, maximum number of OTAs. |

All firmwares are configured with variable sized SPIFFS file system; its size depends from the system size and the amount of OTA partitions. One Factory partiton is always in place.

Current system size is 1280KB, so the space left for the SPIFFS filesystem is:
| Partitions | OTAs | SPIFFS |
| ---        | ---  | ---   |
| 1 | 0 |  2736  |
| 2 | 1 |  1464  |
| 3 | 1 |  192  |

Those figures will change as soon as I have time to study the modules a bit.

To flash you can:
- use **esptool.py**.
- use the **flash.sh** script from its directory after editing to set the correct usb port for your board.
