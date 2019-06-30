LLENAR = 107
EMPTY = bytes((0x80, 0, 0, 0))
COLOR = bytes((0x81, 0xff, 0x00, 0xff))
OTHER_COLOR = bytes((0x81, 0xff, 0xff, 0xff))

NUM_LEDS=107
b=bytearray(12+NUM_LEDS * 4)

for n in range(NUM_LEDS):
    start = 4 + n*4
    b[start:start+4] = EMPTY

for n in range(LLENAR):
    start = 4 + n*4
    b[start:start+4] = COLOR

b[4:4+4] = OTHER_COLOR

print(b)

#import machine
#spi = machine.SPI(machine.SPI.HSPI, sck=14, mosi=13, miso=15)
spi.write(b)
