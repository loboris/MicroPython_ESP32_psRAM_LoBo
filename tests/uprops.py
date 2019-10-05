class Sprite:
    def __init__(self):
        self._x = 1
        self.y = 2

    @property
    def x(self):
        return self._x

    @x.setter
    def x(self, value):
        self._x = value


s = Sprite()
print(s.x, s.y)
s.x = 9
s.y = 8
print(s.x, s.y)
