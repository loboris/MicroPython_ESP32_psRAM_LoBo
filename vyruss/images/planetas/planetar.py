import sys
import os
from to_led import to_led

PATH = "imagenes"

for fn in os.listdir(PATH):
    if "flat" in fn:
        continue

    n_led = 50 if "saturno" in fn else 25
    planet_bytes = to_led(path=os.path.join(PATH, fn), n_led=n_led)
    print(fn, len(planet_bytes))
