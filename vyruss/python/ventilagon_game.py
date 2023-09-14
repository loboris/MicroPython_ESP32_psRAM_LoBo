import ventilagon
from director import director
from scene import Scene
try:
    import serialcomms as comms
except:
    import comms


class VentilagonGame(Scene):
    def on_enter(self):
        ventilagon.enter()
        self.last_buttons = None

    def sending_loop(self):
        sending = ventilagon.sending()
        while sending:
            comms.send(sending)
            sending = ventilagon.sending()

    def on_exit(self):
        ventilagon.exit()
	self.sending_loop()

    def step(self):
        buttons = director.buttons
        if buttons != self.last_buttons:
            self.last_buttons = buttons
            ventilagon.received(buttons)

        if director.was_pressed(director.BUTTON_D):
            director.pop()
            raise StopIteration()

	self.sending_loop()
