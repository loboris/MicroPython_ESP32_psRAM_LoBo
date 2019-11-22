from director import director
from scene import Scene
from sprites import Sprite, reset_sprites

INITIAL = 62

def make_me_a_planet(n):
    planet = Sprite()
    planet.set_strip(n)
    planet.set_perspective(0)
    planet.set_x(0)
    y = INITIAL  #54 - planet.height()
    planet.set_y(y)
    return planet


class VentilagonGame(Scene):
    def on_enter(self):
        self.planet_id = 7
        self.planet = make_me_a_planet(17)
        self.planet.set_frame(0)
        self.call_later(30, self.animate)
    
    def animate(self):
        current_y = self.planet.y()
        if current_y < 255:
            self.planet.set_y(current_y + 1)
        self.call_later(30, self.animate)

    def step(self):
        if director.was_pressed(director.BUTTON_A):
            self.planet_id = (self.planet_id + 1) % 9
            self.planet.set_strip(self.planet_id + 10)
            self.planet.set_y(INITIAL)

        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()