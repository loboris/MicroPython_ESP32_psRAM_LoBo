import imagenes
import spritelib
import usocket
import utime
from povsprites import update

try:
    import network

    ap_if = network.WLAN(network.AP_IF)
    print('starting access point...')
    ap_if.active(True)
    ap_if.config(essid="ventilastation", authmode=3, password="plagazombie2")
    print('network config:', ap_if.ifconfig())
except:
    print("no need to set up wifi")

UDP_THIS = "0.0.0.0", 5005
UDP_OTHER = "127.0.0.1", 5225
#UDP_OTHER = "192.168.4.2", 5225
DISABLED_FRAME = -1

print ("connecting....")
this_addr = usocket.getaddrinfo(*UDP_THIS)[0][-1]
other_addr = usocket.getaddrinfo(*UDP_OTHER)[0][-1]

sock = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)
sock.setblocking(False)
sock.bind(this_addr)

print ("connected!!!")

# init images
spritelib.set_imagestrip(0, imagenes.galaga_png)
spritelib.set_imagestrip(1, imagenes.galaga_alt8_png)
spritelib.set_imagestrip(2, imagenes.galaga_alt10_png)
spritelib.set_imagestrip(3, imagenes.disparo_png)
spritelib.set_imagestrip(4, imagenes.ll9_png)

# init nave
nave = spritelib.create_sprite(0)
nave.image_strip = 4
nave.frame = 0
nave.x = 256 - 8
nave.y = 0

# init disparo
disparo = spritelib.create_sprite(1)
disparo.image_strip = 3
disparo.x = 48
disparo.y = 12

spritelib.debug(nave)

malos = []

for n in range(5):
    malo = spritelib.create_sprite(n + 10)
    malo.image_strip = 0
    malo.frame = (n + 1) * 2
    malo.y = 127
    malo.x = (n + 1) * 17
    malos.append(malo)


def sock_send(what):
    sock.sendto(what, other_addr)

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

def step(where):
    nave.x = (nave.x + rotar(nave.x, where)) % 256

def process(b):
    left =  bool(b & 1)
    right = bool(b & 2)
    up =    bool(b & 4)
    down =  bool(b & 8)
    boton = bool(b & 16)
    accel = bool(b & 32)
    decel = bool(b & 64)
    
    if up and left:
        direction = "↖"
    elif up and right:
        direction = "↗"
    elif down and left:
        direction = "↙"
    elif down and right:
        direction = "↘"
    elif up:
        direction = "↑"
    elif down:
        direction = "↓"
    elif left:
        direction = "←"
    elif right:
        direction = "→"
    else:
        direction = " "

    where = new_heading(up, down, left, right)
    if where is not None:
        step(where)

    if (boton and disparo.frame == DISABLED_FRAME):
        disparo.y = 0
        disparo.x = nave.x + 6
        disparo.frame = 0

    if accel:
        nave.y -=1
    if decel:
        nave.y +=1
    if not accel and not decel:
        nave.y = 0


    #text = "\r{0} {2} {1} {3} {4}   ".format(direction, boton, int(nave.x), decel, accel)
    #sock_send(bytes(text, "utf-8"))
    #print(text, end="")

def loop():
    last_b = None
    step = 0
    while True:
        try:
            val = sock.recv(1)
            if val:
                for b in val:
                    process(b)
                    last_b = b
        except OSError:
            if last_b:
                process(last_b)

        for n in range(len(malos)):
            m = malos[n]
            if m.y > 18:
                m.y = m.y - 1

            #if m.y < -32:
            #    m.y = 127
            m.x = (m.x + n - 3) % 256

        for n in range(len(malos)):
            malos[n].frame = (n + 1) * 2 + step

        step = (step + 1) % 2

        if disparo.frame != DISABLED_FRAME:
            disparo.y += 3
            if disparo.y < 0:
                disparo.frame = DISABLED_FRAME

        utime.sleep_ms(20)
        update()

loop()
