import socket
import socketserver
import sys
import threading
from itertools import cycle
from pygletengine import PygletEngine, image_stripes, palette, spritedata


LED_COUNT = 52
LISTEN_IP = "0.0.0.0"
LISTEN_SPRITES = 5225
LISTEN_IMAGES = 6226
#UDP_IP_COMMANDS = "192.168.4.1"
UDP_IP_COMMANDS = "127.0.0.1"
UDP_PORT_COMMANDS = 5005

commands_sock = socket.socket(socket.AF_INET,
                     socket.SOCK_DGRAM)
commands_sock.setblocking(False)
#commands_sock.bind((LISTEN_IP, UDP_PORT_COMMANDS))

def sock_send(what):
    commands_sock.sendto(what, (UDP_IP_COMMANDS, UDP_PORT_COMMANDS))

class ImagesRequestHandler(socketserver.StreamRequestHandler):
    def handle(self):
        slot = self.rfile.readline().strip()
        data = bytes(self.rfile.read())
        if slot == b"pal":
            palette = data
        else:
            image_stripes[slot.decode()] = data
        self.wfile.write(b"OK")
        self.finish()

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    pass


class SpritesRequestHandler(socketserver.StreamRequestHandler):
    def handle(self):
        global spritedata
        spritedata[:] = self.rfile.read(256)

class ThreadedUDPServer(socketserver.ThreadingMixIn, socketserver.UDPServer):
    pass


led_count = 52
if len(sys.argv) >= 2:
    led_count = int(sys.argv[1])

images_server = ThreadedTCPServer((LISTEN_IP, LISTEN_IMAGES), ImagesRequestHandler)
sprites_server = ThreadedTCPServer((LISTEN_IP, LISTEN_SPRITES), SpritesRequestHandler)
with images_server:
    ip, port = images_server.server_address
    images_server_thread = threading.Thread(target=images_server.serve_forever)
    images_server_thread.daemon = True
    images_server_thread.start()
    sprites_server_thread = threading.Thread(target=sprites_server.serve_forever)
    sprites_server_thread.daemon = True
    sprites_server_thread.start()
    try:
        PygletEngine(led_count, sock_send)
    finally:
        images_server.shutdown()
        sprites_server.shutdown()
        images_server.server_close()
        sprites_server.server_close()
