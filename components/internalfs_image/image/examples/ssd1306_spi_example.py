import machine
from ssd1306 import SSD1306_SPI

WIDTH = const(128)
HEIGHT = const (64)
pdc = machine.Pin(27, machine.Pin.OUT)
pcs = machine.Pin(26, machine.Pin.OUT)
sck_pin = machine.Pin(19, machine.Pin.OUT)
mosi_pin = machine.Pin(23, machine.Pin.IN)
miso_pin = machine.Pin(25, machine.Pin.OUT)

prst = machine.Pin(18, machine.Pin.OUT)

spi = machine.SPI(1,baudrate=1000000, sck=sck_pin, mosi=mosi_pin, miso=miso_pin)

ssd = SSD1306_SPI(WIDTH, HEIGHT, spi, pdc, prst, pcs)

import freesans20

from writer import Writer
wri2 = Writer(ssd, freesans20, verbose=True)

Writer.set_clip(True, True)
Writer.set_textpos(0, 0)
wri2.printstring('MicroPython\n')

ssd.show()
