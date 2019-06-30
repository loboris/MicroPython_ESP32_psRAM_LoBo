import comms
import imagenes
import spritelib
import utime

DEBUG = True
try:
    from remotepov import update
except:
    update = lambda: None

    if DEBUG:
        import povsprites
        import uctypes
        debug_buffer = uctypes.bytearray_at(povsprites.getaddress(999), 32*16)
        next_loop = 1000
        def update():
            global next_loop
            now = utime.ticks_ms()
            if utime.ticks_diff(next_loop, now) < 0:
                next_loop = utime.ticks_add(now, 1000)
                comms.send("debug", debug_buffer)


DISABLED_FRAME = -1

# init images
spritelib.set_imagestrip(0, imagenes.galaga_png)
spritelib.set_imagestrip(1, imagenes.galaga_alt8_png)
spritelib.set_imagestrip(2, imagenes.galaga_alt10_png)
spritelib.set_imagestrip(3, imagenes.disparo_png)
spritelib.set_imagestrip(4, imagenes.ll9_png)
#spritelib.set_imagestrip(4, imagenes.crawling_png)
spritelib.set_imagestrip(5, imagenes.explosion_png)
spritelib.set_imagestrip(6, imagenes.gameover_png)

# init nave
nave = spritelib.create_sprite(1)
nave.image_strip = 4
nave.frame = 0
nave.x = 256 - 8
nave.y = 0

# init disparo
disparo = spritelib.create_sprite(2)
disparo.image_strip = 3
disparo.x = 48
disparo.y = 12

gameover = spritelib.create_sprite(0)
gameover.image_strip = 6
# Disable Frame
gameover.frame = DISABLED_FRAME
gameover.x = -32
gameover.y = 2

spritelib.debug(nave)

explosiones = []
malos = []

def generar_malos():
    malos_aux = []
    for n in range(5):
        malo = spritelib.create_sprite(n + 10)
        malo.image_strip = 0
        malo.frame = (n + 1) * 2
        malo.y = n * 3 + 128
        malo.x = (n + 1) * 17
        malos_aux.append(malo)
    return malos_aux

def reset_game():
    gameover.frame = DISABLED_FRAME
    malos.clear()
    generar_malos()
    nave.frame = 0

def sonido(nombre):
    comms.send("play " + nombre)

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

    #Not Shot if it in gameover
    if gameover.frame != 0:
        if (boton and disparo.frame == DISABLED_FRAME):
            disparo.y = nave.y + 11
            disparo.x = nave.x + 6
            disparo.frame = 0
            sonido("shoot1")

    if (accel and decel and gameover.frame == 0):
        reset_game()
         
    if accel:
        nave.y -=1
    if decel:
        nave.y +=1
    if not accel and not decel:
        nave.y = 0


    #text = "\r{0} {2} {1} {3} {4}   ".format(direction, boton, int(nave.x), decel, accel)
    #sock_send(bytes(text, "utf-8"))
    #print(text, end="")


def collision(missile, targets):
    #return
    for target in targets:
        if (missile.x < target.x + spritelib.sprite_width(target) and
                missile.x + spritelib.sprite_width(missile) > target.x and
                missile.y < target.y + spritelib.sprite_height(target) and
                missile.y + spritelib.sprite_height(missile) > target.y):
            return target


def loop():
    last_val = None
    step = 0
    counter = 0    
    loop_start = utime.ticks_ms()
 
    while True:
        next_loop = utime.ticks_add(loop_start, 20)

        val = comms.receive(1)
        if val is not None:
            process(val[0])
            last_val = val[0]
        elif last_val is not None:
            process(last_val)

        #List empty in the begining and when you finish one level
        global malos
        if not malos:
            malos = generar_malos()
            
        # Move malos
        for n in range(len(malos)):
            m = malos[n]
            if m.y > 18 or True:
                m.y = m.y - 1
            
            if m.y < -16:
                m.y = 127
            #lateral velocity 
            if not counter % 4:
                m.x = (m.x + n - 3) % 256
            counter+=1

        for n in range(len(malos)):
            malos[n].frame = (n + 1) * 2 + step
        
        # explotion animation           
        for e in explosiones:
            e.frame += 1
            if e.frame == 9:
                e.frame = DISABLED_FRAME
                explosiones.remove(e)

        step = (step + 1) % 2
        
        # detect shot colitions
        if disparo.frame != DISABLED_FRAME:
            disparo.y += 3
            if disparo.y < 0:
                disparo.frame = DISABLED_FRAME
            malo = collision(disparo, malos)
            if malo:
                disparo.frame = DISABLED_FRAME
                malo.frame = 0
                malo.image_strip = 5
                malos.remove(malo)
                explosiones.append(malo)
                sonido("explosion2")


        # detect spaceship colitions
        global nave 
        if nave.frame != DISABLED_FRAME:
            malo = collision(nave, malos)
            if malo:
                nave.frame = DISABLED_FRAME
                malo.frame = 0
                malo.image_strip = 5
                explosiones.append(malo)
                malos.remove(malo)
                gameover.frame = 0
                sonido("explosion3")

        update()
        delay = utime.ticks_diff(next_loop, utime.ticks_ms())
        if delay > 0:
            utime.sleep_ms(delay)
        loop_start = next_loop

loop()
