# ESP32 Template Plus #

This repository contains an esp-idf template on steroids: in a few minutes you can have a fully functional build to flash your ESP32 with, and to jumpstart your own project.
I made it to test my tiny [M5Stack](http://www.m5stack.com/) and it works good for me. I hope you enjoy it too.

The idea is to use the Pro Core for hw management using the wide range of C-like Arduino libs (tft, audio, buttons, GPIO pins, ota ...) and leave the App Core for uPython black magic. But:
- the buildchain is just a very flexible barebone that can be confugured at wish.
- more and more uPython libs and c-wrappers are available every day.

In other words: defaults for M5Stack but it is pretty easy for you to customize it at wish. Memory is the only real limit, on boards without psRAM.

Most of the awesome features listed below are from [Loboris uPython and psRAM](https://github.com/loboris) repo. I just reworked/cleaned his repo and glued some more stuff on top (see [External resources](#res)).

## Table of Contents ##
* 1. [Features & Status](#fands)
  * 1.1. [Current terminal session](#terminal)
  * 1.2. [Features](#features)
  * 1.3. [Hardware](#hw)
* 2. [Documentation](#docs)
  * 2.1. [Install](#install)
  * 2.2. [Customize](#custom)
  * 2.3. [External resources](#res)
* 3. [TODOs](#todo)

<a name="fands"/>

## 1. Features & Status ##

Currently it builds on **Linux** but should be fine on **MacOS** and **Windows** as well. Please try and report back!

The default image runs MicroPhyton on M5Stack. The other processes (Arduino and OTA Server) are created but not started yet.

<a name="terminal"/>

### 1.1. Current terminal session ###

```
$ ./app.sh flash monitor

---------------------
MicroPython for ESP32
---------------------

=========================================
Flashing MicroPython firmware to ESP32...
=========================================
Flashing binaries to serial port /dev/ttyUSB0 (app at offset 0x10000)...
esptool.py v2.2.1
Connecting......
Chip is ESP32D0WDQ6 (revision 1)
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
Changed.
Configuring flash size...
Auto-detected Flash size: 4MB
Compressed 21632 bytes to 12690...
Wrote 21632 bytes (12690 compressed) at 0x00001000 in 0.2 seconds (effective 1039.7 kbit/s)...
Hash of data verified.
Compressed 144 bytes to 69...
Wrote 144 bytes (69 compressed) at 0x0000f000 in 0.0 seconds (effective 632.6 kbit/s)...
Hash of data verified.
Compressed 1254768 bytes to 750595...
Wrote 1254768 bytes (750595 compressed) at 0x00010000 in 12.3 seconds (effective 814.3 kbit/s)...
Hash of data verified.
Compressed 3072 bytes to 142...
Wrote 3072 bytes (142 compressed) at 0x00008000 in 0.0 seconds (effective 8422.2 kbit/s)...
Hash of data verified.

Leaving...
Staying in bootloader.
OK.

============================
Executing esp-idf monitor...
============================
MONITOR
--- idf_monitor on /dev/ttyUSB0 115200 ---
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
ets Jun  8 2016 00:22:57

rst:0x1 (POWERON_RESET),boot:0x17 (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
mode:DIO, clock div:1
load:0x3fff0018,len:4
load:0x3fff001c,len:5984
ho 0 tail 12 room 4
load:0x40078000,len:0
load:0x40078000,len:15548
entry 0x40079018
I (30) boot: ESP-IDF v3.1-dev-288-gdaa8cfa8 2nd stage bootloader
I (30) boot: compile time 02:41:20
I (30) boot: Enabling RNG early entropy source...
I (36) qio_mode: Enabling default flash chip QIO
I (42) boot: SPI Speed      : 80MHz
I (46) boot: SPI Mode       : QIO
I (50) boot: SPI Flash Size : 4MB
I (54) boot: Partition Table:
I (57) boot: ## Label            Usage          Type ST Offset   Length
I (65) boot:  0 nvs              WiFi data        01 02 00009000 00004000
I (72) boot:  1 otadata          OTA data         01 00 0000d000 00002000
I (80) boot:  2 phy_init         RF data          01 01 0000f000 00001000
I (87) boot:  3 factory          factory app      00 00 00010000 0013e000
I (94) boot:  4 boot1            OTA app          00 10 00150000 0013e000
I (102) boot:  5 boot2            OTA app          00 11 00290000 0013e000
I (109) boot:  6 internalfs       Unknown data     01 82 003ce000 00030000
I (117) boot: End of partition table
I (121) boot: Defaulting to factory image
I (126) esp_image: segment 0: paddr=0x00010020 vaddr=0x3f400020 size=0x43d80 (277888) map
I (208) esp_image: segment 1: paddr=0x00053da8 vaddr=0x3ffb0000 size=0x0c268 ( 49768) load
I (223) esp_image: segment 2: paddr=0x00060018 vaddr=0x400d0018 size=0xcaf00 (831232) map
I (440) esp_image: segment 3: paddr=0x0012af20 vaddr=0x3ffbc268 size=0x009d0 (  2512) load
I (441) esp_image: segment 4: paddr=0x0012b8f8 vaddr=0x40080000 size=0x00400 (  1024) load
I (447) esp_image: segment 5: paddr=0x0012bd00 vaddr=0x40080400 size=0x15ed0 ( 89808) load
I (485) esp_image: segment 6: paddr=0x00141bd8 vaddr=0x400c0000 size=0x00064 (   100) load
I (486) esp_image: segment 7: paddr=0x00141c44 vaddr=0x50000000 size=0x00904 (  2308) load
I (509) boot: Loaded app from partition at offset 0x10000
I (509) boot: Disabling RNG early entropy source...
I (115) wifi: wifi firmware version: 9adebde
I (116) wifi: config NVS flash: enabled
I (116) wifi: config nano formating: disabled
I (128) wifi: Init dynamic tx buffer num: 16
I (128) wifi: Init data frame dynamic rx buffer num: 16
I (128) wifi: Init management frame dynamic rx buffer num: 16
I (133) wifi: wifi driver task: 3ffc4a84, prio:23, stack:4096
I (138) wifi: Init static rx buffer num: 8
I (142) wifi: Init dynamic rx buffer num: 16
I (146) wifi: wifi power manager task: 0x3ffcfa50 prio: 21 stack: 2560
I (256) wifi: mode : sta (XX:XX:XX:XX:XX:XX)

Internal FS (SPIFFS): Mounted on partition 'internalfs' [size: 196608; Flash address: 0x3CE000]
----------------
Filesystem size: 173312 B
           Used: 512 B
           Free: 172800 B
----------------

FreeRTOS running on BOTH CORES, MicroPython task started on App Core.
Running from Factory partition starting at 0x10000, [factory].

 Reset reason: Power on reset
    uPY stack: 19456 bytes
     uPY heap: 80000/5856/74144 bytes

MicroPython ESP32_LoBo_v3.1.18 - 2017-02-07 on ESP32 board with ESP32
Type "help()" for more information.
>>> 
>>> import micropython, machine
>>> 
>>> micropython.mem_info()
stack: 752 out of 19456
GC: total: 80000, used: 6320, free: 73680
 No. of 1-blocks: 25, 2-blocks: 9, max blk sz: 337, max free sz: 4596
>>> 
>>> machine.heap_info()
Heap outside of MicroPython heap:
---------------------------------
              Free: 108120
         Allocated: 141324
      Minimum free: 107920
      Total blocks: 155
Largest free block: 60956
  Allocated blocks: 149
       Free blocks: 6
>>> 
```

<a name="features"/>

### 1.2. Features ###

The following is a features table (with current status) to help you understand what the deal is:

| Staus | Feature   | Description |
|:---:|:-------------:|:-------|
| Ok | Easy conf | Includes **menu-driven configuration and a template.sh script** to make as **easy** as possible building the firmware. The regular esp-idf menuconfig system can be used for configuration of uPython, Arduino and OTA Server options because they are built-in as esp-idf components. No manual *sdkconfig.h* editing and tweaking is necessary. |
| Ok | OTA pull | **OTA Update** supported, various partitions layouts. |
| WiP | OTA push | Includes an **OTA push server**. |
| Ok | CPU | Supports both **unicore** (MicroPython, Arduino and OTA Server tasks running only on the first ESP32 core) and **dualcore** (MicroPython task running on ESP32 App core, all the others on the Pro core) configurations. User can choose the core to run each task. |
| Ok | Memory | Supports ESP32 boards **with and without psRAM**. MicroPython works great on ESP32, but the most serious issue is still (as on most other MicroPython boards) limited amount of free memory left after boot (~100KB). This repository contains all the tools and sources necessary to build working MicroPython firmware which can fully use the advantages of 4MB (or more) of psRAM. Flash memory is limited as well, but on ESP32 both RAM and FLASH can be expanded up to **16MB**, making even more valuable the chance to use psRAM to exploit more complex applications installed on the expanded flash memory. |
| Ok | Flash | Supports all flash memory access modes: QIO, QOUT, DIO, DOUT. Defaults to **QIO, 80Mhz**. |
| Ok | Flash | Supports a patched **SPIFFS filesystem** and it can be used instead of FatFS in SPI Flash.
| Ok | Flash | Supports **Fat filesystem with esp-idf wear leveling driver**, so there is less danger of damaging the flash with frequent writes. |
| Ok | Flash | Supports **SD card**; it uses esp-idf's SDMMC driver and it can work in **SD mode** (*1-bit* and *4-bit*) and **SPI mode** (sd card can be connected to any pins). |
| Ok | Flash | **Native ESP32 VFS** support for spi Flash & sdcard filesystems. |
| Ok | Flash | Proper files **timestamp** management both on internal fat filesysten and on sdcard |
| Ok | Flash | Flexible automatic and/or manual filesystem configuration. |
| Ok | Time | **RTC Class** is added to machine module, including methods for synchronization of system time to **ntp** server, **deepsleep**, **wakeup** from deepsleep **on external pin** level, ... |
| Ok | Time | **Time zone** can be configured via **menuconfig** and is used when syncronizing time from NTP server. |
| WiP | Raw WiFi | Includes code to **send raw 802.11 packets and monitor raw packets**. |
| WiP | Arduino | Includes the latest **Arduino's ESP32 libraries** to seamless run Arduino code on the ESP32 boards. |
| WiP | Arduino | Includes C-like (Arduino) display menu and system utilities (battery monitor, timer, ...). |
| Ok | uPython | Includes the latest **MicroPython** build from [main uPython repository](https://github.com/micropython/micropython). |
| Ok | uPython | **Ymodem** module for fast transfer of text/binary files to/from host. |
| Ok | uPython | **Btree** module included, can be Enabled/Disabled via **menuconfig** . |
| Ok | uPython | **_threads** module greatly improved, inter-thread **notifications** and **messaging** included |
| Ok | uPython | **Neopixel** module using ESP32 **RMT** peripheral with many new features. |
| Ok | uPython | **DHT** module implemented using ESP32 RMT peripheral. |
| Ok | uPython | **1-wire** module implemented using ESP32 RMT peripheral. |
| Ok | uPython | **i2c** module uses ESP32 hardware i2c driver. |
| Ok | uPython | **spi** module uses ESP32 hardware spi driver. |
| Ok | uPython | **adc** module improved, new functions added. |
| Ok | uPython | **pwm** module, ESP32 hardware based. |
| Ok | uPython | **timer** module improved, new timer types and features. |
| Ok | uPython | **curl** module added, many client protocols including FTP and eMAIL. |
| Ok | uPython | **ssh** module added with sftp/scp support and _exec_ function to execute program on server. |
| Ok | uPython | **display** module added with full support for spi TFT displays. |
| Ok | uPython | **mqtt** module added, implemented in C, runs in separate task. |
| Ok | uPython | **mDNS** module added, implemented in C, runs in separate task. |
| Ok | uPython | **telnet** module added, connect to **REPL via WiFi** using telnet protocol. |
| Ok | uPython | **ftp** server module added, runs as separate ESP32 task. |
| Ok | uPython | **GSM/PPPoS** support, connect to the Internet via GSM module. |
| Ok | uPython | Includes some additional frozen modules: **pye** editor, **urequests**, **functools**, **logging**, ... |
| Ok | Eclipse | **Eclipse project files included**. To import the project into Eclipse go to File->Import->General->Existing Projects into Workspace, then select *esp32-template-plus* directory and click on Finish button. Finally Rebuild Index. |

<a name="hw"/>

### 1.3. Hardware ###

Currently, there are several modules & development boards that ship with psRAM, some examples:

* [**M5Stack**](http://www.m5stack.com) _Development Kit_ [version with psRAM](https://www.aliexpress.com/store/product/M5Stack-NEWEST-4M-PSRAM-ESP32-Development-Board-with-MPU9250-9DOF-Sensor-Color-LCD-for-Arduino-uPython/3226069_32847906756.html?spm=2114.12010608.0.0.1ba0ee41gOPji)
* **TTGO T8 V1.1** _board_, available at [eBay](https://www.ebay.com/itm/TTGO-T8-V1-1-ESP32-4MB-PSRAM-TF-CARD-3D-ANTENNA-WiFi-bluetooth/152891206854?hash=item239906acc6:g:7QkAAOSwMfhadD85)
* **ESP-WROVER-KIT** _boards_ from Espressif, available from [ElectroDragon](http://www.electrodragon.com/product/esp32-wrover-kit/), [AnalogLamb](https://www.analoglamb.com/product/esp-wrover-kit-esp32-wrover-module/), ...
* **WiPy 3.0** _board_ from [Pycom](https://pycom.io/product/wipy-3/).
* **TTGO TAudio** _board_ ([eBay](https://www.ebay.com/itm/TTGO-TAudio-V1-0-ESP32-WROVER-SD-Card-Slot-Bluetooth-WI-FI-Module-MPU9250/152835010520?hash=item2395ad2fd8:g:Jt8AAOSwR2RaOdEp))
* **Lolin32 Pro** _board_ from [Wemos](https://wiki.wemos.cc/products:lolin32:lolin32_pro) - **`no longer available`** ([Schematic](https://wiki.wemos.cc/_media/products:lolin32:sch_lolin32_pro_v1.0.0.pdf)).
* **ESP-WROVER** _module_ from Espressif, available from [ElectroDragon](http://www.electrodragon.com/product/esp32-wrover-v4-module-based-esp32/) and many other vendors.
* **ALB32-WROVER** _module_ (4 MB SPIRAM & 4/8/16 MB Flash) from [AnalogLamb](https://www.analoglamb.com/product/alb32-wrover-esp32-module-with-64mb-flash-and-32mb-psram/).
* **S01**, **L01** and **G01** _OEM modules_ from [Pycom](https://pycom.io/webshop#oem-products).

Espressif maintains [a more detailed list of ESP32 boards on the market](http://esp32.net/#Hardware). Following the boards I'm aware of working with this code:

| Board             | uPython   | Arduino | Others |
| ----------------- |:-------------:| -------:| ------:|
| M5Stack           | [works](https://github.com/mfp20)     | WiP     | WiP    |
| ESP-WROVER-KIT v3 | [works](https://github.com/loboris)     | unknown | unknown|

In any case is always possible to solder psRAM and/or extra flash yourself. Those ICs are pretty cheap and there are various different ways to solder SMD chips without the proper tools. I placed a 16MB flash on mine with aluminium foil and kapton tape to protect the board from the air gun shooting on the chip to be replaced: easypeasylemonsqueeze. Just be patient and work slow.

<a name="docs"/>

## 2. Documentation ##

<a name="install"/>

### 2.1. Install ###

1. Clone:

`git clone https://github.com/mfp20/esp-idf-template-plus.git`

2. Subclone:

`git submodule update --init --recursive`

3. Configure:

`./app.sh menuconfig`

4. Build:

`./app.sh`

5. Flash:

`./app.sh flash`

6. Log in:

`./app.sh monitor`

7. Have fun!

<a name="custom"/>

### 2.2. Customize ###

The project is 99% open source and mostly MIT/Apache licenses. The only binary blobs are the infamous radio craps: wifi, BT, GSM, ... as usual. It means you can customize anything at wish.

The directory structure is fairly straight forward:
- esp32-template-plus/main: contains the main.c file with the app_main() function that it is run at boot. It initializes the hw, the ota server task, and runs both arduino and micropython tasks. The default arduino task in turn sets the menu on display where to choose (using the hw buttons) one of the apps.c app to run on display. In order to drop in a custom arduino app to replace the default one, just edit the two setup/loop functions in main.c.
- esp32-template-plus/components: contains the esp-idf app components.
- esp32-template-plus/tools: contains the ESP32 sdk, xtensa buildchains for Linux/MacOS/Windows included. Keep in mind that the esp-idf included has been patched by Loboris to enable some uPython features. At 2018/02/09 the build [failed](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/issues/61) (SPIFFS component) using Espressif's vanilla sdk.
- esp32-template-plus/firmwares: contains prebuilt firmware images and their configuration files (sdkconfig and partitions.csv).
- esp32-template-plus/docs: self-explanatory.


<a name="res"/>

### 2.3. External resources ###

In order to merge future revisions, I mantain some bridge repos on my account. Source repos:
- https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
- https://github.com/espressif/arduino-esp32
- https://github.com/m5stack/M5Stack
- https://github.com/tomsuch/M5StackSAM
- https://github.com/yanbe/esp32-ota-server
- plus other additions I cherry picked up and there.

You can find docs at:
- Espressif's [esp-idf](https://github.com/espressif/esp-idf) sdk; and it's [manual](https://esp-idf.readthedocs.io/en/latest/). 
- Loboris' [wiki](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/wiki), mostly focused on the uPython part; [examples](https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo/tree/master/MicroPython_BUILD/components/micropython/esp32/modules_examples) included.
- M5Stack's [website](http://www.m5stack.com/).
- Yanbe's [ota server](https://github.com/yanbe/esp32-ota-server)
- Jeija's [802.11 raw packets sending/monitoring](https://github.com/Jeija/esp32-80211-tx)

<a name="todo"/>

## 3. TODOs ##
1. rework and clean up the Kconfig.projbuild files to sort menuconfig and freeze the default build.
2. enable the arduino menu application and its system apps
3. re-add prebuilt firmwares
4. fix this document
