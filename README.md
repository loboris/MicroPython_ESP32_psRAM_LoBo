# ESP32 Template Plus #

## Work In Progress!!! The code in this repo should be considered alpha and not used in production environments. ##

This repository contains an esp-idt template on steroids: in a few minutes you can have a fully functional build to flash your ESP32 with, and to jumpstart your own project.

It is a mash-up of other repos:
- https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
- https://github.com/espressif/arduino-esp32
- https://github.com/m5stack/M5Stack
- https://github.com/tomsuch/M5StackSAM
- https://github.com/yanbe/esp32-ota-server

Plus other additions I cherry picked up and there. In order to import future improvements, I mantain some bridge repos on my account.

I made it to test my tiny [M5Stack](http://www.m5stack.com/) and it works good for me. I hope you enjoy it too.

## Index ##
1. Install instructions
2. Features
3. Status
4. References/Docs

## 1. Install instructions ##

1. Clone:

`git clone https://github.com/mfp20/esp-idf-template-plus.git`

2. Subclone:

`git submodule update --init --recursive`

3. Configure:

`./build.sh menuconfig`

4. Build:

`./build.sh`

5. Flash:

`./build.sh flash`

6. Enjoy!

## 2. Features ##

### 2.1. Major add-ons ###
* Includes the latest **MicroPython** build from [main Micropython repository](https://github.com/micropython/micropython). Courtesy of [Loboris](https://github.com/loboris).
* Includes the latest **Arduino's ESP32 libraries** to seamless run Arduino code on the ESP32 boards.
* Includes an **OTA push server** to update remote devices.
* Includes code to **send raw 802.11 packets and monitor mode**.
* Includes **menu-driven configuration and a build.sh script** to make as **easy** as possible building the firmware. The regular esp-idf menuconfig system can be used for configuration of Micropython, Arduino and OTA Server options because they are built-in as esp-idf components. No manual *sdkconfig.h* editing and tweaking is necessary. Courtesy of [Loboris](https://github.com/loboris).

### 2.2. Other Features ###
* Supports both **unicore** (MicroPython, Arduino and OTA Server tasks running only on the first ESP32 core) and **dualcore** (MicroPython task running on ESP32 App core, all the others on the Pro core) configurations. User can choose the core to run each task.
* Supports ESP32 boards **with and without psRAM**. MicroPython works great on ESP32, but the most serious issue is still (as on most other MicroPython boards) limited amount of free memory left after boot (~100KB). This repository contains all the tools and sources necessary to build working MicroPython firmware which can fully use the advantages of 4MB (or more) of psRAM. Flash memory is limited as well, but on ESP32 both RAM and FLASH can be expanded up to **16MB**, making even more valuable the chance to use psRAM to exploit more complex applications installed on the expanded flash memory. Courtesy of [Loboris](https://github.com/loboris).
* Default configuration has **2MB** of MicroPython heap, **20KB** of MicroPython stack, **~200KB** of free DRAM heap for C modules and functions.
* Supports all flash memory access modes: QIO, QOUT, DIO, DOUT. Defaults to **QIO, 80Mhz**.
* Supports **Fat filesystem with esp-idf wear leveling driver**, so there is less danger of damaging the flash with frequent writes.
* Supports **SPIFFS filesystem** and it can be used instead of FatFS in SPI Flash.
* **sdcard** support is included which uses esp-idf **sdmmc** driver and can work in **SD mode** (*1-bit* and *4-bit*) or in **SPI mode** (sd card can be connected to any pins). For imformation on how to connect sdcard see the documentation.
* Files **timestamp** is correctly set to system time both on internal fat filesysten and on sdcard
* **Native ESP32 VFS** support for spi Flash & sdcard filesystems.
* Flexible automatic and/or manual filesystem configuration.

### 2.3. Included modules ###
* **RTC Class** is added to machine module, including methods for synchronization of system time to **ntp** server, **deepsleep**, **wakeup** from deepsleep **on external pin** level, ...
* **Time zone** can be configured via **menuconfig** and is used when syncronizing time from NTP server
* Built-in **ymodem module** for fast transfer of text/binary files to/from host
* Some additional frozen modules are added, like **pye** editor, **urequests**, **functools**, **logging**, ...
* **Btree** module included, can be Enabled/Disabled via **menuconfig**
* **_threads** module greatly improved, inter-thread **notifications** and **messaging** included
* **Neopixel** module using ESP32 **RMT** peripheral with many new features
* **DHT** module implemented using ESP32 RMT peripheral
* **1-wire** module implemented using ESP32 RMT peripheral
* **i2c** module uses ESP32 hardware i2c driver
* **spi** module uses ESP32 hardware spi driver
* **adc** module improved, new functions added
* **pwm** module, ESP32 hardware based
* **timer** module improved, new timer types and features
* **curl** module added, many client protocols including FTP and eMAIL
* **ssh** module added with sftp/scp support and _exec_ function to execute program on server
* **display** module added with full support for spi TFT displays
* **mqtt** module added, implemented in C, runs in separate task
* **mDNS** module added, implemented in C, runs in separate task
* **telnet** module added, connect to **REPL via WiFi** using telnet protocol
* **ftp** server module added, runs as separate ESP32 task
* **GSM/PPPoS** support, connect to the Internet via GSM module
* **OTA Update** supported, various partitions layouts
* **Eclipse** project files included. To include it into Eclipse goto File->Import->Existing Projects into Workspace->Select *esp-idf-template-plus* directory->Finish. **Rebuild index**.

## 3. Status ##

It builds on **Linux** but should be fine on **MacOS** and **Windows** as well. Please try and report back!
The default image runs MicroPhyton on M5Stack. The other processes (Arduino and OTA Server) are created but not started yet.

### 3.1. Hardware support ###

| Board             | Micropython   | Arduino | Others |
| ----------------- |:-------------:| -------:| ------:|
| M5Stack           | [works](https://github.com/mfp20)     | WiP     | WiP    |
| ESP-WROVER-KIT v3 | [works](https://github.com/loboris)     | unknown | unknown|

Currently, there are several modules & development boards that ship with 4MB of psRAM, some examples:<br>

* [**M5Stack**](http://www.m5stack.com) _Development Kit_ [version with psRAM](https://www.aliexpress.com/store/product/M5Stack-NEWEST-4M-PSRAM-ESP32-Development-Board-with-MPU9250-9DOF-Sensor-Color-LCD-for-Arduino-Micropython/3226069_32847906756.html?spm=2114.12010608.0.0.1ba0ee41gOPji)
* **TTGO T8 V1.1** _board_, available at [eBay](https://www.ebay.com/itm/TTGO-T8-V1-1-ESP32-4MB-PSRAM-TF-CARD-3D-ANTENNA-WiFi-bluetooth/152891206854?hash=item239906acc6:g:7QkAAOSwMfhadD85)
* **ESP-WROVER-KIT** _boards_ from Espressif, available from [ElectroDragon](http://www.electrodragon.com/product/esp32-wrover-kit/), [AnalogLamb](https://www.analoglamb.com/product/esp-wrover-kit-esp32-wrover-module/), ...
* **WiPy 3.0** _board_ from [Pycom](https://pycom.io/product/wipy-3/).
* **TTGO TAudio** _board_ ([eBay](https://www.ebay.com/itm/TTGO-TAudio-V1-0-ESP32-WROVER-SD-Card-Slot-Bluetooth-WI-FI-Module-MPU9250/152835010520?hash=item2395ad2fd8:g:Jt8AAOSwR2RaOdEp))
* **Lolin32 Pro** _board_ from [Wemos](https://wiki.wemos.cc/products:lolin32:lolin32_pro) - **`no longer available`** ([Schematic](https://wiki.wemos.cc/_media/products:lolin32:sch_lolin32_pro_v1.0.0.pdf)).
* **ESP-WROVER** _module_ from Espressif, available from [ElectroDragon](http://www.electrodragon.com/product/esp32-wrover-v4-module-based-esp32/) and many other vendors.
* **ALB32-WROVER** _module_ (4 MB SPIRAM & 4/8/16 MB Flash) from [AnalogLamb](https://www.analoglamb.com/product/alb32-wrover-esp32-module-with-64mb-flash-and-32mb-psram/).
* **S01**, **L01** and **G01** _OEM modules_ from [Pycom](https://pycom.io/webshop#oem-products).

## 4. References/Docs ##

* [Loboris' Wiki](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/wiki), mostly focused on the Micropython part.
* Micropython examples on [Lobori's repo](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/tree/master/MicroPython_BUILD/components/micropython/esp32/modules_examples).

### Example terminal session

```
I (0) cpu_start: App cpu up.
I (1569) spiram: SPI SRAM memory test OK
I (1570) heap_init: Initializing. RAM available for dynamic allocation:
D (1570) heap_init: New heap initialised at 0x3ffae6e0
I (1575) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
D (1581) heap_init: New heap initialised at 0x3ffc1a00
I (1586) heap_init: At 3FFC1A00 len 0001E600 (121 KiB): DRAM
I (1593) heap_init: At 3FFE0440 len 00003BC0 (14 KiB): D/IRAM
I (1599) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
D (1606) heap_init: New heap initialised at 0x4009d70c
I (1611) heap_init: At 4009D70C len 000028F4 (10 KiB): IRAM
I (1617) cpu_start: Pro cpu start user code
I (1622) spiram: Adding pool of 4096K of external SPI memory to heap allocator
I (1630) spiram: Reserving pool of 32K of internal memory for DMA/internal allocations
D (1646) clk: RTC_SLOW_CLK calibration value: 3305242
D (89) intr_alloc: Connected src 46 to int 2 (cpu 0)
D (90) intr_alloc: Connected src 57 to int 3 (cpu 0)
D (90) intr_alloc: Connected src 24 to int 9 (cpu 0)
I (95) cpu_start: Starting scheduler on PRO CPU.
D (0) intr_alloc: Connected src 25 to int 2 (cpu 1)
I (4) cpu_start: Starting scheduler on APP CPU.
D (119) heap_init: New heap initialised at 0x3ffe0440
D (125) heap_init: New heap initialised at 0x3ffe4350
D (130) intr_alloc: Connected src 16 to int 12 (cpu 0)
D (145) nvs: nvs_flash_init_custom partition=nvs start=9 count=4
D (178) intr_alloc: Connected src 34 to int 3 (cpu 1)
D (187) intr_alloc: Connected src 22 to int 4 (cpu 1)

Internal FS (SPIFFS): Mounted on partition 'internalfs' [size: 1048576; Flash address: 0x2D0000]
----------------
Filesystem size: 956416 B
           Used: 512 B
           Free: 955904 B
----------------

FreeRTOS running on BOTH CORES, MicroPython task running on both cores.
Running from partition at 10000, type 10 [MicroPython_1].

 Reset reason: Power on reset
    uPY stack: 19456 bytes
     uPY heap: 3073664/5664/3068000 bytes (in SPIRAM using malloc)

MicroPython ESP32_LoBo_v3.1.0 - 2017-01-03 on ESP32 board with ESP32
Type "help()" for more information.
>>> 
>>> import micropython, machine
>>> 
>>> micropython.mem_info()
stack: 752 out of 19456
GC: total: 3073664, used: 5904, free: 3067760
 No. of 1-blocks: 19, 2-blocks: 7, max blk sz: 325, max free sz: 191725
>>> 
>>> machine.heap_info()
Heap outside of MicroPython heap:
---------------------------------
              Free: 239920
         Allocated: 52328
      Minimum free: 233100
      Total blocks: 85
Largest free block: 113804
  Allocated blocks: 79
       Free blocks: 6

SPIRAM info:
------------
              Free: 1048532
         Allocated: 3145728
      Minimum free: 1048532
      Total blocks: 2
Largest free block: 1048532
  Allocated blocks: 1
       Free blocks: 1
>>>
```
