import uselect
import usocket

try:
    import network

    ap_if = network.WLAN(network.AP_IF)
    print('starting access point...')
    ap_if.active(True)
    ap_if.config(essid="ventilastation", authmode=3, password="plagazombie2")
    print('network config:', ap_if.ifconfig())
except:
    print("no need to set up wifi")
