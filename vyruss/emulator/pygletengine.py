import pyglet
from pyglet.gl import *
from pyglet.window import key
from struct import pack, unpack

import imagenes
image_stripes = {"0": imagenes.galaga_png}
spritedata = b"\0\0\0\0" * 64

window = pyglet.window.Window(config=Config(double_buffer=True))

LED_DOT = 4
LED_SIZE = min(window.width, window.height) / 1.9
R_ALPHA = max(window.height, window.width)

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

palette = change_colors(imagenes.palette_pal)


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
        def render(column):
            self.i = (self.i+1) % (204*4)
            return palette[self.i:self.i+led_count*4]

        @window.event
        def on_draw():
            window.clear()

            angle = 360.0 / 256.0

            pyglet.gl.glPointSize(LED_DOT)
            for column in range(256):
                self.vertex_list.colors[:] = render(column)
                self.vertex_list.draw(GL_POINTS)
                glRotatef(angle, 0, 0, 1)

        def x(dt):
            pass

        pyglet.clock.schedule_interval(x, 1/30.0)
        pyglet.app.run()
