from director import director
import imagenes
import menu
import vyruss

def update_over_the_air():
    import ota_update
    director.push(ota_update.Update())

class GamesMenu(menu.Menu):
    OPTIONS = [
        ('vyruss', 7, 3, 64),
        ('vong', 7, 1, 64),
        ('credits', 7, 0, 64),
        ('ventilagon', 7, 2, 64),
    ]

    def on_option_pressed(self, option_index):
        option_pressed = self.options[option_index]
        print(option_pressed)
        if option_pressed[0] == 'vyruss':
            director.push(vyruss.VyrusGame())
            raise StopIteration()

    def step(self):
        super(GamesMenu, self).step()
        if director.is_pressed(director.BUTTON_D) \
            and director.is_pressed(director.BUTTON_B)\
            and director.is_pressed(director.BUTTON_C):
            update_over_the_air()

def main():
    # init images
    director.register_strip(0, imagenes.galaga_png)
    director.register_strip(1, imagenes.numerals_png)
    director.register_strip(2, imagenes.gameover_png)
    director.register_strip(3, imagenes.disparo_png)
    director.register_strip(4, imagenes.ll9_png)
    director.register_strip(5, imagenes.explosion_png)
    director.register_strip(6, imagenes.explosion_nave_png)
    director.register_strip(7, imagenes.menu_png)
    director.register_strip(10, imagenes.tierra_flat_png)
    director.register_strip(11, imagenes.marte_flat_png)
    director.register_strip(12, imagenes.jupiter_flat_png)
    director.register_strip(13, imagenes.saturno_flat_png)
    director.register_strip(14, imagenes.sves_flat_png)
    director.register_strip(15, imagenes.ventilastation_flat_png)
    director.register_strip(16, imagenes.tecno_estructuras_flat_png)

    director.push(GamesMenu())
    director.run()

if __name__ == '__main__':
    main()
