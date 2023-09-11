from urandom import choice, randrange, seed
import utime

from director import director
from scene import Scene
from sprites import Sprite, reset_sprites


LEVELS = [
    # oleadas, planeta, disparos_simultaneos
    (2, 13, 3),
    (3, 12, 4),
    (4, 11, 6),
    (5, 10, 8),
]

MAX_BOMBS = max(l[2] for l in LEVELS)
MAX_GROUPS = max(l[0] for l in LEVELS)
BADDIES_PER_GROUP = 10

def calculate_direction(current, destination):
    center_delta = 128 - current
    new_destination = (destination + center_delta) % 256
    if new_destination < 128:
        return -1
    if new_destination > 128:
        return +1
    return 0


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


def make_me_a_planet(n):
    planet = Sprite()
    planet.set_strip(n)
    planet.set_perspective(0)
    planet.set_x(0)
    y = 62
    planet.set_y(y)
    return planet

class ScoreBoard:
    def __init__(self):
        self.chars = []
        for n in range(9):
            s = Sprite()
            s.set_strip(1)
            s.set_x(110 + n * 4)
            s.set_y(0)
            s.set_frame(10)
            s.set_perspective(2)
            self.chars.append(s)

        self.setscore(0)
        self.setlives(3)

    def setscore(self, value):
        for n, l in enumerate("%05d" % value):
            v = ord(l) - 0x30
            self.chars[n].set_frame(v)

    def setlives(self, lives):
        for n in range(6, 9):
            if lives > n-6:
                self.chars[n].set_frame(11)
            else:
                self.chars[n].set_frame(10)

class StarfleetState:
    def __init__(self, scene):
        self.game_over_sprite = Sprite()
        self.game_over_sprite.set_x(256-32)
        self.game_over_sprite.set_y(0)
        self.game_over_sprite.set_perspective(2)
        self.game_over_sprite.set_strip(2)

        self.fighters = [StarFighter() for n in range(3)]
        self.destroyed = []
        self.fighter = self.fighters[0]
        self.exploded = False
        self.scene = scene

    def step(self):
        self.fighter.step()

    def game_over(self):
        director.music_play("vy-gameover")
        self.game_over_sprite.set_frame(0)
        self.scene.call_later(8333, self.scene.finished)

    def respawn(self):
        self.destroyed.append(self.fighters.pop(0))
        self.fighter = self.fighters[0]
        self.fighter.set_frame(0)
        self.exploded = False

    def explode(self):
        self.fighter.explode()
        remaining_lives = len(self.fighters) - 1
        self.scene.scoreboard.setlives(remaining_lives)
        self.exploded = True
        director.sound_play(b"explosion3")
        if remaining_lives:
            self.scene.call_later(1500, self.respawn)
        else:
            self.game_over()

    def collision(self, others):
        if self.fighter:
            return self.fighter.collision(others)

    def slide(self, where):
        if not self.exploded:
            self.fighter.slide(where)

    def accel(self, accel, decel):
        pass
        # if not self.exploded:
        #     self.fighter.accel(accel, decel)

class VyrusGame(Scene):
    def __init__(self):
        super(VyrusGame, self).__init__()
        seed(utime.ticks_ms())

    def on_enter(self):
        self.scoreboard = ScoreBoard()
        self.hiscore = 0
        self.starfleet = StarfleetState(self)
        self.killed = []
        self.laser = Laser()
        self.level = 0
        self.active_bombs = []
        total_buddies = MAX_GROUPS * BADDIES_PER_GROUP
        self.all_baddies = [Baddie() for _ in range(total_buddies)]
        self.all_bombs = [Bomb() for _ in range(MAX_BOMBS)]
        self.planet = make_me_a_planet(13)
        self.used_baddie = 0
        self.start_level()

    def start_level(self):
        self.state = StateEntering(self)
        self.waves, planet_number, unfired_bombs = LEVELS[self.level]
        self.unfired_bombs = self.all_bombs[0:unfired_bombs]
        self.starfleet.fighter.reset()
        self.planet.disable()
        self.planet.set_strip(planet_number)
        self.used_baddie = 0
        self.everyone = []
        self.explosions = []
        director.music_play("vy-main")

    def getBaddie(self, base_picture):
        b = self.all_baddies[self.used_baddie]
        b.reset(base_picture)
        self.used_baddie += 1
        return b

    def change_state(self):
        self.state = self.state.next_state(self)

    def explode_baddie(self, baddie):
        self.hiscore += randrange(10, 19)
        self.scoreboard.setscore(self.hiscore)
        self.everyone.remove(baddie)
        self.state.remove_baddie(baddie)
        explosion = baddie.explode()
        self.explosions.append(explosion)

    def process_input(self):
        if director.was_pressed(director.BUTTON_A):
            self.fire()

        accel = director.is_pressed(director.BUTTON_B)
        decel = director.is_pressed(director.BUTTON_C)
        self.starfleet.accel(accel, decel)

        up = director.is_pressed(director.JOY_UP)
        down = director.is_pressed(director.JOY_DOWN)
        left = director.is_pressed(director.JOY_LEFT)
        right = director.is_pressed(director.JOY_RIGHT)
        self.heading(up, down, left, right)

        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()

    def step(self):
        self.process_input()

        self.state.step()
        if self.laser.enabled:
            self.laser.step()
            #print("everyone", self.everyone)
            hit = self.laser.collision(self.everyone)
            if hit:
                self.laser.finish()
                director.sound_play(b"explosion2")
                self.explode_baddie(hit)

        self.starfleet.step()
        if not self.starfleet.exploded:
            bomb = self.starfleet.collision(self.active_bombs)
            if bomb:
                self.starfleet.explode()
                bomb.disable()
                self.active_bombs.remove(bomb)
                self.unfired_bombs.append(bomb)
            else:
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

    def finished(self):
        director.pop()
        raise StopIteration()

    def advance_level(self):
        self.level += 1
        if self.level >= len(LEVELS):
            director.pop()
            raise StopIteration()
        self.start_level()


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
        director.music_play("vy-3warps")
        self.fleet.planet.set_y(0)
        self.fleet.planet.set_frame(0)
        self.animating_planet = True
        self.animating_ship = False
        self.fleet.call_later(4000, self.warp_ahead)

    def step(self):
        if self.animating_planet:
            new_y = self.fleet.planet.y() + 1
            self.fleet.planet.set_y(new_y)
            if new_y == 255:
                self.animating_planet = False
                self.fleet.call_later(1500, self.finished)
        if self.animating_ship:
            sf = self.fleet.starfleet.fighter
            new_y = sf.y() + 2
            if new_y > 250:
                self.animating_ship = False
            sf.set_y(new_y)

    def warp_ahead(self):
        self.animating_ship = True

    def finished(self):
        self.fleet.advance_level()


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
        self.max_attacking = 1

    def remove_baddie(self, baddie):
        if baddie in self.attacking:
            self.attacking.remove(baddie)
            self.max_attacking += 1

    def step(self):
        for baddie in self.fleet.everyone:
            baddie.step()
            if baddie.finished:
                self.remove_baddie(baddie)

        if len(self.fleet.everyone) == 0:
            self.fleet.change_state()
        elif len(self.attacking) < self.max_attacking:
            baddie = choice(self.fleet.everyone)
            if baddie not in self.attacking:
                delta = baddie.y() - 16
                baddie.movements = [TravelCloser(delta), TravelAway(delta)]
                self.attacking.append(baddie)
                self.fleet.drop_bomb()


class StateEntering(FleetState):
    next_state = StateAttacking

    def setup(self):
        print("setupando")
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

        baddie = self.fleet.getBaddie(picture)
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

        if self.steps % 8 == 0 and len(self.groups[-1]) < BADDIES_PER_GROUP:
            self.add_baddie()

        if self.all_baddies_in_last_group_finished() or \
           self.all_baddies_in_last_group_exploded():
            self.steps = 0
            if len(self.groups) < self.fleet.waves:
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
    
    def reset(self):
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

    def __init__(self):
        super().__init__()
        self.reset()

    def reset(self):
        super().reset()
        self.set_x(256-8)
        self.set_y(16)
        self.set_strip(4)
        self.frame_counter = -1
        self.step = self.starship_step

    def starship_step(self):
        if not self.exploded:
            self.frame_counter = (self.frame_counter + 1) % self.BLINK_RATE
            if self.frame_counter in self.keyframes:
                self.set_frame(self.keyframes[self.frame_counter])

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
    def reset(self, base_frame):
        super().reset()
        self.set_strip(0)
        self.base_frame = base_frame
        self.frame_step = 0
        self.step = self.baddie_step
        self.finished = False

    def baddie_step(self):
        self.frame_step += 1
        self.set_frame((not (self.frame_step & 8)) + self.base_frame)
        if self.movements:
            self.finished = False
            movement = self.movements[0]
            movement.step(self)
            if movement.finished(self):
                self.movements.pop(0)
        elif not self.finished:
            self.finished = True


class Laser(Sprite):
    def __init__(self):
        super().__init__()
        self.set_strip(3)
        self.enabled = False

    def fire(self, starfighter):
        director.sound_play(b"shoot1")
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
        director.sound_play(b"shoot3")
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
