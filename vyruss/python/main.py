from director import director
import imagenes
import menu
import vyruss

DEBUG = True
UPGRADEABLES = "director.py|scene.py|menu.py|vyruss.py".split("|")

#gameover = sprites.get_sprite(0)
#gameover.image_strip = 6
## Disable Frame
#gameover.frame = DISABLED_FRAME
#gameover.x = -32
#gameover.y = 2

def verificar_start_ota():
    if not reset_was_down and reset:
        if accel and decel:
            update_over_the_air()

def update_over_the_air():
    try:
        import network
        import requests
        import machine
        import os
        sta_if = network.WLAN(network.STA_IF)
        sta_if.active(True)
        sta_if.connect("alecu-casa", "plagazombie2")
        print("connecting to wifi", end="")
        while not sta_if.isconnected():
            print(".", end="")
            utime.sleep_ms(333)
        print()
        mdns = network.mDNS()
        print("Iniciando mdns")
        mdns.start("esptilador", "deadbeef")
        print("Buscando mdns")
        base_url = "http://" + mdns.queryHost("bollo.local") + ":8000/"
        for fn in UPGRADEABLES:
            print("updating " + fn)
            tmpfn = "TMP_" + fn
            result = requests.get(base_url + fn, file=tmpfn)
            if result[0] == 200:
                os.rename(tmpfn, fn)

        print("rebooting")
        machine.reset()
    except Exception as e:
        print(e)


class GamesMenu(menu.Menu):
    OPTIONS = [
        ('vyruss', 7, 3, 64),
        ('credits', 7, 0, 64),
        ('vong', 7, 1, 64),
        ('ventilagon', 7, 2, 64),
    ]

    def on_option_pressed(self, option_index):
        option_pressed = self.options[option_index]
        print(option_pressed)
        if option_pressed[0] == 'vyruss':
            director.push(vyruss.VyrusGame())
            raise StopIteration()


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
