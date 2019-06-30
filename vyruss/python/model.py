import spritelib

sprite_num = 0

def new_sprite():
    global sprite_num
    sprite = spritelib.create_sprite(4+sprite_num)
    sprite_num += 1
    return sprite


class Scene:
    def __init__(self):
        self.setup()

    def step(self):
        pass
    
class Fleet(Scene):
    def setup(self):
        self.state = StateEntering(self)

    def step(self):
        self.state.step()

class FleetState:
    def __init__(self, fleet):
        self.fleet = fleet
        self.setup()

    def step(self):
        pass

class StateEntering(FleetState):
    def setup(self):
        self.phase = 0
        self.steps = 0
        self.groups = []
        self.create_group()

    def create_group(self):
        self.groups.append([])

    def add_baddie(self):
        baddie = Baddie()
        baddie.setup()
        self.groups[-1].append(baddie)

    def step(self):
        for group in self.groups:
            for baddie in group:
                baddie.step()

        if self.steps % 24 == 0 and len(self.groups[-1]) < 5:
            self.add_baddie()

        self.steps += 1

class StateAttacking(FleetState):
    def setup(self):
        pass

    def step(self):
        pass

class StateDefeated(FleetState):
    def setup(self):
        pass

    def step(self):
        pass

class Baddie:
    def setup(self, rotation=1):
        self.sprite = new_sprite()
        self.sprite.image_strip = 0
        self.sprite.frame = 2
        self.sprite.x = 32
        self.sprite.y = -20
        self.rotation = rotation
        self.movements = [TravelTo(100,0), TravelTo(0, 64), TravelTo(0, 128), TravelTo(0, 128), Hover()]

    def step(self):
        self.sprite.x += self.rotation
        self.sprite.y += 1
        if self.sprite.y < -100:
            self.sprite.y = -20
        self.sprite.frame = (not (self.sprite.y) & 8) + 8


class Movement:
    pass

class FollowPath(Movement):
    pass

class TravelTo(Movement):
    pass

class Hover(Movement):
    """Hover around the current position."""
    def __init__(self):
        self.dx = 0
        self.dy = 0
        self

    def step(self):
        pass

class Chase(Movement):
    pass
