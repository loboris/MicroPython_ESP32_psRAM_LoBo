from director import director
from scene import Scene
from sprites import Sprite, reset_sprites


class Menu(Scene):
    OPTIONS = []

    def __init__(self, selected_index=0):
        super(Menu, self).__init__()
        self.options = self.OPTIONS[:]
        self.selected_index = selected_index

    def on_enter(self):
        reset_sprites()
        self.game_over_sprite = Sprite()
        self.game_over_sprite.set_x(128)
        self.game_over_sprite.set_y(20)
        self.game_over_sprite.set_perspective(0)
        self.game_over_sprite.set_strip(2)

        self.game_over_sprite.set_frame(0)

    def step(self):
        if director.was_pressed(director.BUTTON_LEFT):
            self.selected_index = max(self.selected_index - 1, 0)
            print(self.options[self.selected_index])
        if director.was_pressed(director.BUTTON_RIGHT):
            self.selected_index = min(self.selected_index + 1, len(self.options) - 1)
            print(self.options[self.selected_index])
        if director.was_pressed(director.BUTTON_A):
            self.on_option_pressed(self.selected_index)

    def on_option_pressed(self, option_index):
        print('pressed:', option_index)
