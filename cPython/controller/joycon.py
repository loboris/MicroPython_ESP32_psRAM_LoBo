import pyglet
from pyglet.gl import *

from espnetwork import UDP_THIS, UDP_OTHER

joysticks = pyglet.input.get_joysticks()
assert joysticks, 'No joystick device is connected'
joystick = joysticks[0]
joystick.open()
print("started joystick")

last_sent = None

def callback(dt):
    global last_sent
    left = joystick.x < -0.5
    right = joystick.x > 0.5
    up = joystick.y < -0.5
    down = joystick.y > 0.5

    boton = joystick.buttons[1]
    accel = joystick.rz > 0
    decel = joystick.z > 0

    val = (left << 0 | right << 1 | up << 2 | down << 3 | boton << 4 |
            accel << 5 | decel << 6)

    if val != last_sent:
        sock_send(bytes((val,)))
        last_sent = val

    ret = next(sock_iterator)
    if ret:
        print(str(ret, "utf-8"), end='', flush=True)

pyglet.clock.schedule(callback)
pyglet.app.run()
