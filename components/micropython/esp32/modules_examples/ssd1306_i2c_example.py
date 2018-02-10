import machine
from ssd1306 import SSD1306_I2C

WIDTH = const(128)
HEIGHT = const (64)
sda_pin = machine.Pin(26)
scl_pin = machine.Pin(25)

i2c = machine.I2C(scl=scl_pin, sda=sda_pin, speed=400000)

ssd = SSD1306_I2C(WIDTH, HEIGHT, i2c)

import freesans20

from writer import Writer
wri2 = Writer(ssd, freesans20, verbose=True)

Writer.set_clip(True, True)
Writer.set_textpos(0, 0)
wri2.printstring('MicroPython\nby LoBo\n10/2017')

ssd.show()
