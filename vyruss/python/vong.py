from director import director
from scene import Scene
from sprites import Sprite, reset_sprites

def make_me_a_planet(n):
    planet = Sprite()
    planet.set_strip(n)
    planet.set_perspective(0)
    planet.set_x(0)
    planet.set_y(255)
    return planet

class VongGame(Scene):
    def on_enter(self):
        self.planet = make_me_a_planet(18)
        self.planet.set_frame(0)

    def step(self):
        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()