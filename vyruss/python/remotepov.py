import comms
import uctypes

sprite_data = bytearray(b"\0\0\0\xff\xff" * 100)
stripes = {}

def init(num_pixels, palette):
    comms.send(b"palette", palette)

def getaddress(sprite_num):
    return uctypes.addressof(sprite_data) + sprite_num * 5

def set_imagestrip(n, stripmap):
    stripes[n] = stripmap
    comms.send(b"imagestrip %s %d" % (n, len(stripmap)), stripmap)

def update():
    comms.send(b"sprites", sprite_data)
