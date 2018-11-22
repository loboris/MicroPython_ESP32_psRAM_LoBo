import uctypes

data = bytearray(b"\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc")

def init(num_pixels, columns, num_sprites):
    pass

def sprite_x(sprite_num, x):
    pass

def sprite_y(sprite_num, y):
    pass

def getaddress(sprite_num):
    return uctypes.addressof(data)
