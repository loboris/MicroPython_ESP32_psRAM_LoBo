import uctypes
import usocket

SEND_UDP = 5225
SEND_TCP = 6226
SEND_IP = "127.0.0.1"
#SEND_IP = "192.168.4.2"

sprite_data = bytearray(b"\0\0\0\xff" * 64)

udp_addr = usocket.getaddrinfo(SEND_IP, SEND_UDP)[0][-1]
udp_sock = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)

tcp_addr = usocket.getaddrinfo(SEND_IP, SEND_TCP)[0][-1]

stripes = {}

def send_tcp(identifier, data):
    tcp_sock = usocket.socket(usocket.AF_INET, usocket.SOCK_STREAM)
    tcp_sock.connect(tcp_addr)
    tcp_sock.write(identifier + "\n")
    tcp_sock.write(data)
    tcp_sock.close()

def init(num_pixels, palette):
    send_tcp("pal", palette)

def getaddress(sprite_num):
    return uctypes.addressof(sprite_data) + sprite_num * 4

def set_imagestrip(n, stripmap):
    stripes[n] = stripmap
    send_tcp(str(n), stripmap)

def update():
    udp_sock.sendto(sprite_data, udp_addr)
