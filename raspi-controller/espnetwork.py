UDP_THIS = "0.0.0.0", 5225
UDP_OTHER = "127.0.0.1", 5005
#UDP_OTHER = "192.168.4.1", 5005

import socket

sock = socket.socket(socket.AF_INET,
                     socket.SOCK_DGRAM)
sock.setblocking(False)
sock.bind(UDP_THIS)

def sock_send(what):
    try:
        sock.sendto(what, UDP_OTHER)
        print('{:08b}'.format(what[0]))
    except BlockingIOError:
        print("error trying to send", what)

def sock_generator():
    while True:
        try:
            data, _ = sock.recvfrom(1024)
            yield data
        except BlockingIOError:
            yield None

sock_iterator = sock_generator()
