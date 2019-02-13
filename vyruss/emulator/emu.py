import socket
import socketserver
import sys
import threading
from itertools import cycle
from pygletengine import PygletEngine, image_stripes, palette, spritedata


LED_COUNT = 52
LISTEN_IP = "0.0.0.0"
LISTEN_UDP = 5225
LISTEN_TCP = 6226
#UDP_IP_COMMANDS = "192.168.1.183"
UDP_IP_COMMANDS = "127.0.0.1"
UDP_PORT_COMMANDS = 5005

def file_iterator(f):
    while True:
        data = f.read(4*LED_COUNT)
        while data != b"":
            yield data
            data = f.read(4*LED_COUNT)
        f.seek(0)

sprites_sock = socket.socket(socket.AF_INET,
                     socket.SOCK_DGRAM)
sprites_sock.setblocking(False)
sprites_sock.bind((LISTEN_IP, LISTEN_UDP))

def sock_iterator():
    while True:
        try:
            data, _ = sprites_sock.recvfrom(1024)
            yield data
        except BlockingIOError:
            yield None

def sock_send(what):
    sprites_sock.sendto(what, (UDP_IP_COMMANDS, UDP_PORT_COMMANDS))

def sock_vsync():
    #sock_send(b"V")
    pass

class ThreadedTCPRequestHandler(socketserver.StreamRequestHandler):
    def handle(self):
        slot = self.rfile.readline().strip()
        data = bytes(self.rfile.read())
        if slot == "pal":
            palette = data
        else:
            image_stripes[slot] = data
        self.wfile.write(b"OK")
        self.finish()
        print(image_stripes)

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    pass

filename = '-'
if len(sys.argv) >= 2:
    filename = sys.argv[1]

if filename == '-':
    iterator = sock_iterator()
    vsync = sock_vsync
else:
    f = open(filename, 'rb')
    iterator = file_iterator(f)
    vsync = lambda: f.seek(0)

led_count = 50
if len(sys.argv) >= 3:
    led_count = int(sys.argv[2])

revs_per_second = 5
if len(sys.argv) >= 4:
    revs_per_second = float(sys.argv[3])

server = ThreadedTCPServer((LISTEN_IP, LISTEN_TCP), ThreadedTCPRequestHandler)
with server:
    ip, port = server.server_address
    server_thread = threading.Thread(target=server.serve_forever)
    server_thread.daemon = True
    server_thread.start()
    try:
        PygletEngine(led_count, iterator, vsync, sock_send, revs_per_second)
    finally:
        server.shutdown()
