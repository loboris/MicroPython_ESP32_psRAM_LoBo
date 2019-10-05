class Sprite:
    def __init__(self):
        self._x = 1
        self.y = 2
        self._z = 2

    @property
    def x(self):
        return self._x

    @x.setter
    def x(self, value):
        self._x = value


    def z(self, value):
        self._z = value
    z = property(None, z)

s = Sprite()
print(s.x, s.y)
s.x = 9
s.y = 8
s.z = 10
print(s.x, s.y, s._z)
