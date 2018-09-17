"""
Very basic example showing data exchange main CPU <--> ULP coprocessor.

To show that the ULP is doing something, it just increments the value <data>.
It does that once per ulp timer wakeup (and then the ULP halts until it gets
waked up via the timer again).

The timer is set to a rather long period, so you can watch the data value
incrementing (see loop at the end).
"""

from machine import ULP
from machine import mem32
import utime
import machine

from esp32_ulp.__main__ import src_to_binary

source = """\
data:       .long 0

entry:      move r3, data    # load address of data into r3
            ld r2, r3, 0     # load data contents ([r3+0]) into r2
            add r2, r2, 1    # increment r2
            st r2, r3, 0     # store r2 contents into data ([r3+0])
            halt             # halt ULP co-processor (until next wake-up)
"""


print ("Wake Reason:",machine.wake_reason())
binary = src_to_binary(source)

load_addr, entry_addr = 0, 4

ULP_MEM_BASE = 0x50000000
ULP_DATA_MASK = 0xffff  # ULP data is only in lower 16 bits

ulp = ULP()
ulp.set_wakeup_period(0, 1000000)  # use timer0, wakeup after 1s
ulp.load_binary(load_addr, binary)

mem32[ULP_MEM_BASE + load_addr] = 0x1000
ulp.run(entry_addr)

while True:
    print(hex(mem32[ULP_MEM_BASE + load_addr] & ULP_DATA_MASK))
    utime.sleep(1)
