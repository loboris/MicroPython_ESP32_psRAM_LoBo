import comms
from urandom import seed, choice, randrange
import utime
#import random

# TODO: para hacerlo más random esto debería suceder después de que
# le jugadore toca el botón para iniciar la partida
seed(utime.ticks_ms())

from sprites import Sprite, reset_sprites


def audio_play(track):
    comms.send(b"audio_play " + track)


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
        reset_sprites()
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


class StarfleetState:
    def __init__(self):
        self.fighters = [StarFighter(n) for n in [0]] #[-1, 0, 1]]
        self.fighter = self.fighters[0]
        self.exploded = False

    def step(self):
        for f in self.fighters:
            f.step()

    def explode(self):
        self.fighter.explode()
        self.exploded = True
        audio_play(b"explosion3")
        # TODO: what happens after the ship explodes?

    def collision(self, others):
        if self.fighter:
            return self.fighter.collision(others)

    def slide(self, where):
        if not self.exploded:
            for f in self.fighters:
                f.slide(where)

    def accel(self, accel, decel):
        if not self.exploded:
            for f in self.fighters:
                f.accel(accel, decel)

class Fleet(Scene):
    def setup(self):
        self.state = StateEntering(self)
        self.starfleet = StarfleetState()
        self.killed = []
        self.laser = Laser()
        self.level = 4
        self.unfired_bombs = [Bomb() for _ in range(self.level)]
        self.active_bombs = []
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
            #print("everyone", self.everyone)
            hit = self.laser.collision(self.everyone)
            if hit:
                self.laser.finish()
                audio_play(b"explosion2")
                self.explode_baddie(hit)

        self.starfleet.step()
        if not self.starfleet.exploded:
            bomb = self.starfleet.collision(self.active_bombs)
            if bomb:
                self.starfleet.explode()
                bomb.disable()
                self.active_bombs.remove(bomb)
                self.unfired_bombs.append(bomb)

            baddie = self.starfleet.collision(self.everyone)
            if baddie:
                self.starfleet.explode()
                if isinstance(baddie, Explodable):
                    self.explode_baddie(baddie)

        for e in self.explosions:
            if not e.finished:
                e.step()
                # por ahora no lo removemos, para que no se vaya de scope
                # self.explosions.remove(e)

        for bomb in self.active_bombs:
            bomb.step()
            if not bomb.enabled:
                self.active_bombs.remove(bomb)
                self.unfired_bombs.append(bomb)

    def fire(self):
        if not self.laser.enabled and not self.starfleet.exploded:
            self.laser.fire(self.starfleet.fighter)

    def drop_bomb(self):
        if self.everyone and self.unfired_bombs:
            bomb = self.unfired_bombs.pop()
            bomb.fire(choice(self.everyone))
            self.active_bombs.append(bomb)

    def heading(self, up, down, left, right):
        where = new_heading(up, down, left, right)
        if where is not None:
            where = where - 8 # ancho de la nave
            self.starfleet.slide(where)

    def accel(self, accel, decel):
        self.starfleet.accel(accel, decel)


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


class StateResetting(FleetState):
    def setup(self):
        print("restarting state")

    def step(self):
        StateResetting.next_state = StateEntering
        reset_sprites()
        self.fleet.setup()


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
            delta = baddie.y() - 16
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
        self.next_bomb = 37

    def create_group(self):
        self.groups.append([])

    def add_baddie(self):
        final_x = self.final_x_pos[self.num_baddies % 14]
        final_y = self.final_y_pos[self.num_baddies // 14]
        self.num_baddies += 1

        picture = (self.num_baddies % 5) * 2 + 2

        base_x = self.bases[len(self.groups)-1]

        baddie = Baddie(picture)
        baddie.set_y(128 + 32)
        if len(self.groups[-1]) % 2:
            baddie.set_x(base_x + 16)
            baddie.movements = [
                TravelCloser(80), TravelX(112),
                TravelCloser(32), TravelX(-96),
                TravelAway(42), TravelTo(final_x, final_y),
            ] 
        else:
            baddie.set_x(base_x - 16)
            baddie.movements = [
                TravelCloser(80), TravelX(-112),
                TravelCloser(32), TravelX(96),
                TravelAway(42), TravelTo(final_x, final_y),
            ] 

        self.groups[-1].append(baddie)
        self.fleet.everyone.append(baddie)

    def all_baddies_in_last_group_exploded(self):
        g = self.groups[-1]
        return g and all(b.exploded for b in g)

    def all_baddies_in_last_group_finished(self):
        g = self.groups[-1]
        return g and all(b.finished for b in g)

    def step(self):
        for baddie in self.fleet.everyone:
            baddie.step()

        self.steps += 1

        if self.steps % 8 == 0 and len(self.groups[-1]) < 10:
            self.add_baddie()

        if self.all_baddies_in_last_group_finished() or \
           self.all_baddies_in_last_group_exploded():
            self.steps = 0
            if len(self.groups) < 5:
                self.create_group()
            else:
                self.fleet.change_state()

        self.next_bomb -= 1
        if self.next_bomb == 0:
            self.fleet.drop_bomb()
            self.next_bomb = 20 + randrange(30)


class Explodable(Sprite):
    explosion_strip = 5
    explosion_steps = 5

    def __init__(self):
        super().__init__()
        self.exploded = False
        self.step = self.dummy_step

    def explode(self):
        self.exploded = True
        # hay que apagar mientras se cambia el stripe y reubica
        self.disable()
        # es necesario calcular el centro antes de cambiar el strip
        center_x = self.x() + self.width() // 2
        center_y = self.y() + self.height() // 2
        self.set_strip(self.explosion_strip)
        self.set_x(center_x - self.width() // 2)
        self.set_y(center_y - self.height() // 2)
        # recién ahora arranca el contador de frames
        self.set_frame(0)
        self.finished = False
        self.step = self.exploded_step
        return self

    def dummy_step(self):
        pass

    def exploded_step(self):
        if self.finished:
            return
        self.set_frame(self.frame() + 1)
        if self.frame() == self.explosion_steps:
            self.disable()
            self.finished = True


class StarFighter(Explodable):
    explosion_strip = 6
    explosion_steps = 4

    BLINK_RATE = int(30.0 * 1.5) 
    keyframes = {
        int(BLINK_RATE * 0) : 0,
        int(BLINK_RATE * 0.333333333): 1,
        int(BLINK_RATE * 0.5): 2,
        int(BLINK_RATE * 0.833333333): 3,
    }

    def __init__(self, n):
        #super().__init__(strip=4, x=256-8, y=16, frame=0)
        super().__init__()
        self.set_x(256-8 - n * 18)
        self.set_y(16)
        self.set_strip(4)
        self.set_frame(0)
        self.frame_counter = 0
        self.step = self.starship_step

    def starship_step(self):
        if not self.exploded:
            self.frame_counter = (self.frame_counter + 1) % self.BLINK_RATE
            if self.frame_counter in self.keyframes:
                self.set_frame(self.keyframes[self.frame_counter])
#            self.set_frame(int(self.frame_counter/3.214285714))

    def slide(self, where):
        if not self.exploded:
            current_x = self.x()
            self.set_x((current_x + rotar(current_x, where) * 2) % 256)

    def accel(self, accel, decel):
        if self.exploded:
            return

        if accel:
            self.set_y(self.y() - 1)

        if decel:
            self.set_y(self.y() + 1)

        if not accel and not decel:
            self.set_y(16)


class Baddie(Explodable):
    def __init__(self, base_frame):
        super().__init__()
        self.set_strip(0)
        self.base_frame = base_frame
        self.frame_step = 0
        self.step = self.baddie_step
        self.finished = False

    def baddie_step(self):
        self.frame_step += 1
        self.set_frame((not (self.frame_step & 8)) + self.base_frame)
        if self.movements:
            movement = self.movements[0]
            movement.step(self)
            if (movement.finished(self)):
                self.movements.pop(0)
        elif not self.finished:
            self.finished = True
    

class Laser(Sprite):
    def __init__(self):
        super().__init__()
        self.set_strip(3)
        self.enabled = False

    def fire(self, starfighter):
        audio_play(b"shoot1")
        self.enabled = True
        self.set_y(starfighter.y() + 11)
        self.set_x(starfighter.x() + 6)
        self.set_frame(0)

    def finish(self):
        self.enabled = False
        self.disable()

    def step(self):
        LASER_SPEED = 6
        self.set_y(self.y() + LASER_SPEED)
        if self.y() > 170:
            self.finish()


class Bomb(Sprite):
    def __init__(self):
        super().__init__()
        self.set_strip(3)
        self.enabled = False

    def fire(self, baddie):
        audio_play(b"shoot3")
        self.enabled = True
        self.set_y(baddie.y() + 11)
        self.set_x(baddie.x() + 6)
        self.set_frame(1)

    def finish(self):
        self.enabled = False
        self.disable()

    def step(self):
        BOMB_SPEED = 3
        self.set_y(self.y() - BOMB_SPEED)
        if self.y() < 6:
            self.finish()


class Movement:
    pass


class FollowPath(Movement):
    pass


X_SPEED = 3
Y_SPEED = 2


class TravelTo(Movement):
    def __init__(self, x, y):
        self.dest_x = x
        self.dest_y = y

    def step(self, sprite):
        sprite.set_x(sprite.x() + calculate_direction(sprite.x(), self.dest_x) * X_SPEED)
        sprite.set_y(sprite.y() + calculate_direction(sprite.y(), self.dest_y) * Y_SPEED)
        
    def finished(self, sprite):
        return abs(sprite.x() - self.dest_x) < X_SPEED and abs(sprite.y() - self.dest_y) < Y_SPEED


class TravelBy(Movement):
    def __init__(self, count, speed=None):
        self.count = abs(count)
        self.speed = -1 if count < 0 else 1

    def finished(self, sprite):
        return self.count <= 0


class TravelX(TravelBy):
    def step(self, sprite):
        if self.count > 0:
            sprite.set_x(sprite.x() + X_SPEED * self.speed)

        self.count -= X_SPEED


class TravelCloser(TravelBy):
    def step(self, sprite):
        sprite.set_y(sprite.y() - Y_SPEED)
        self.count -= Y_SPEED


class TravelAway(TravelBy):
    def step(self, sprite):
        sprite.set_y(sprite.y() + Y_SPEED)
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
