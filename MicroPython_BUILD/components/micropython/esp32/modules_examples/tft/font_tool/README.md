
#### Program to convert any ttf font to c source file that can be compiled and used as external font in **display.TFT**
---

This is a command line **Windows** program, but can be used under **Linux** with **wine**:

Usage:

```
ttf2c_vc2003.exe <point-size> <input-file> <output-file> [<start_char> <end_char>]
```

or, under Linux:

```
wine ./ttf2c_vc2003.exe <point-size> <input-file> <output-file> [<start_char> <end_char>]
```

### Example:
---

```
wine ./ttf2c_vc2003.exe 18 Vera.ttf vera.c
```

**After the c source is created, open it in editor and make the following changes:**

Delete the line:

```
#include <avr/pgmspace.h>
```

Change the line:

```
uint8_t vera18[] PROGMEM =
```

to:

```
const unsigned char vera18[] =
```

The font name (**vera18** here) will be different for different font, you can change it to any other name.

---

You can transfer **c** source file to the MicroPython file system and use **tft.compileFont()** to compile the font
```
tft.compileFont("vera.c")
```
and use it as external font:
```
tft.font("vera.fon")
```
