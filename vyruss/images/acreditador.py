#!/usr/bin/env python3

from PIL import Image

cr = Image.open("credits.png")

rodaja = 16
num_rodajas = cr.height // rodaja
new_size = (cr.width * num_rodajas, 16)

workspace = Image.new("RGB", new_size, (255, 0, 255))

for j in range(num_rodajas):
    r = cr.crop((0, j * rodaja, cr.width, (j+1) * rodaja))
    workspace.paste(r, (cr.width * j, 0))

workspace.save("galaga/credits.png")
