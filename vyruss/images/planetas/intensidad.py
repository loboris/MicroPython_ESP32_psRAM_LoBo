from machine import Pin
from apa102 import APA102



clock = Pin(14, Pin.OUT)     # set GPIO14 to output to drive the clock
data = Pin(13, Pin.OUT)      # set GPIO13 to output to drive the data
apa = APA102(clock, data, 128) # create APA102 driver on the clock and the data pin for 8 pixels

def intensidad(value=128):
    max_intensidad = 128
    value = value % max_intensidad

    for i in range(max_intensidad):
        if i <= value:
            apa[i] = (255,255,255,32)
        else:
            apa[i] = (0,0,0,0)
    apa.write()                  # write data to all pixels
