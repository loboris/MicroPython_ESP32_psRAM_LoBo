# MicroPython for ESP32

# with support for 4MB of psRAM


**MicroPython core** is synchronized with [main MicroPython repository](https://github.com/micropython/micropython)

**Commit:** bcfff4fc98a73c5ad9b7d3e338649955e861ada4

<br>

Some source files are changed to be compatible with this port.<br>
All modified files has the copyright notice:<br>
_Copyright (c) 2018 LoBo (https://github.com/loboris)_<br>

## List of the modified files:

### **py** directory

builtinhelp.c<br>
modmath.c<br>
modmicropython.c<br>
modsys.c<br>
modthread.c<br>
mpconfig.h<br>
mphal.h<br>
mpstate.h<br>
mpthread.h<br>
mpz.c<br>
mpz.h<br>
obj.c<br>
obj.h<br>
objint_mpz.c<br>
ringbuf.h<br>
runtime.h<br>
scheduler.c<br>
vm.c<br>

### **extmod** directory

machine_pulse.c<br>
modbtree.c<br>
modframebuf.c<br>
modlwip.c<br>
moduhashlib.c<br>
moduselect.c<br>
modussl_mbedtls.c<br>
modutimeq.c<br>
utime_mphal.c<br>
utime_mphal.h<br>
vfs.c<br>

### **lib** directory

utils/pyexec.c<br>
utils/sys_stdio_mphal.c<br>
mp-readline/readline.c<br>

