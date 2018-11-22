import socket
import pyglet
from pyglet.window import key

from espnetwork import sock_send, sock_iterator

window = pyglet.window.Window()
keys = key.KeyStateHandler()
window.push_handlers(keys)

last_sent = None

def callback(dt):
    global last_sent

    left = keys[key.LEFT]
    right = keys[key.RIGHT]
    up = keys[key.UP]
    down = keys[key.DOWN]

    boton = keys[key.SPACE]
    accel = keys[key.A]
    decel = keys[key.D]

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
