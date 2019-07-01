import spritelib

sprite_num = 0

def new_sprite():
    global sprite_num
    sprite = spritelib.create_sprite(4+sprite_num)
    sprite_num += 1
    return sprite

def calculate_direction(current, destination):
    center_delta = 128 - current
    new_destination = (destination + center_delta) % 256
    if new_destination < 128:
        return -1
    if new_destination > 128:
        return +1
    return 0

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
        if len(self.groups[-1]) % 2:
            baddie = Baddie(10)
            baddie.sprite.x = 144-8
            baddie.sprite.y = 128
            baddie.movements = [
                TravelCloser(80), TravelX(112),
                TravelCloser(32), TravelX(-96),
                TravelAway(72), Hover()
            ] 
        else:
            baddie = Baddie(2)
            baddie.sprite.x = 112-8
            baddie.sprite.y = 128
            baddie.movements = [
                TravelCloser(80), TravelX(-112),
                TravelCloser(32), TravelX(96),
                TravelAway(72), Hover()
            ] 

        self.groups[-1].append(baddie)

    def step(self):
        for group in self.groups:
            for baddie in group:
                baddie.step()

        if self.steps % 16 == 0 and len(self.groups[-1]) < 10:
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
    def __init__(self, base_frame):
        self.sprite = new_sprite()
        self.sprite.image_strip = 0
        self.sprite.frame = 2
        self.base_frame = base_frame
        self.frame_step = 0

    def step(self):
        self.frame_step += 1
        self.sprite.frame = (not (self.frame_step & 8)) + self.base_frame
        if self.movements:
            movement = self.movements[0]
            movement.step(self.sprite)
            if (movement.finished(self.sprite)):
                self.movements.pop(0)

class Movement:
    pass

class FollowPath(Movement):
    pass

class TravelTo(Movement):
    def __init__(self, x, y):
        self.dest_x = x
        self.dest_y = y

    def step(self, sprite):
        sprite.x += calculate_direction(sprite.x, self.dest_x) * 2
        sprite.y += calculate_direction(sprite.y, self.dest_y) * 1

    def finished(self, sprite):
        return sprite.x == self.dest_x and sprite.y == self.dest_y

class TravelBy(Movement):
    def __init__(self, count, speed=None):
        self.count = abs(count)
        self.speed = -1 if count < 0 else 1

    def finished(self, sprite):
        return self.count <= 0

class TravelX(TravelBy):
    def step(self, sprite):
        if self.count > 0:
            sprite.x += 2 * self.speed

        self.count -= 2

class TravelCloser(TravelBy):
    def step(self, sprite):
        sprite.y -= 1
        self.count -= 1

class TravelAway(TravelBy):
    def step(self, sprite):
        sprite.y += 1
        self.count -= 1

class Hover(Movement):
    """Hover around the current position."""

    def __init__(self):
        self.dx = 0
        self.dy = 0
        self.vx = 1
        self.vy = 1

    def step(self, sprite):
        pass

    def finished(self, sprite):
        return False

class Chase(Movement):
    pass
