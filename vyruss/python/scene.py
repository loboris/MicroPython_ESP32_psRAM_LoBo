import utime

from sprites import reset_sprites


class Scene:
    def __init__(self):
        reset_sprites()
        self.pending_calls = []

    def on_enter(self):
        pass

    def on_exit(self):
        pass

    def call_later(self, delay, callable):
        when = utime.ticks_add(utime.ticks_ms(), delay)
        self.pending_calls.append((when, callable))
        self.pending_calls.sort()

    def scene_step(self):
        self.step()
        now = utime.ticks_ms()
        while self.pending_calls:
            when, callable = self.pending_calls[0]
            if utime.ticks_diff(when, now) <= 0:
                self.pending_calls.pop(0)
                callable()
            else:
                break

    def step(self):
        pass
