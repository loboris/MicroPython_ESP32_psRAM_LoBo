import utime

import comms
import spritelib
import model

import imagenes

DEBUG = True

try:
    from remotepov import update
except:
    update = lambda: None
    print("setting up fan debug")
    if DEBUG:
        import povdisplay
        import uctypes
        debug_buffer = uctypes.bytearray_at(povdisplay.getaddress(999), 32*16)
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


gameover = spritelib.get_sprite(0)
gameover.image_strip = 6
# Disable Frame
gameover.frame = DISABLED_FRAME
gameover.x = -32
gameover.y = 2

def reset_game():
    global scene
    scene = model.Fleet()

button_was_down = False
reset_was_down = False

def process_input(b):
    left =  bool(b & 1)
    right = bool(b & 2)
    up =    bool(b & 4)
    down =  bool(b & 8)
    boton = bool(b & 16)
    accel = bool(b & 32)
    decel = bool(b & 64)
    reset = bool(b & 128)

    global reset_was_down
    if not reset_was_down and reset:
        reset_game()
    reset_was_down = reset

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

    scene.heading(up, down, left, right)

    global button_was_down
    if not button_was_down and boton:
        scene.fire()
    button_was_down = boton
            
    if (accel and decel and gameover.frame == 0):
        reset_game()
         
    scene.accel(accel, decel)

    #text = "\r{0} {2} {1} {3} {4}   ".format(direction, boton, int(nave.x), decel, accel)
    #sock_send(bytes(text, "utf-8"))
    #print(text, end="")


reset_game()

def game_loop():
    last_val = None
    counter = 0    
 
    while True:
        next_loop = utime.ticks_add(utime.ticks_ms(), 30)

        val = comms.receive(1)
        if val is not None:
            process_input(val[0])
            last_val = val[0]
        elif last_val is not None:
            process_input(last_val)

        scene.step()

        update()
        delay = utime.ticks_diff(next_loop, utime.ticks_ms())
        if delay > 0:
            utime.sleep_ms(delay)
        else:
            print("odelay:", delay)

game_loop()
