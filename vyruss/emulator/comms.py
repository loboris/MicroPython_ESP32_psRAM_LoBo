import asyncio
import os
import signal
import time

from gmqtt import Client as MQTTClient

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

STOP = None

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

def send(b):
    try:
        sock.send(b)
    except socket.error as err:
        print(err)

def on_connect(client, flags, rc, properties):
    print('Connected')
    client.subscribe('fan_debug', qos=0)
    client.subscribe('audio_play', qos=0)
    client.subscribe('audio_play', qos=0)
    client.subscribe('temperatura', qos=0)
    client.subscribe('joystick_leds', qos=0)
    client.subscribe('start_fan', qos=0)
    client.subscribe('stop_fan', qos=0)

last_time_seen = 0

def on_message(client, topic, payload, qos, properties):
    global last_time_seen

    if topic == "sprites":
        assert len(payload) == 256
        spritedata[:] = payload

    if topic == "pal":
        assert len(payload) == 1024
        palette[:] = payload

    if topic == "imagestrip":
        image_stripes[slot.decode()] = payload

    if topic == "audio_play":
        #playsound(payload)
        pass

    if topic == "fan_debug":
        print(topic, payload)
        rpm = -1
        for now, duration in struct.iter_unpack("qq", payload):
            if now > last_time_seen:
                last_time_seen = now
                rpm, fps = 1000000 / duration * 60, (1000000/duration)*2
                print(now, duration, "(%.2f rpm, %.2f fps)" % (rpm, fps))
                send_telemetry(rpm, fps)
        if rpm == -1:
            last_time_seen = 0
            rpm = 0
        print('fan_speed', "rpm=%d" % rpm)
        client.publish('fan_speed', "rpm=%d" % rpm)

def on_subscribe(client, mid, qos):
    print('SUBSCRIBED to', mid)

def on_disconnect(client, packet, exc=None):
    print('Disconnected')

async def main():
    global STOP
    STOP = asyncio.Event()

    client = MQTTClient("raspi-eventos")
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.on_subscribe = on_subscribe

    await client.connect("ventilastation.local")
    await STOP.wait()

def mqtt_loop():
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(main())

def shutdown():
    STOP.set()

receive_thread = threading.Thread(target=mqtt_loop)
receive_thread.daemon = True
receive_thread.start()
