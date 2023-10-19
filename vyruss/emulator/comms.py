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
from pygletengine import image_stripes, palette, spritedata, playsound, playmusic

sock = None
sockfile = None
#sock.setblocking(False)

STOP = None
looping = True
mqtt_client = None

influx_host = "ventilastation.local:8086"
influx_url = "/write?" + urllib.parse.urlencode({'db': "ventilastation", 'precision': 'ns'})

def send_telemetry(body):
    conn = http.client.HTTPConnection(influx_host)
    conn.request("POST", influx_url, body=body, headers={})
    response = conn.getresponse()
    if int(response.status) // 100 != 2:
        print("Sending failed: %s, %s: %s" % ( response.status,
        response.reason, body))

def send_velocidad(rpm, fps):
    body = "velocidad rpm=%f,fps=%f\n" % (rpm, fps)
    send_telemetry(body)
    if mqtt_client:
        try:
            mqtt_client.publish('fan_speed', "rpm=%d" % rpm)
        except Exception as err:
            print("cannot publish in mqtt:", err)

def waitconnect():
    print("conectando...")
    while looping:
        try:
            global sock, sockfile
            if config.USE_IP:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect((config.SERVER_IP, config.SERVER_PORT))
                sockfile = sock.makefile(mode="rb")
            else:
                import serial
                device = [f for f in os.listdir("/dev/") if f.startswith(config.SERIAL_DEVICE)][0]
                sock = sockfile = serial.Serial("/dev/" + device, 115200)
            return
        except socket.error as err:
            print(err)
            time.sleep(.5)
            print("retry...")


def receive_loop():
    last_time_seen = 0

    waitconnect()
    while looping:
        try:
            l = sockfile.readline()

            l = l.strip()
            if not l:
                continue

            command, *args = l.split()

            if command == b"sprites":
                spritedata[:] = sockfile.read(5*100)

            if command == b"pal":
                palette[:] = sockfile.read(1024)

            if command == b"sound":
                playsound(b" ".join(args))

            if command == b"arduino":
                arduino_send(b" ".join(args))

            if command == b"music":
                playmusic(b" ".join(args))

            if command == b"musicstop":
                playmusic("off")

            if command == b"imagestrip":
                length, slot = args
                image_stripes[slot.decode()] = sockfile.read(int(length))

            if command == b"debug":
                length = 32 * 16
                data = sockfile.read(length)

                readings = []
                for now, duration in struct.iter_unpack("qq", data):
                    if now < 10000:
                        last_time_seen = 0

                    if now > last_time_seen:
                        last_time_seen = now
                        rpm, fps = 1000000 / duration * 60, (1000000/duration)*2
                        print(now, duration, "(%.2f rpm, %.2f fps)" % (rpm, fps))
                        readings.append(rpm)

                if len(readings):
                    avg_rpm = sum(readings) / len(readings)
                    avg_fps = avg_rpm / 30
                    print("average %.2f rpm %.2f fps" % (avg_rpm, avg_fps))
                    send_velocidad(avg_rpm, avg_fps)
                #print(struct.unpack("q"*32*2, data))


        except socket.error as err:
            print(err)
            waitconnect()

        except Exception as err:
            print(err)
            if sock:
                sock.close()
            waitconnect()


def shutdown():
    global looping
    looping = False
    if sock:
        sock.close()

receive_thread = threading.Thread(target=receive_loop)
receive_thread.daemon = True
receive_thread.start()

def send(b):
    try:
        if config.USE_IP:
            if sock:
                sock.send(b)
        else:
            if sockfile:
                sockfile.write(b)
    except socket.error as err:
        print(err)

def on_connect(client, flags, rc, properties):
    global mqtt_client
    mqtt_client = client
    print('Connected')
    client.subscribe('fan_debug', qos=0)
    client.subscribe('sound_play', qos=0)
    client.subscribe('music_play', qos=0)
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
                send_velocidad(rpm, fps)
        if rpm == -1:
            last_time_seen = 0
            rpm = 0
        print('fan_speed', "rpm=%d" % rpm)
        client.publish('fan_speed', "rpm=%d" % rpm)

    if topic == "temperatura":
        send_telemetry("%s %s" % (topic, payload.decode().strip()))
        print(payload)

def on_subscribe(client, mid, qos):
    print('SUBSCRIBED to', mid)

def on_disconnect(client, packet, exc=None):
    global mqtt_client
    mqtt_client = None
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
    #STOP.set()
    global looping
    looping = False
    if sock:
        sock.close()

#mqtt_thread = threading.Thread(target=mqtt_loop)
#mqtt_thread.daemon = True
#mqtt_thread.start()

try:
    import serial
    ARDUINO_DEVICE = "/dev/ttyAMA0"
    arduino = serial.Serial(ARDUINO_DEVICE, 57600)

    arduino_commands = {
        b"start": b"S",
        b"stop": b"r",
        b"reset": b"R",
        b"attract": b"s"
    }

    def arduino_send(command):
        print("arduino, sending", command)
        arduino.write(arduino_commands.get(command, b" "))

except Exception as e:
    print(e)

    def arduino_send(_):
        pass

