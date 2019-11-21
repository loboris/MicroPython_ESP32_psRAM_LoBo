from director import director
from scene import Scene
from sprites import Sprite

UPGRADEABLES = "ota_update.py|main.py|director.py|scene.py|menu.py|vyruss.py|credits.py|ventilagon.py|vong.py".split("|")

class Update(Scene):
    def on_enter(self):
        logo = self.logo = Sprite()
        logo.set_strip(14)
        logo.set_perspective(0)
        logo.set_x(0)
        y = 54 - logo.height()
        logo.set_y(y)
        logo.set_frame(0)
    
    def step(self):
        try:
            import network
            import utime
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
            base_url = "http://192.168.4.1:8000/"
            for fn in UPGRADEABLES:
                print("updating " + fn, end="")
                tmpfn = "TMP_" + fn
                result = requests.get(base_url + fn, file=tmpfn)
                if result[0] == 200:
                    try:
                        os.remove(fn)
                    except:
                        pass
                    os.rename(tmpfn, fn)
                    print(" sucess")
                else:
                    print(" ", result[0])

            print("rebooting")
            machine.reset()
        except Exception as e:
            print(e)
            
        finally:
            director.pop()
            raise StopIteration()