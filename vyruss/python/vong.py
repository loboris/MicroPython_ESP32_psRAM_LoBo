from director import director
from scene import Scene
from sprites import Sprite, reset_sprites


class VongGame(Scene):
    def step(self):
        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()