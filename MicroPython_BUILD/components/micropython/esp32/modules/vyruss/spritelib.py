import povsprites
import uctypes

sprite_struct = {
    "image_data": (uctypes.PTR | 0, uctypes.UINT8),
    "image": uctypes.UINT32 | 0,
    "x": uctypes.UINT8 | 4,
    "y": uctypes.INT8 | 5,
    "width": uctypes.UINT8 | 6,
    "height": uctypes.UINT8 | 7,
    "frame": uctypes.UINT8 | 8,
    "enabled": uctypes.UINT8 | 9,
}

PIXELS=52
SPRITES=-1
COLUMNS=-1

povsprites.init(PIXELS, COLUMNS, SPRITES)

def set_palette(palette):
    povsprites.set_palette(palette)

def get_sprite(num_sprite, pixmap, width, height, frames):
    data = povsprites.getaddress(num_sprite)
    sp = uctypes.struct(data, sprite_struct) #, uctypes.LITLE_ENDIAN)
    #sp.image = uctypes.addressof(pixmap)
    #sp.width = width
    #sp.height = height
    return sp

def debug(s):
    print("nave@%x (%d,%d,%d,%d) %d %x" % (s.image, s.x, s.y, s.width, s.height, s.frame, s.enabled))
