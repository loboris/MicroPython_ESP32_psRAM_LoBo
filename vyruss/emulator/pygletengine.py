import pyglet
from pyglet.gl import *
from pyglet.window import key
from struct import pack, unpack

window = pyglet.window.Window(config=Config(double_buffer=False))

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


class PygletEngine():
    def __init__(self, led_count, line_iterator, vsync, keyhandler, revs_per_second):
        self.led_count = led_count
        self.line_iterator = line_iterator
        self.total_angle = 0
        self.vsync = vsync
        self.keyhandler = keyhandler
        self.revs_per_second = revs_per_second
        led_step = int(LED_SIZE / led_count)
        self.fmt_pack = "<" + "L" * led_count
        self.fmt_unpack = "<" + "L" * led_count

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

        pyglet.clock.schedule_interval(self.update, 1/10000)
        self.loop()

    def loop(self):
        #pyglet.app.run()
        while True:
            pyglet.clock.tick()
            window.dispatch_events()

    def get_colors(self):
        return next(self.line_iterator)

    def draw_black(self):
        glColor4f(0, 0, 0, 0.005)
        pyglet.graphics.draw_indexed(4, pyglet.gl.GL_TRIANGLES,
                                     [0, 1, 2, 0, 2, 3],
                                     ('v2i', (R_ALPHA, -R_ALPHA,
                                              R_ALPHA, R_ALPHA,
                                              -R_ALPHA, R_ALPHA,
                                              -R_ALPHA, -R_ALPHA)))

    def change_colors(self, colors):
        # byteswap all longs
        b = unpack(self.fmt_unpack, colors)
        colors = pack(self.fmt_pack, *b)

        self.vertex_list.colors[:] = colors
        return

    def update(self, dt):
        angle = 360 * self.revs_per_second * dt

        colors = c2 = self.get_colors()
        while c2:
            colors = c2
            c2 = self.get_colors()
        if colors:
            self.change_colors(colors)

        self.draw_black()
        pyglet.gl.glPointSize(LED_DOT)

        ROT=4
        frac_angle = -angle/ROT
        for n in range(ROT):
            self.vertex_list.draw(GL_POINTS)
            glRotatef(frac_angle, 0, 0, 1)

        self.total_angle += angle

        if self.total_angle > 360:
            self.vsync()
            self.total_angle -= 360

        glFlush()

