try:
    import remotepov as povdisplay
except:
    import povdisplay

import uctypes

sprite_struct = {
    "x": uctypes.UINT8 | 0,
    "y": uctypes.UINT8 | 1,
    "image_strip": uctypes.UINT8 | 2,
    "frame": uctypes.UINT8 | 3,
    "perspective": uctypes.INT8 | 4,
}

stripes = {}

def get_sprite(num_sprite):
    data = povdisplay.getaddress(num_sprite)
    sp = uctypes.struct(data, sprite_struct) #, uctypes.LITLE_ENDIAN)
    return sp

def set_imagestrip(n, stripmap):
    stripes[n] = stripmap
    povdisplay.set_imagestrip(n, stripmap)

def debug(s):
    print("nave@%d (%d,%d,%d,%d) %d" % (s.image_strip, s.x, s.y, 0, 0, s.frame))
