import pyglet
from pyglet.gl import *
from pyglet.window import key
from struct import pack, unpack

fps_display = pyglet.clock.ClockDisplay()

import imagenes
image_stripes = {"0": imagenes.galaga_png}
spritedata = bytearray(b"\0\0\0\0\x10\0\0\2\x20\0\0\4\x30\0\0\6\x40\0\0\x08\x50\0\0\x0A"
+ b"\0\0\0\xff" * 58)

window = pyglet.window.Window(config=Config(double_buffer=True), fullscreen=True)
keys = key.KeyStateHandler()

LED_DOT = 6
LED_SIZE = min(window.width, window.height) / 1.9
R_ALPHA = max(window.height, window.width)
ROWS = 128
COLUMNS = 256

TRANSPARENT = 0xFF
deepspace = [51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36,
35, 35, 34, 33, 32, 31, 30, 30, 29, 28, 27, 27, 26, 25, 25, 24, 23, 23, 22, 21,
21, 20, 19, 19, 18, 18, 17, 17, 16, 16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11,
11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 3,
3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]

glLoadIdentity()
glEnable(GL_BLEND)
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
glTranslatef(window.width / 2, window.height / 2, 0)

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

palette = change_colors(imagenes.palette_pal)
upalette = unpack_palette(palette)


class PygletEngine():
    def __init__(self, led_count, keyhandler):
        self.led_count = led_count
        self.total_angle = 0
        self.last_sent = 0
        self.keyhandler = keyhandler
        led_step = int(LED_SIZE / led_count)

        vertex_pos = []
        for i in range(led_count):
            vertex_pos.extend([0, led_step * i])

        self.vertex_list = pyglet.graphics.vertex_list(
            led_count,
            ('v2i', vertex_pos),
            ('c4B', (0, 0, 0, 255) * led_count))

        glRotatef(180, 0, 0, 1)

        
        def send_keys():
            left = keys[key.LEFT]
            right = keys[key.RIGHT]
            up = keys[key.UP]
            down = keys[key.DOWN]

            boton = keys[key.SPACE]
            accel = keys[key.A]
            decel = keys[key.D]

            val = (left << 0 | right << 1 | up << 2 | down << 3 | boton << 4 |
                    accel << 5 | decel << 6)

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

        def render(column):
            pixels = [0x00000000] * led_count

            # el sprite 0 se dibuja arriba de todos los otros
            for n in range(63, -1, -1):
                x, y, image, frame = unpack("BbBb", spritedata[n*4:n*4+4])
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
                        color = pixeldata[src]
                        src += 1
                        if color != TRANSPARENT:
                            pixels[deepspace[y]] = upalette[color]

            return pack_colors(pixels)

        @window.event
        def on_draw():
            window.clear()

            angle = -(360.0 / 256.0)

            pyglet.gl.glPointSize(LED_DOT)
            for column in range(256):
                self.vertex_list.colors[:] = render(column)
                self.vertex_list.draw(GL_POINTS)
                glRotatef(angle, 0, 0, 1)

            fps_display.draw()

        def animate(dt):
            send_keys()
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
