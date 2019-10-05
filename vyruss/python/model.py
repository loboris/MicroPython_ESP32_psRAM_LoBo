import spritelib
import comms

def audio_play(track):
    comms.send("audio_play " + track)

DISABLED_FRAME = -1
sprite_num = 1

def new_sprite():
    global sprite_num
    sprite = spritelib.get_sprite(sprite_num)
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
        for n in range(1, 64):
            sp = spritelib.get_sprite(n)
            sp.frame = DISABLED_FRAME
        global sprite_num
        sprite_num = 1
        self.setup()

    def step(self):
        pass

    def change_state(self):
        pass

    def fire(self):
        pass

    def heading(self, up, down, left, right):
        pass

    def accel(self, accel, decel):
        pass

def new_heading(up, down, left, right):
    """
       128
    96↖ ↑ ↗ 160
    64←   → 192
    32↙ ↓ ↘ 224
        0
    """
    if up:
        if left:
            return 96
        elif right:
            return 160
        else:
            return 128

    if down:
        if left:
            return 32
        elif right:
            return 224
        else:
            return 0

    if left:
        return 64

    if right:
        return 192

    return None


def rotar(desde, hasta):
    delta_centro = 128 - desde
    nuevo_hasta = (hasta + delta_centro) % 256

    if nuevo_hasta < 128:
        return -1
    if nuevo_hasta > 128:
        return +1

    return 0


class Fleet(Scene):
    def setup(self):
        self.state = StateEntering(self)
        self.starfighter = StarFighter()
        self.laser = Laser()
        self.explosions = []
     
    def change_state(self):
        self.state = self.state.next_state(self)

    def explode_baddie(self, baddie):
        self.everyone.remove(baddie)
        self.state.remove_baddie(baddie)
        explosion = baddie.explode()
        self.explosions.append(explosion)

    def step(self):
        self.state.step()
        if self.laser.enabled:
            self.laser.step()
            hit = self.laser.collision(self.everyone)
            if hit:
                self.laser.finish()
                audio_play("explosion2")
                self.explode_baddie(hit)

        baddie = self.starfighter.collision(self.everyone)
        if baddie:
            audio_play("explosion3")
            self.explode_baddie(baddie)
            # TODO: explode the starship
            pass

        for e in self.explosions:
            e.step()
            if e.finished:
                self.explosions.remove(e)

    def fire(self):
        if not self.laser.enabled:
            self.laser.fire(self.starfighter)
            audio_play("shoot1")

    def heading(self, up, down, left, right):
        where = new_heading(up, down, left, right)
        if where is not None:
            where = where - 8 # ancho de la nave
            self.starfighter.step(where)

    def accel(self, accel, decel):
        self.starfighter.accel(accel, decel)


class FleetState:
    def __init__(self, fleet):
        self.fleet = fleet
        self.setup()

    def remove_baddie(self, baddie):
        pass

    def step(self):
        pass

class StateDefeated(FleetState):
    def setup(self):
        print("everyone defeated")
        pass

    def step(self):
        pass

class StateAttacking(FleetState):
    next_state = StateDefeated

    def setup(self):
        self.attacking = []

    def remove_baddie(self, baddie):
        if baddie in self.attacking:
            self.attacking.remove(baddie)

    def step(self):
        for baddie in self.fleet.everyone:
            baddie.step()

        if len(self.fleet.everyone) == 0:
            self.fleet.change_state()
        elif len(self.attacking) < 2:
            baddie = self.fleet.everyone[0]
            delta = baddie.sprite.y - 16
            baddie.movements = [TravelCloser(delta), TravelAway(delta), Hover()]
            self.attacking.append(baddie)

class StateEntering(FleetState):
    next_state = StateAttacking

    def setup(self):
        self.phase = 0
        self.steps = 0
        self.groups = []
        self.fleet.everyone = []
        self.create_group()
        # [int(x * 18.285714285714285 + 0.5) for x in range(14) ], shuffled by hand
        self.final_x_pos = [0, 128, 55, 183, 18, 73, 146, 201, 37, 91, 238, 110, 165, 219]
        self.final_y_pos = [128, 110, 92, 74]
        self.bases = [128-8, 224-8, 32-8, 256-8, 128-8]
        self.num_baddies = 0

    def create_group(self):
        self.groups.append([])

    def add_baddie(self):
        final_x = self.final_x_pos[self.num_baddies % 14]
        final_y = self.final_y_pos[self.num_baddies // 14]
        self.num_baddies += 1

        picture = (self.num_baddies % 5) * 2 + 2

        base_x = self.bases[len(self.groups)-1]

        baddie = Baddie(picture)
        baddie.sprite.y = 128 + 32
        if len(self.groups[-1]) % 2:
            baddie.sprite.x = base_x + 16
            baddie.movements = [
                TravelCloser(80), TravelX(112),
                TravelCloser(32), TravelX(-96),
                TravelAway(42), TravelTo(final_x, final_y), Hover()
            ] 
        else:
            baddie.sprite.x = base_x - 16
            baddie.movements = [
                TravelCloser(80), TravelX(-112),
                TravelCloser(32), TravelX(96),
                TravelAway(42), TravelTo(final_x, final_y), Hover()
            ] 

        self.groups[-1].append(baddie)
        self.fleet.everyone.append(baddie)

    def all_baddies_in_last_group_exploded(self):
        g = self.groups[-1]
        return g and all(b.exploded for b in g)

    def step(self):
        for baddie in self.fleet.everyone:
            baddie.step()

        self.steps += 1

        if self.steps % 8 == 0 and len(self.groups[-1]) < 10:
            self.add_baddie()

        if self.steps % 256 == 0 or self.all_baddies_in_last_group_exploded():
            self.steps = 0
            if len(self.groups) < 5:
                self.create_group()
            else:
                self.fleet.change_state()

class Sprite:
    def __init__(self, strip, x=0, y=0, frame=DISABLED_FRAME):
        self.sprite = new_sprite()
        self.sprite.image_strip = strip
        self.sprite.x = x
        self.sprite.y = y
        self.sprite.frame = frame

    def collision(self, targets):
        this = self.sprite
        for target in targets:
            other = target.sprite
            if (this.x < other.x + spritelib.sprite_width(other) and
                this.x + spritelib.sprite_width(this) > other.x and
                this.y < other.y + spritelib.sprite_height(other) and
                this.y + spritelib.sprite_height(this) > other.y):
                return target

class StarFighter(Sprite):
    def __init__(self):
        super().__init__(strip=4, x=256-8, y=16, frame=0)


    def step(self, where):
        current_x = self.sprite.x
        self.sprite.x = (current_x + rotar(current_x, where) * 2) % 256

    def accel(self, accel, decel):
        if accel:
            self.sprite.y -= 1

        if decel:
            self.sprite.y += 1

        if not accel and not decel:
            self.sprite.y = 16


class Laser(Sprite):
    def __init__(self):
        super().__init__(strip=3, x=48, y=12)
        self.enabled = False

    def fire(self, starfighter):
        self.enabled = True
        self.sprite.frame = 0
        self.sprite.y = starfighter.sprite.y + 11
        self.sprite.x = starfighter.sprite.x + 6

    def finish(self):
        self.enabled = False
        self.sprite.frame = DISABLED_FRAME

    def step(self):
        LASER_SPEED = 6
        self.sprite.y += LASER_SPEED
        if self.sprite.y > 170:
            self.finish()


class BaddieExploding(Sprite):
    def __init__(self, baddie):
        self.sprite = baddie.sprite
        self.sprite.frame = 0
        self.sprite.image_strip = 5
        self.finished = False

    def step(self):
        self.sprite.frame += 1
        if self.sprite.frame == 9:
            self.sprite.frame = DISABLED_FRAME
            self.finished = True

class Baddie(Sprite):
    def __init__(self, base_frame):
        super().__init__(strip=0)
        self.base_frame = base_frame
        self.frame_step = 0
        self.exploded = False

    def step(self):
        self.frame_step += 1
        self.sprite.frame = (not (self.frame_step & 8)) + self.base_frame
        if self.movements:
            movement = self.movements[0]
            movement.step(self.sprite)
            if (movement.finished(self.sprite)):
                self.movements.pop(0)
    
    def explode(self):
        self.exploded = True
        return BaddieExploding(self)

class Movement:
    pass

class FollowPath(Movement):
    pass

class TravelTo(Movement):
    def __init__(self, x, y):
        self.dest_x = x
        self.dest_y = y

    def step(self, sprite):
        sprite.x += calculate_direction(sprite.x, self.dest_x) * 4
        sprite.y += calculate_direction(sprite.y, self.dest_y) * 2

    def finished(self, sprite):
        return sprite.x == self.dest_x and sprite.y == self.dest_y

class TravelBy(Movement):
    def __init__(self, count, speed=None):
        self.count = abs(count)
        self.speed = -1 if count < 0 else 1

    def finished(self, sprite):
        return self.count <= 0

X_SPEED = 3
Y_SPEED = 2

class TravelX(TravelBy):
    def step(self, sprite):
        if self.count > 0:
            sprite.x += X_SPEED * self.speed

        self.count -= X_SPEED

class TravelCloser(TravelBy):
    def step(self, sprite):
        sprite.y -= Y_SPEED
        self.count -= Y_SPEED

class TravelAway(TravelBy):
    def step(self, sprite):
        sprite.y += Y_SPEED
        self.count -= Y_SPEED

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
