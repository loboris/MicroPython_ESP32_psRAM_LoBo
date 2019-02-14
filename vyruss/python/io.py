import usocket
from utime import ticks_us, sleep

PIXELS = const(52)
buffer = bytearray(PIXELS * 4)

vsync_handler = None

socksend_addr = usocket.getaddrinfo('127.0.0.1', 5225)[0][-1]
sockrecv_addr = usocket.getaddrinfo('0.0.0.0', 5005)[0][-1]

socksend = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)
socksend.setblocking(False)

sockrecv = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)
sockrecv.setblocking(False)
sockrecv.bind(sockrecv_addr)

left_pressed = False
right_pressed = False
up_pressed = False
down_pressed = False
button_pressed = False

def send(data):
    socksend.sendto(data, socksend_addr)

def loop():
    global left_pressed, right_pressed, up_pressed, down_pressed, button_pressed
    try:
        buf = sockrecv.recv(1)
        if buf:
            for b in buf:
                b = chr(b)
                if b == 'V':
                    if vsync_handler:
                        vsync_handler(ticks_us())
                elif b == 'l':
                    left_pressed = False
                elif b == 'L':
                    left_pressed = True
                elif b == 'r':
                    right_pressed = False
                elif b == 'R':
                    right_pressed = True
                elif b == 'u':
                    up_pressed = False
                elif b == 'U':
                    up_pressed = True
                elif b == 'd':
                    down_pressed = False
                elif b == 'D':
                    down_pressed = True
                elif b == 'b':
                    button_pressed = False
                elif b == 'B':
                    button_pressed = True
                else:
                    pass

    except OSError:
        pass
