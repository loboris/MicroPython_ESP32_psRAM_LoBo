import imagenes
import spritelib
import usocket
import utime

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
#UDP_OTHER = "127.0.0.1", 5225
UDP_OTHER = "192.168.4.2", 5225

print ("connecting....")
this_addr = usocket.getaddrinfo(*UDP_THIS)[0][-1]
other_addr = usocket.getaddrinfo(*UDP_OTHER)[0][-1]

sock = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)
sock.setblocking(False)
sock.bind(this_addr)

print ("connected!!!")

nave = spritelib.get_sprite(0, imagenes.galaga_png, width=16, height=16, frames=2)
disparo = spritelib.get_sprite(1, imagenes.disparo_png, width=3, height=5, frames=1)
spritelib.debug(nave)

imagen_base = nave.image
print("nave!", hex(nave.image))
imagen_nave = 0

malos = []

for n in range(11, 10+6):
    otra = spritelib.get_sprite(n, None, 16, 16, 2)
    otra.enabled = True
    otra.y = n * 3
    malos.append(otra)
    #otra.x = (n - 10) * 16
    #otra.y = 30 + (n-10) * 4


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

    if (boton and not disparo.enabled):
        global imagen_nave
        imagen_nave = (imagen_nave + 1) % 6
        nave.image = imagen_base + imagen_nave * (16*16*2)

    disparo.enabled = boton

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

        for otra in malos:
            otra.y = otra.y - 1
            if otra.y < 0:
                otra.y = 127

        utime.sleep_ms(15)

loop()