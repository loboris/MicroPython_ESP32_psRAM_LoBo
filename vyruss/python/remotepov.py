import comms
import uctypes

sprite_data = bytearray(b"\0\0\0\xff" * 64)
stripes = {}

def init(num_pixels, palette):
    comms.send("palette", palette)

def getaddress(sprite_num):
    return uctypes.addressof(sprite_data) + sprite_num * 4

def set_imagestrip(n, stripmap):
    stripes[n] = stripmap
    comms.send("imagestrip %s %d" % (n, len(stripmap)), stripmap)

def update():
    comms.send("sprites", sprite_data)
