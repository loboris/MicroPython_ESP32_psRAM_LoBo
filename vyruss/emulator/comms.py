import config
import socket
import threading
from pygletengine import image_stripes, palette, spritedata, playsound

sock = None
sockfile = None
#sock.setblocking(False)

looping = True

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
                playsound(args[0])

            if command == b"imagestrip":
                length, slot = args
                image_stripes[slot.decode()] = sockfile.read(int(length))

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
