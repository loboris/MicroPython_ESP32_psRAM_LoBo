import sys
import os
import time


def mac2eui(mac):
    mac = mac[0:6] + 'fffe' + mac[6:]
    return hex(int(mac[0:2], 16) ^ 2)[2:] + mac[2:]


# Node Name
from machine import unique_id
import ubinascii
unique_id = ubinascii.hexlify(unique_id()).decode()

NODE_NAME = 'ESP32_'

NODE_EUI = mac2eui(unique_id)
NODE_NAME = NODE_NAME + unique_id
# NODE_NAME = NODE_NAME + NODE_EUI

# millisecond
millisecond = time.ticks_ms
# microsecond = time.ticks_us


# Controller
from controller_esp import Controller


