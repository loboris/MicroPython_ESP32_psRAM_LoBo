import utime

try:
    import serialcomms as comms
except:
    import comms
import sprites
import gc

DEBUG = False

try:
    from remotepov import update
except:
    import povdisplay
    import imagenes
    PIXELS = 54
    povdisplay.init(PIXELS, imagenes.palette_pal)
    update = lambda: None
    if DEBUG:
        print("setting up fan debug")
        import uctypes
        debug_buffer = uctypes.bytearray_at(povdisplay.getaddress(999), 32*16)
        next_loop = 1000
        def update():
            global next_loop
            now = utime.ticks_ms()
            if utime.ticks_diff(next_loop, now) < 0:
                next_loop = utime.ticks_add(now, 1000)
                comms.send(b"debug", debug_buffer)


class Director:
    JOY_LEFT = 1
    JOY_RIGHT = 2
    JOY_UP = 4
    JOY_DOWN = 8
    BUTTON_A = 16
    BUTTON_B = 32
    BUTTON_C = 64
    BUTTON_D = 128

    def __init__(self):
        self.scene_stack = []
        self.buttons = 0
        self.last_buttons = 0
        gc.disable()
        sprites.reset_sprites()


    def push(self, scene):
        self.scene_stack.append(scene)
        sprites.reset_sprites()
        gc.collect()
        scene.on_enter()

    def pop(self):
        scene = self.scene_stack.pop()
        scene.on_exit()
        if not scene.keep_music:
            self.music_off()
        sprites.reset_sprites()
        if self.scene_stack:
            self.scene_stack[-1].on_enter()
        return scene

    def register_strip(self, sprite_index, image):
        sprites.set_imagestrip(sprite_index, image)

    def is_pressed(self, button):
        return bool(button & self.buttons)

    def was_pressed(self, button):
        return bool(button & self.buttons) and not bool(button & self.last_buttons)

    def was_released(self, button):
        return not bool(button & self.buttons) and bool(button & self.last_buttons)

    def sound_play(self, track):
        comms.send(b"sound " + track)

    def music_play(self, track):
        comms.send(b"music " + track)

    def music_off(self):
        comms.send(b"music off")

    def run(self):
        while True:
            scene = self.scene_stack[-1]
            next_loop = utime.ticks_add(utime.ticks_ms(), 30)

            val = comms.receive(1)
            if val:
                self.buttons = val[0]

            try:
                scene.scene_step()
            except StopIteration:
                pass

            self.last_buttons = self.buttons

            # TODO check this hack
            update()

            gc.collect()
            delay = utime.ticks_diff(next_loop, utime.ticks_ms())
            if delay > 0:
                utime.sleep_ms(delay)


director = Director()
