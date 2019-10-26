from spritelib import *

DISABLED_FRAME = -1
sprite_num = 1


def new_sprite():
    global sprite_num
    sprite = get_sprite(sprite_num)
    sprite_num += 1
    return sprite

def reset_sprites():
    for n in range(1, 64):
        sp = get_sprite(n)
        sp.frame = DISABLED_FRAME
    global sprite_num
    sprite_num = 1


class Sprite:
    def __init__(self, replacing=None):
        if replacing:
            self._sprite = replacing._sprite    
        else:
            self._sprite = new_sprite()
        self.set_frame(DISABLED_FRAME)

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

    def collision(self, targets):
        for target in targets:
            other = target
            if (self.x() < other.x() + other.width() and
                self.x() + self.width() > other.x() and
                self.y() < other.y() + other.height() and
                self.y() + self.height() > other.y()):
                return target
