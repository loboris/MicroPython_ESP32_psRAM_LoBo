#!/usr/bin/env python3

from PIL import Image
import os
import sys
from itertools import zip_longest, chain
import struct

TRANSPARENT = (255, 0, 255)
FOLDER = "galaga"

# width, height, frames, palette
attributes = {
  "disparo.png": (3, 8, 2, 0),
  "explosion.png": (32, 32, 5, 0),
  "explosion_nave.png": (32, 32, 4, 0),
  "galaga.png": (16, 16, 12, 0),
  "ll9.png": (16, 16, 4, 0),
  "gameover.png": (64, 20, 1, 0),
  "crawling.png": (72, 228, 1, 0),
  "numerals.png": (4, 5, 12, 0),

  "00_galaga.png": (16, 16, 28, 0),
  "01_captured.png": (16, 16, 28, 0),
  "02_greenboss.png": (16, 16, 28, 0),
  "03_blueboss.png": (16, 16, 28, 0),
  "04_redmoth.png": (16, 16, 28, 0),
  "05_bluebee.png": (16, 16, 28, 0),
  "06_galaxian.png": (16, 16, 28, 0),
  "07_skorpion.png": (16, 16, 28, 0),
  "08_greenshit.png": (16, 16, 28, 0),
  "09_dumbbug.png": (16, 16, 28, 0),
  "10_newsat.png": (16, 16, 28, 0),
  "11_spock.png": (16, 16, 28, 0),

  "tierra_flat.png": (255, 25, 1, 0),
  "marte_flat.png": (255, 25, 1, 0),
  "jupiter_flat.png": (255, 25, 1, 0),
  "saturno_flat.png": (255, 50, 1, 0),
  "letters.png": (20, 20, 120, 0),
  "vga_cp437.png": (9, 16, 255, 0),
  "vga_pc734.png": (9, 16, 255, 0),
  "ready.png": (48, 8, 2, 0),
  "copyright.png": (128, 8, 1, 0),

  "tecno_estructuras_flat.png": (255, 54, 1, 0),
  "ventilastation_flat.png": (255, 54, 1, 0),
  "doom_flat.png": (255, 54, 1, 0),
  "sves_flat.png": (255, 54, 1, 0),
  "yourgame_flat.png": (255, 54, 1, 0),
  "menatwork_flat.png": (255, 40, 1, 0),
  "vlad_farting_flat.png": (255, 54, 1, 0),
  "farty_lion_flat.png": (255, 54, 1, 0),
  "farty_lionhead_flat.png": (255, 54, 1, 0),
  "bg64_flat.png": (255, 54, 1, 0),
  "bgspeccy_flat.png": (255, 54, 1, 0),
  "reset.png": (128, 53, 5, 0),

  "menu.png": (64, 30, 4, 0),
  "credits.png": (64, 16, 32, 0),
}

def grouper(iterable, n, fillvalue=None):
    "Collect data into fixed-length chunks or blocks"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx"
    args = [iter(iterable)] * n
    return zip_longest(*args, fillvalue=fillvalue)

filenames = [f for f in os.listdir(FOLDER) if f.endswith(".png")]

images = [Image.open(os.path.join(FOLDER, f)) for f in filenames]

w_size = (sum(i.width for i in images), max(i.height for i in images))

workspace = Image.new("RGB", w_size, (255,0,255))

x = 0
for i in images:
    workspace.paste(i, (x, 0, x+i.width, i.height))
    x+=i.width

workspace.save("w1.png")

import pprint
workspace = workspace.convert("P", palette=Image.ADAPTIVE, dither=0)
palette = list(grouper(workspace.getpalette(), 3))
mi = palette.index(TRANSPARENT)
palorder = list(range(256))
palorder[255] = mi
palorder[mi] = 255
workspace = workspace.remap_palette(palorder)
pprint.pprint(list(grouper(workspace.getpalette(), 3)), stream=sys.stderr)

palette = list(grouper(workspace.getpalette(), 3))
mi = palette.index(TRANSPARENT)
for n, c in enumerate(palette):
    if (c == TRANSPARENT or c == (254, 0, 254)) and n != 255:
        palette[n] = (255, 255, 0)
print("transparent index=", palette.index(TRANSPARENT), file=sys.stderr)
workspace.putpalette(chain.from_iterable(palette))
workspace.save("w2.png")

def gamma(value, gamma=2.5, offset=0.5):
    assert 0 <= value <= 255
    return int( pow( float(value) / 255.0, gamma ) * 255.0 + offset )
    
#print("unsigned long palette_pal[] PROGMEM = {")

pal_raw = []
with open("raw/palette.pal", "wb") as pal:
    for c in palette:
        r, g, b = c
        #if (r, g, b) == TRANSPARENT:
            #r, g, b = 255, 255, 0
        #r, g, b = gamma(r), gamma(g), gamma(b)
        quad = bytearray((255, b, g, r))
        pal.write(quad)
        #print("    0x%02x%02x%02x%02x," % (r, g, b, 255))
        pal_raw.append(quad)

#print("};")
#print()

print("palette_pal =", b"".join(pal_raw))
print()

palettes = [b"".join(pal_raw)]

raws = []
sizes = []

rom_strips = []

for (j, i) in enumerate(images):
    p = i.convert("RGB").quantize(palette=workspace, colors=256, method=Image.FASTOCTREE, dither=Image.NONE)
    p.save("debug/xx%02d.png" % j)
    b = p.transpose(Image.ROTATE_270).tobytes()
    filename = i.filename.rsplit("/", 1)[-1]
    attrs = bytes(attributes[filename])

    var_name = filename.replace(".", "_")
    if var_name.startswith("0") or var_name.startswith("1"):
        var_name = "_" + var_name
    print(var_name, "=", repr(attrs + b))
    #print("unsigned char %s[] PROGMEM = {" % var_name)
    for g in grouper(("0x%02x" % n for n in b), 8):
        #print("    " + ", ".join(g) + ",")
        pass
    #print("};")
    print()
    rom_strips.append(struct.pack("<16s", var_name.encode("utf-8")) + attrs + b)
    
    if ("fondo.png" in i.filename):
        fn = "raw/" + i.filename.rsplit(".", 1)[0] + ".raw"
        with open(fn, "wb") as raw:
            raw.write(b)
    else:
        raws.append(b)
        sizes.append(p.size)

with open("raw/images.raw", "wb") as raw:
    raw.write(b"".join(raws))

with open("sprites.rom", "wb") as rom:
    offset = 4 + len(rom_strips) * 4 + len(palettes) * 4
    rom.write(struct.pack("<HH", len(rom_strips), len(palettes)))

    for strip in rom_strips:
        rom.write(struct.pack("<L", offset))
        offset += len(strip)
    for palette in palettes:
        rom.write(struct.pack("<L", offset))
        offset += len(palette)
        
    for strip in rom_strips:
        rom.write(strip)

    for palette in palettes:
        rom.write(palette)
