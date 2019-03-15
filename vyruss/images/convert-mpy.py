#!/usr/bin/env python3

from PIL import Image
import os
from itertools import zip_longest

MAGENTA = (255, 0, 255)
FOLDER = "galaga"

# width, height, frames, palette
attributes = {
  "disparo.png": (3, 8, 1, 0),
  "explosion.png": (32, 32, 9, 0),
  "galaga.10.png": (10, 10, 12, 0),
  "galaga.8.png": (8, 8, 12, 0),
  "galaga.alt10.png": (10, 10, 12, 0),
  "galaga.alt8.png": (8, 8, 12, 0),
  "galaga.png": (16, 16, 12, 0),
  "ll9.png": (16, 16, 1, 0),
  "gameover.png": (64, 20, 1, 0),
}

def grouper(iterable, n, fillvalue=None):
    "Collect data into fixed-length chunks or blocks"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx"
    args = [iter(iterable)] * n
    return zip_longest(*args, fillvalue=fillvalue)

filenames = [f for f in os.listdir(FOLDER) if f.endswith(".png")]

images = [Image.open(os.path.join(FOLDER, f)) for f in filenames]

w_size = (sum(i.width for i in images), max(i.height for i in images))

workspace = Image.new("RGB", w_size, (0,0,0))

x = 0
for i in images:
    workspace.paste(i, (x, 0, x+i.width, i.height))
    x+=i.width

#workspace.save("w1.png")

workspace = workspace.convert("P", palette=Image.ADAPTIVE)
palette = list(grouper(workspace.getpalette(), 3))
mi = palette.index(MAGENTA)
palorder = list(range(256))
palorder[255] = mi
palorder[mi] = 255
workspace = workspace.remap_palette(palorder)
palette = list(grouper(workspace.getpalette(), 3))
mi = palette.index(MAGENTA)
#workspace.save("w2.png")

def gamma(value, gamma=2.5, offset=0.5):
    assert 0 <= value <= 255
    return int( pow( float(value) / 255.0, gamma ) * 255.0 + offset )
    
#print("unsigned long palette_pal[] PROGMEM = {")

pal_raw = []
with open("raw/palette.pal", "wb") as pal:
    for c in palette:
        r, g, b = c
        r, g, b = gamma(r), gamma(g), gamma(b)
        quad = bytearray((255, b, g, r))
        pal.write(quad)
        #print("    0x%02x%02x%02x%02x," % (r, g, b, 255))
        pal_raw.append(quad)

#print("};")
#print()

print("palette_pal =", b"".join(pal_raw))
print()

raws = []
sizes = []


for i in images:
    p = i.convert("RGB").quantize(palette=workspace)
    b = p.transpose(Image.ROTATE_270).tobytes()
    filename = i.filename.rsplit("/", 1)[-1]
    attrs = bytes(attributes[filename])

    var_name = filename.replace(".", "_")
    print(var_name, "=", repr(attrs + b))
    #print("unsigned char %s[] PROGMEM = {" % var_name)
    for g in grouper(("0x%02x" % n for n in b), 8):
        #print("    " + ", ".join(g) + ",")
        pass
    #print("};")
    print()
    
    if ("fondo.png" in i.filename):
        fn = "raw/" + i.filename.rsplit(".", 1)[0] + ".raw"
        with open(fn, "wb") as raw:
            raw.write(b)
    else:
        raws.append(b)
        sizes.append(p.size)

with open("raw/images.raw", "wb") as raw:
    raw.write(b"".join(raws))
