from spritelib import *

DISABLED_FRAME = 255
sprite_num = 1


def new_sprite():
    global sprite_num
    sprite = get_sprite(sprite_num)
    sprite_num += 1
    return sprite

def reset_sprites():
    for n in range(0, 100):
        sp = get_sprite(n)
        sp.frame = DISABLED_FRAME
        sp.image_strip = 4
        sp.x = 0
        sp.y = 0
        sp.perspective = 1
    global sprite_num
    sprite_num = 1


class Sprite:
    def __init__(self, replacing=None):
        # print(sprite_num, self.__class__)
        if replacing:
            self._sprite = replacing._sprite    
        else:
            self._sprite = new_sprite()
        self.set_frame(DISABLED_FRAME)
        self.set_x(0)
        self.set_y(0)
        self.set_perspective(True)

    def disable(self):
        self.set_frame(DISABLED_FRAME)

    def x(self):
        return self._sprite.x
    
    def set_x(self, value):
        self._sprite.x = value

    def y(self):
        return self._sprite.y
    
    def set_y(self, value):
        self._sprite.y = value

    def width(self):
        return stripes[self._sprite.image_strip][0]

    def height(self):
        return stripes[self._sprite.image_strip][1]

    def set_strip(self, strip_number):
        self._sprite.image_strip = strip_number
    
    def frame(self):
        return self._sprite.frame
    
    def set_frame(self, value):
        self._sprite.frame = value

    def set_perspective(self, value):
        self._sprite.perspective = value

    def collision(self, targets):
        def intersects(x1, w1, x2, w2):
            delta = min(x1, x2)
            x1 = (x1 - delta + 128) % 256
            x2 = (x2 - delta + 128) % 256
            return x1 < x2 + w2 and x1 + w1 > x2
            
        for target in targets:
            other = target
            if (intersects(self.x(), self.width(), other.x(), other.width()) and
                intersects(self.y(), self.height(), other.y(), other.height())):
                return target
