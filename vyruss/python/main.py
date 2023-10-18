from director import director
import imagenes
import menu

def update_over_the_air():
    import ota_update
    director.push(ota_update.Update())

class GamesMenu(menu.Menu):
    OPTIONS = [
        ('vyruss', 7, 0, 64),
        ('vladfarty', 7, 2, 64),
        ('credits', 7, 3, 64),
        ('ventilagon', 7, 1, 64),
    ]

    def on_option_pressed(self, option_index):
        option_pressed = self.options[option_index]
        print(option_pressed)
        if option_pressed[0] == 'vyruss':
            import vyruss
            director.push(vyruss.VyrusGame())
            raise StopIteration()
        if option_pressed[0] == 'credits':
            import credits
            director.push(credits.Credits())
            raise StopIteration()
        if option_pressed[0] == 'vladfarty':
            import vladfarty
            director.push(vladfarty.VladFarty())
            raise StopIteration()
        if option_pressed[0] == 'ventilagon':
            import ventilagon_game
            director.push(ventilagon_game.VentilagonGame())
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
    director.register_strip(8, imagenes.credits_png)
    director.register_strip(10, imagenes.tierra_flat_png)
    director.register_strip(11, imagenes.marte_flat_png)
    director.register_strip(12, imagenes.jupiter_flat_png)
    director.register_strip(13, imagenes.saturno_flat_png)
    director.register_strip(14, imagenes.sves_flat_png)
    director.register_strip(15, imagenes.ventilastation_flat_png)
    director.register_strip(16, imagenes.tecno_estructuras_flat_png)
    director.register_strip(17, imagenes.menatwork_flat_png)
    director.register_strip(18, imagenes.yourgame_flat_png)
    director.register_strip(19, imagenes.vga_pc734_png)
    director.register_strip(20, imagenes.vga_cp437_png)
    director.register_strip(21, imagenes.vlad_farting_flat_png)
    director.register_strip(22, imagenes.farty_lion_flat_png)
    director.register_strip(23, imagenes.ready_png)
    director.register_strip(24, imagenes.bg64_flat_png)
    director.register_strip(25, imagenes.copyright_png)
    director.register_strip(26, imagenes.bgspeccy_flat_png)
    director.register_strip(27, imagenes.reset_png)
    director.register_strip(28, imagenes.farty_lionhead_flat_png)
    #director.register_strip(19, imagenes.doom_flat_png)

    director.push(GamesMenu())
    director.run()

if __name__ == '__main__':
    main()
