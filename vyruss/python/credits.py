from director import director
from scene import Scene
from sprites import Sprite

SPEED = 4
TITLE_DELAYS = 3000
END_OF_TITLES = 583


def make_me_a_planet(n):
    planet = Sprite()
    planet.set_strip(n)
    planet.set_perspective(0)
    planet.set_x(0)
    planet.set_y(255)
    return planet


class Credits(Scene):

    def on_enter(self):
        director.music_play("credits")
        self.vs = make_me_a_planet(15)
        self.vs.set_frame(0)
        self.te = make_me_a_planet(16)
        self.sves = make_me_a_planet(14)
        self.counter = 0
        self.y = 0
        self.sprites = []
        for n in range(31, -1, -1):
            sprite = Sprite()
            sprite.set_x(256 - 32)
            sprite.set_y(0)
            sprite.set_strip(8)
            sprite.set_perspective(1)
            sprite.set_frame(n)
            self.sprites.append(sprite)
        self.sprites.reverse()
        self.call_later(TITLE_DELAYS, self.start_scrolling)
        self.scrolling = False

    def start_scrolling(self):
        self.vs.disable()
        self.scrolling = True

    def move(self, direction):
        self.y += direction
        if self.y < 0:
            self.y = 0
        if self.y > END_OF_TITLES:
            self.y = END_OF_TITLES


        for n, sprite in enumerate(self.sprites):
            this_y = self.y - n * 16
            if this_y >= 0 and this_y < 255:
                sprite.set_y(this_y)

        if self.y == END_OF_TITLES:
            self.scrolling = False
            self.te.set_frame(0)
            self.call_later(TITLE_DELAYS, self.end_credit)

    def step(self):
        self.counter = self.counter + 1

        if director.was_pressed(director.BUTTON_D):
            self.finished()

        if self.scrolling:
            if director.is_pressed(director.JOY_UP):
                self.move(1)
            elif director.is_pressed(director.JOY_DOWN):
                self.move(-1)
            elif self.counter % SPEED == 0:
                self.move(1)

    def end_credit(self):
        self.te.disable()
        self.sves.set_frame(0)
        self.call_later(TITLE_DELAYS, self.finished)

    def finished(self):
        director.pop()
        raise StopIteration()