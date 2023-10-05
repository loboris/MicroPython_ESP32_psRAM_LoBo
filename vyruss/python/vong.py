from director import director
from scene import Scene
from sprites import Sprite, reset_sprites

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
    planet.set_y(255)
    return planet

class Letter(Sprite):
  def __init__(self, char):
    super().__init__()
    self.set_strip(20)
    self.set_frame(ord(char))
    self.enabled = True
    self.set_x(256-4)
    self.set_y(0)
    self.set_perspective(2)

class VongGame(Scene):
    def on_enter(self):
        self.planet = make_me_a_planet(18)
        self.planet.set_frame(0)
        phrase = "Welcome to Vlad Farty"
        self.visible_letters = []
        for e, l in enumerate(phrase):
          letter = Letter(l)
          letter.set_x(e*9)
	  self.visible_letters.append(letter)

    def step(self):
        if director.is_pressed(director.JOY_DOWN) and director.is_pressed(director.JOY_LEFT) and director.was_pressed(director.BUTTON_A):
            self.planet.set_strip(19)

        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()
