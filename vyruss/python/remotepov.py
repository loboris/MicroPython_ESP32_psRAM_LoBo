import uctypes
import usocket

from config import OTHER_IP

SEND_SPRITES = 5225
SEND_IMAGESTRIPES = 6226

sprite_data = bytearray(b"\0\0\0\xff" * 64)

sprites_addr = usocket.getaddrinfo(OTHER_IP, SEND_SPRITES)[0][-1]
#udp_sock = usocket.socket(usocket.AF_INET, usocket.SOCK_DGRAM)

imagestripes_addr = usocket.getaddrinfo(OTHER_IP, SEND_IMAGESTRIPES)[0][-1]

stripes = {}

def send_tcp(identifier, data):
    print("sending %r - %d bytes" % (identifier, len(data)))
    imagestripes_sock = usocket.socket(usocket.AF_INET, usocket.SOCK_STREAM)
    imagestripes_sock.connect(imagestripes_addr)
    imagestripes_sock.write(identifier + "\n")
    imagestripes_sock.write(data)
    imagestripes_sock.close()
    print("sent")

def init(num_pixels, palette):
    send_tcp("pal", palette)

def getaddress(sprite_num):
    return uctypes.addressof(sprite_data) + sprite_num * 4

def set_imagestrip(n, stripmap):
    stripes[n] = stripmap
    send_tcp(str(n), stripmap)

count = 0
def update():
    global count
    try:
        count += 1
        sprites_sock = usocket.socket(usocket.AF_INET, usocket.SOCK_STREAM)
        sprites_sock.connect(sprites_addr)
        sprites_sock.write(sprite_data)
        sprites_sock.close()
    except OSError:
        print(count)
