# coding: utf-8

import time


def blink(pin, times = 1, on_seconds = 0.1, off_seconds = 0.1, high_is_on = True):
    for i in range(times):
        pin.high() if high_is_on else pin.low()
        time.sleep(on_seconds)
        pin.low() if high_is_on else pin.high()
        time.sleep(off_seconds)