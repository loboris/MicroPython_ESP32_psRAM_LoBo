from director import director
from scene import Scene
from sprites import Sprite, reset_sprites


class Menu(Scene):
    OPTIONS = []  # option id, strip id, frame, width

    def __init__(self, selected_index=0):
        super(Menu, self).__init__()
        self.options = self.OPTIONS[:]
        self.selected_index = selected_index

    def on_enter(self):
        reset_sprites()
        self.sprites = []
        for option_id, strip_id, frame, width in self.options:
            sprite = Sprite()
            sprite.set_x(256 - 32)
            sprite.set_y(0)
            sprite.set_perspective(0)
            sprite.set_strip(strip_id)
            sprite.set_frame(frame)

            self.sprites.append(sprite)

    def step(self):
        if director.was_pressed(director.BUTTON_RIGHT):
            director.audio_play(b'shoot3')
            self.selected_index -= 1
            if self.selected_index == -1:
                self.selected_index = len(self.options) - 1
        if director.was_pressed(director.BUTTON_LEFT):
            director.audio_play(b'shoot3')
            self.selected_index += 1
            if self.selected_index > len(self.options) - 1:
                self.selected_index = 0
        if director.was_pressed(director.BUTTON_A):
            director.audio_play(b'shoot1')
            self.on_option_pressed(self.selected_index)

        # option[3] has the width
        offset = sum([option[3]
                      for option_index, option in enumerate(self.options)
                      if option_index < self.selected_index])
        offset += int(self.options[self.selected_index][3] / 2)

        start_x = 0
        accumulated_width = 0
        for option_index, sprite in enumerate(self.sprites):
            sprite.set_x(start_x + accumulated_width - offset)
            sprite.set_y(0)
            accumulated_width += self.options[option_index][3]  # option width

    def on_option_pressed(self, option_index):
        print('pressed:', option_index)
