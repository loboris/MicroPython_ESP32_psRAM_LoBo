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
    def __init__(self, strip, x=0, y=0, frame=DISABLED_FRAME):
        self._sprite = new_sprite()
        self.strip = strip
        self.x = x
        self.y = y
        self.frame = frame

    def disable(self):
        self.frame = DISABLED_FRAME

    @property
    def x(self):
        return self._sprite.x
    
    @x.setter
    def x(self, value):
        self._sprite.x = value

    @property
    def y(self):
        return self._sprite.y
    
    @y.setter
    def y(self, value):
        self._sprite.y = value

    @property
    def width(self):
        return stripes[self._sprite.image_strip][0]

    @property
    def height(self):
        return stripes[self._sprite.image_strip][1]

    def strip(self, strip_number):
        self._sprite.image_strip = strip_number
    strip = property(None, strip)

    @property
    def frame(self):
        return self._sprite.frame
    
    @frame.setter
    def frame(self, value):
        self._sprite.frame = value

    def collision(self, targets):
        for target in targets:
            other = target
            if (self.x < other.x + other.width and
                self.x + self.width > other.x and
                self.y < other.y + other.height and
                self.y + self.height > other.y):
                return target
