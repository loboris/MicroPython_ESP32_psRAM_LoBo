import uselect
import usocket

try:
    import network
    import utime

    sta_if = network.WLAN(network.STA_IF)
    if not sta_if.isconnected():
        print('connecting to network', end="")
        sta_if.active(True)
        sta_if.connect('ventilastation', 'plagazombie2')
        while not sta_if.isconnected():
            print(".", end="")
            utime.sleep_ms(333)
        print()
    print('network config:', sta_if.ifconfig())
    network.telnet.start()
except:
    print("no need to set up wifi")

UDP_THIS = "0.0.0.0", 5005
this_addr = usocket.getaddrinfo(*UDP_THIS)[0][-1]

sock = usocket.socket(usocket.AF_INET, usocket.SOCK_STREAM)
sock.setsockopt(usocket.SOL_SOCKET, usocket.SO_REUSEADDR, 1)
sock.bind(this_addr)
sock.listen(10)
sock.setblocking(0)
print("listening on 5005")

poller = uselect.poll()
poller.register(sock, uselect.POLLIN)
conn = None

def receive(bufsize):
    global conn
    retval = None
    for obj, event, *more in poller.ipoll(0, 0):
        if obj is sock:
            if conn:
                conn.close()
                poller.unregister(conn)
            conn, _ = sock.accept()
            conn.setblocking(0)
            poller.register(conn, uselect.POLLIN)
        else:
            try:
                b = obj.read(1)
            except OSError:
                b = None
            if b:
                retval = b
            else:
                obj.close()
                poller.unregister(obj)
                conn = None
    return retval

def send(line, data=b""):
    #print("sending:", line, end="")
    global conn
    if conn:
        try:
            conn.write(line + b"\n" + data)
        except OSError:
            conn.close()
            poller.unregister(conn)
            conn = None
            # conn, _ = sock.accept()
            # conn.setblocking(0)
            # poller.register(conn, uselect.POLLIN)
