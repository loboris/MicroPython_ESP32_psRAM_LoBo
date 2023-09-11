# This file is executed on every boot (including wake-boot from deepsleep)
import sys
sys.path[1] = '/flash/lib'
import machine
spi = machine.SPI(machine.SPI.HSPI, sck=14, mosi=13, miso=15)
