import comms
import sys
from itertools import cycle
from pygletengine import PygletEngine


led_count = 54
if len(sys.argv) >= 2:
    led_count = int(sys.argv[1])

try:
    PygletEngine(led_count, comms.send)
finally:
    comms.shutdown()
