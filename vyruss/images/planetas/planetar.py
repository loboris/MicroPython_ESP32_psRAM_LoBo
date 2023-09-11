import sys
import os
from to_led import to_led

PATH = "imagenes"

n_leds = {
    "tierra": 25,
    "marte": 25,
    "jupiter": 25,
    "saturno": 50,
    "menatwork": 40,
}

allfiles = os.listdir(PATH)

for fn in allfiles:
    if "flat" in fn:
        continue

    outfilename = fn.rsplit(".", 1)[0] + "_flat.png"
    if outfilename in allfiles:
        continue


    n_led = 54
    for k, v in n_leds.items():
        if fn.startswith(k):
            n_led = v
    planet_bytes = to_led(path=os.path.join(PATH, fn), n_led=n_led)
    print(fn) #, len(planet_bytes))
