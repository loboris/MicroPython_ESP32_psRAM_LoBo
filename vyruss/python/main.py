from director import director
import imagenes
import menu
import vyruss

UPGRADEABLES = "director.py|scene.py|menu.py|vyruss.py".split("|")

def update_over_the_air():
    try:
        import network
        import requests
        import machine
        import os
        sta_if = network.WLAN(network.STA_IF)
        sta_if.active(True)
        sta_if.connect("ventilastation", "plagazombie2")
        print("connecting to wifi", end="")
        while not sta_if.isconnected():
            print(".", end="")
            utime.sleep_ms(333)
        print()
        mdns = network.mDNS()
        print("Iniciando mdns")
        mdns.start("ventilador", "deadbeef")
        print("Buscando mdns")
        host = mdns.queryHost("ventilastation.local")
        print("Encontrado host: ", host)
        base_url = "http://" + host + ":8000/"
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
        if director.is_pressed(director.BUTTON_D) \
            and director.is_pressed(director.BUTTON_B)\
            and director.is_pressed(director.BUTTON_C):
            update_over_the_air()
        super(Menu, self).step()

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
