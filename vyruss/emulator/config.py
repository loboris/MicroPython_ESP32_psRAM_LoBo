import sys
FULLSCREEN=False
if len(sys.argv) > 1:
    SERVER_IP = sys.argv[1]
else:
    SERVER_IP = "192.168.4.1" # esp32

#SERVER_IP = "127.0.0.1" # localhost
#SERVER_IP = "192.168.1.159"
SERVER_PORT = 5005

