import config
import http.client
import urllib.parse
import struct
import socket
import threading
from pygletengine import image_stripes, palette, spritedata, playsound

sock = None
sockfile = None
#sock.setblocking(False)

looping = True

influx_host = "localhost:8086"
influx_url = "/write?" + urllib.parse.urlencode({'db': "ventilastation", 'precision': 'ns'})

def send_telemetry(rpm, fps):
    conn = http.client.HTTPConnection(influx_host)
    body = "velocidad rpm=%f,fps=%f\n" % (rpm, fps)
    conn.request("POST", influx_url, body=body, headers={})
    response = conn.getresponse()
    if int(response.status) / 100 != 2:
        print("Sending failed: %s, %s: %s" % ( response.status,
        response.reason, body))



def waitconnect():
    while looping:
        try:
            global sock, sockfile
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((config.SERVER_IP, config.SERVER_PORT))
            sockfile = sock.makefile(mode="rb")
            return
        except socket.error as err:
            print(err)

def send(b):
    try:
        sock.send(b)
    except socket.error as err:
        print(err)

def receive_loop():
    last_time_seen = 0

    waitconnect()
    while looping:
        try:
            l = sockfile.readline()
            command, *args = l.split()

            if command == b"sprites":
                spritedata[:] = sockfile.read(256)

            if command == b"pal":
                palette[:] = sockfile.read(1024)

            if command == b"play":
                #playsound(args[0])
                pass

            if command == b"imagestrip":
                length, slot = args
                image_stripes[slot.decode()] = sockfile.read(int(length))

            if command == b"debug":
                length = 32 * 16
                data = sockfile.read(length)
                for now, duration in struct.iter_unpack("qq", data):
                    if now > last_time_seen:
                        last_time_seen = now
                        rpm, fps = 1000000 / duration * 60, (1000000/duration)*2
                        print(now, duration, "(%.2f rpm, %.2f fps)" % (rpm, fps))
                        send_telemetry(rpm, fps)
                #print(struct.unpack("q"*32*2, data))


        except socket.error as err:
            print(err)
            waitconnect()

        except Exception as err:
            sock.close()
            waitconnect()


def shutdown():
    global looping
    looping = False
    sock.close()

receive_thread = threading.Thread(target=receive_loop)
receive_thread.daemon = True
receive_thread.start()
