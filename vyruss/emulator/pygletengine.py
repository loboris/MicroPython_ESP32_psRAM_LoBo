import config
import pyglet
# Force using OpenAL since pulse crashes
pyglet.options['audio'] = ('openal', 'silent')
import math
import random
from pyglet.gl import *
from pyglet.window import key
from struct import pack, unpack
from deepspace import deepspace

fps_display = pyglet.clock.ClockDisplay()


sounds = {}
for sn in ["shoot1", "explosion2", "explosion3", "shoot3"]:
    sounds[bytes(sn, "latin1")] = pyglet.media.load("sounds/%s.wav" % sn, streaming=False)

sound_queue = []
def playsound(name):
    if name in sounds:
        sound_queue.append(sounds[name])

import imagenes
image_stripes = {
    "0": imagenes.galaga_png,
    "1": imagenes.numerals_png,
    "3": imagenes.disparo_png,
    # "4": imagenes.crawling_png,
    "4": imagenes.ll9_png,
    # "4": imagenes._07_skorpion_png,
    "5": imagenes.explosion_png,
    "6": imagenes.explosion_nave_png
}
spritedata = bytearray( b"\0\0\0\0\x10\0\0\2\x20\0\0\4\x30\0\0\6\x40\0\0\x08\x50\0\0\x0A"
+ b"\0\0\0\xff" * 58)

window = pyglet.window.Window(config=Config(double_buffer=True), fullscreen=config.FULLSCREEN)
keys = key.KeyStateHandler()

joysticks = pyglet.input.get_joysticks()
if joysticks:
    joystick = joysticks[0]
    joystick.open()
else:
    joystick = None

LED_DOT = 6
LED_SIZE = min(window.width, window.height) / 2
R_ALPHA = max(window.height, window.width)
ROWS = 256
COLUMNS = 256

TRANSPARENT = 0xFF

STARS = COLUMNS // 2
starfield = [(random.randrange(COLUMNS), random.randrange(ROWS)) for n in range(STARS)]

glLoadIdentity()
glEnable(GL_BLEND)
#glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE)

def change_colors(colors):
    # byteswap all longs
    fmt_unpack = "<" + "L" * (len(colors)//4)
    fmt_pack = ">" + "L" * (len(colors)//4)
    b = unpack(fmt_unpack, colors)
    return pack(fmt_pack, *b)

def pack_colors(colors):
    fmt_pack = "<" + "L" * len(colors)
    return pack(fmt_pack, *colors)

def unpack_palette(pal):
    fmt_unpack = "<" + "L" * (len(pal)//4)
    return unpack(fmt_unpack, pal)

def ungamma(values, gamma=2.5, offset=0.5):
    d = []
    for v in values:
        i = int(pow(((float(v) + offset) / 255.0), 1.0/gamma) * 255.0)
        d.append(i)
    return bytes(d)

palette = ungamma(change_colors(imagenes.palette_pal))
upalette = unpack_palette(palette)


class PygletEngine():
    def __init__(self, led_count, keyhandler):
        self.led_count = led_count
        self.total_angle = 0
        self.last_sent = 0
        self.keyhandler = keyhandler
        led_step = (LED_SIZE / led_count)

        vertex_pos = []
        theta = (math.pi * 2 / COLUMNS)
        def arc_chord(r):
            return 2 * r * math.sin(theta / 2)

        x1, x2 = 0, 0
        for i in range(led_count):
            y1 = led_step * i
            y2 = y1 + (led_step * 1)
            x3 = arc_chord(y2) * 0.7
            x4 = -x3
            vertex_pos.extend([x1, y1, x2, y1, x2, y2, x1, y2])
            x1, x2 = x3, x4

        vertex_colors = (0, 0, 0, 255) * led_count * 4
        texture_pos = (0,0, 1,0, 1,1, 0,1) * led_count

        self.vertex_list = pyglet.graphics.vertex_list(
            led_count * 4,
            ('v2f/static', vertex_pos),
            ('c4B/stream', vertex_colors),
            ('t2f/static', texture_pos))


        texture = pyglet.image.load("glow.png").get_texture(rectangle=True)


        def send_keys():
            reset = keys[key.P]
            try:
                left = joystick.x < -0.5
                right = joystick.x > 0.5
                up = joystick.y < -0.5
                down = joystick.y > 0.5

                boton = joystick.buttons[1]
                accel = joystick.rz > 0
                decel = joystick.z > 0
            except Exception:
                left = keys[key.LEFT]
                right = keys[key.RIGHT]
                up = keys[key.UP]
                down = keys[key.DOWN]

                boton = keys[key.SPACE]
                accel = keys[key.A]
                decel = keys[key.D]

            val = (left << 0 | right << 1 | up << 2 | down << 3 | boton << 4 |
                    accel << 5 | decel << 6 | reset << 7)

            if val != self.last_sent:
                self.keyhandler(bytes([val]))
                self.last_sent = val

        self.i = 0
        def render_anim(column):
            self.i = (self.i+1) % (204*4)
            return palette[self.i:self.i+led_count*4]

        def get_visible_column(sprite_x, sprite_width, render_column):
            sprite_column = (render_column - sprite_x + COLUMNS) % COLUMNS
            if 0 <= sprite_column < sprite_width:
                return sprite_column
            else:
                return -1

        def step_starfield():
            for (n, (x, y)) in enumerate(starfield):
                y -= 1
                if y < 0:
                    y = ROWS - 1
                    x = random.randrange(COLUMNS)
                starfield[n] = (x, y)

        def render(column):
            pixels = [0x00000000] * led_count * 4

            for (x,y) in starfield:
                if x == column:
                    try:
                        px = deepspace[y] * 4
                        pixels[px:px+4] = [0xff404040] * 4
                    except:
                        print(y, deepspace)

            # el sprite 0 se dibuja arriba de todos los otros
            for n in range(63, -1, -1):
                x, y, image, frame = unpack("BBBb", spritedata[n*4:n*4+4])
                if frame == -1:
                    continue

                strip = image_stripes["%d" % image]
                w, h, total_frames, _pal = unpack("BBBB", strip[0:4])
                pixeldata = memoryview(strip)[4:]

                frame %= total_frames

                visible_column = get_visible_column(x, w, column)
                if visible_column != -1:
                    desde = max(y, 0)
                    hasta = min(y + h, ROWS - 1)
                    comienzo = max( -y, 0)
                    base = visible_column * h + (frame * w * h)
                    src = base + comienzo

                    for y in range(desde, hasta):
                        index = pixeldata[src]
                        src += 1
                        if index != TRANSPARENT:
                            color = upalette[index]
                            px = deepspace[y] * 4
                            pixels[px:px+4] = [color] * 4

            return pack_colors(pixels)

        @window.event
        def on_draw():
            window.clear()
            fps_display.draw()

            angle = -(360.0 / 256.0)

            glTranslatef(window.width / 2, window.height / 2, 0)
            glRotatef(180, 0, 0, 1)
            glEnable(texture.target)
            glBindTexture(texture.target, texture.id)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
            for column in range(256):
                limit = len(self.vertex_list.colors)
                self.vertex_list.colors[:] = render(column)[0:limit]
                self.vertex_list.draw(GL_QUADS)
                glRotatef(angle, 0, 0, 1)
            glDisable(texture.target)
            glRotatef(180, 0, 0, 1)
            glTranslatef(-window.width / 2, -window.height / 2, 0)
            step_starfield()


        def animate(dt):
            send_keys()
            while sound_queue:
                sound_queue.pop().play()
            return
            "FIXME"
            for n in range(6):
                val = spritedata[n*4 + 1] - n
                if (val > 127 and val < 200):
                    #val = 256 - 16
                    val = 127
                spritedata[n*4 + 1] = val % 256
                spritedata[n*4] = (spritedata[n*4] + n - 3) % 256

        pyglet.clock.schedule_interval(animate, 1/30.0)
        pyglet.app.run()

window.push_handlers(keys)
