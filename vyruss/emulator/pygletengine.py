import pyglet
from pyglet.gl import *
from pyglet.window import key
from struct import pack, unpack

fps_display = pyglet.clock.ClockDisplay()

import imagenes
image_stripes = {"0": imagenes.galaga_png}
spritedata = bytearray(b"\0\0\0\0\x10\0\0\2\x20\0\0\4\x30\0\0\6\x40\0\0\x08\x50\0\0\x0A" * 16)

window = pyglet.window.Window(config=Config(double_buffer=True))

LED_DOT = 4
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


key_press = {
    key.LEFT: b'L',
    key.RIGHT: b'R',
    key.UP: b'U',
    key.DOWN: b'D',
    key.SPACE: b'S',
}

key_release = {
    key.LEFT: b'l',
    key.RIGHT: b'r',
    key.UP: b'u',
    key.DOWN: b'd',
    key.SPACE: b's',
}

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
    def __init__(self, led_count, line_iterator, vsync, keyhandler, revs_per_second):
        self.led_count = led_count
        self.line_iterator = line_iterator
        self.total_angle = 0
        self.vsync = vsync
        self.keyhandler = keyhandler
        self.revs_per_second = revs_per_second
        led_step = int(LED_SIZE / led_count)

        vertex_pos = []
        for i in range(led_count):
            vertex_pos.extend([0, led_step * i])

        self.vertex_list = pyglet.graphics.vertex_list(
            led_count,
            ('v2i', vertex_pos),
            ('c4B', (0, 0, 0, 255) * led_count))

        glRotatef(180, 0, 0, 1)


        @window.event
        def on_key_press(symbol, modifiers):
            if symbol in key_press:
                self.keyhandler(key_press[symbol])

        @window.event
        def on_key_release(symbol, modifiers):
            if symbol in key_release:
                self.keyhandler(key_release[symbol])

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

                strip = image_stripes[str(image)]
                w, h, tf, p = unpack("BBBB", strip[0:4])
                pixeldata = memoryview(strip)[4:]

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

            angle = 360.0 / 256.0

            pyglet.gl.glPointSize(LED_DOT)
            for column in range(256):
                self.vertex_list.colors[:] = render(column)
                self.vertex_list.draw(GL_POINTS)
                glRotatef(angle, 0, 0, 1)

            #fps_display.draw()

        def x(dt):
            spritedata[1] = (spritedata[1] + 1) % 256
            spritedata[5] = (spritedata[5] + 1) % 256
            spritedata[9] = (spritedata[9] + 1) % 256
            spritedata[13] = (spritedata[13] + 1) % 256
            spritedata[17] = (spritedata[17] + 1) % 256
            spritedata[21] = (spritedata[21] + 1) % 256
            spritedata[25] = (spritedata[25] + 1) % 256
            spritedata[29] = (spritedata[29] + 1) % 256

        pyglet.clock.schedule_interval(x, 1/30.0)
        pyglet.app.run()
