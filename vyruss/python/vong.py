from director import director
from scene import Scene
from sprites import Sprite, reset_sprites
from urandom import randrange

phrase = """
[demo group name tbd] welcomes you to Vlad Farty, arguably the first intro for Persistence of Vision, PoV displays.
We got this big ass fan, bolted some LEDs into it, and worked our way from Arduino up to the ESP32, which freaking rulez. So, you can now enjoy games AND demos!

A big kudos to our friends and family:

Club de Jaqueo
Tecnoestructuras
Videogamo
Cybercirujas
PVM
PyAr
"""

def make_me_a_planet(n):
    planet = Sprite()
    planet.set_strip(n)
    planet.set_perspective(0)
    planet.set_x(0)
    planet.set_y(0)
    return planet

class Letter(Sprite):
  def __init__(self, char):
    super().__init__()
    self.set_strip(20)
    self.set_frame(ord(char))
    self.enabled = True
    self.set_x(256)
    self.set_y(16)
    self.set_perspective(1)

#t = list(range(16,32,1)) + list(range(32,16,-1))
#t = [24, 26, 27, 28, 30, 31, 31, 32, 32, 32, 31, 31, 30, 28, 27, 26, 24, 22, 21, 20, 18, 17, 17, 16, 16, 16, 17, 17, 18, 20, 21, 22]
t = [24, 25, 26, 26, 27, 27, 28, 28, 28, 28, 28, 27, 27, 26, 26, 25, 24, 23, 22, 22, 21, 21, 20, 20, 20, 20, 20, 21, 21, 22, 22, 23]



tlen = len(t)

class VongGame(Scene):
    def on_enter(self):
        phrase = "Welcome to Vlad Farty"
        self.visible_letters = []
        for e, l in enumerate(phrase):
          letter = Letter(l)
          letter.set_x(94 - 9 - e*9)
	  self.visible_letters.append(letter)

        self.vlad_farty = make_me_a_planet(21)
        self.farty_lion = make_me_a_planet(22)
        self.planet = self.farty_lion
        self.planet.set_y(100)
        self.planet.set_frame(0)
        self.n = 0

    def step(self):
        self.n = self.n + 1
        new_y = self.planet.y() + 1
        if new_y < 256:
            self.planet.set_y(new_y)
        self.planet.set_x(t[self.n%tlen]-24)

        for l in self.visible_letters:
            x = l.x()
            l.set_x(x + 1)
            y = t[x % tlen] - 8
            l.set_y(y)
            #l.set_y(randrange(16,32))

        if director.is_pressed(director.JOY_DOWN) and director.is_pressed(director.JOY_LEFT) and director.was_pressed(director.BUTTON_A):
            self.planet.set_strip(19)

        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()
