import machine, _thread, time
import micropython, gc
import bme280

# Setup the LED pins
bled    = machine.Pin(4, mode=machine.Pin.OUT)
#rled    = machine.Pin(0, mode=machine.Pin.OUT)
#gled    = machine.Pin(2, mode=machine.Pin.OUT)

bled.value(0)
#gled.value(0)
#rled.value(0)

# Setup I2C to be used with BME280 sensor
i2c=machine.I2C(scl=machine.Pin(26),sda=machine.Pin(25),speed=400000)
# Initialize BME280
bme=bme280.BME280(i2c=i2c)

# Define LED thread function
#---------------------------
def rgbled(n=200, led=bled):
    notif_exit = 4718
    notif_replay = 2
    notif_count = 3
    x = 0
    _thread.allowsuspend(True)
    while True:
        led.value(1)
        time.sleep_ms(n)
        led.value(0)
        x = x + 1

        t = 10
        while t > 0:
            notif = _thread.getnotification()
            if notif == notif_exit:
                _thread.sendmsg(_thread.getReplID(), "[%s] Exiting" % (_thread.getSelfName()))
                return
            elif notif == notif_replay:
                _thread.sendmsg(_thread.getReplID(), "[%s] I've been notified" % (_thread.getSelfName()))
            elif notif == notif_count:
                _thread.sendmsg(_thread.getReplID(), "[%s] Run counter = %u" % (_thread.getSelfName(), x))
            elif notif == 777:
                _thread.sendmsg(_thread.getReplID(), "[%s] Forced EXCEPTION" % (_thread.getSelfName()))
                time.sleep_ms(1000)
                zz = 234 / 0
            elif notif != 0:
                _thread.sendmsg(_thread.getReplID(), "[%s] Got unknown notification: %u" % (_thread.getSelfName(), notif))

            typ, sender, msg = _thread.getmsg()
            if msg:
                _thread.sendmsg(_thread.getReplID(), "[%s] Message from '%s'\n'%s'" % (_thread.getSelfName(), _thread.getThreadName(sender), msg))
            time.sleep_ms(100)
            t = t - 1
        gc.collect()

# For LED thread we don't need more than 3K stack
_ = _thread.stack_size(3*1024)
# Start LED thread
#rth=_thread.start_new_thread("R_Led", rgbled, (100, rled))

time.sleep_ms(500)
#gth=_thread.start_new_thread("G_Led", rgbled, (250, gled))
bth=_thread.start_new_thread("B_Led", rgbled, (100, bled))

# Function to generate BME280 values string
#---------------
def bmevalues():
    t, p, h = bme.read_compensated_data()

    p = p // 256
    pi = p // 100
    pd = p - pi * 100

    hi = h // 1024
    hd = h * 100 // 1024 - hi * 100
    #return "[{}] T={0:1g}C  ".format(time.strftime("%H:%M:%S",time.localtime()), round(t / 100,1)) + "P={}.{:02d}hPa  ".format(pi, pd) + "H={}.{:01d}%".format(hi, hd)
    return "[{}] T={}C  ".format(time.strftime("%H:%M:%S",time.localtime()), t / 100) + "P={}.{:02d}hPa  ".format(pi, pd) + "H={}.{:02d}%".format(hi, hd)

# Define BME280 thread function
#-----------------------
def bmerun(interval=60):
    _thread.allowsuspend(True)
    sendmsg = True
    send_time = time.time() + interval
    while True:
        while time.time() < send_time:
            notif = _thread.getnotification()
            if notif == 10002:
                _thread.sendmsg(_thread.getReplID(), bmevalues())
            elif notif == 10004:
                sendmsg = False
            elif notif == 10006:
                sendmsg = True
            elif (notif <= 3600) and (notif >= 10):
                interval = notif
                send_time = time.time() + interval
                _thread.sendmsg(_thread.getReplID(), "Interval set to {} seconds".format(interval))
                
            time.sleep_ms(100)
        send_time = send_time + interval
        if sendmsg:
            _thread.sendmsg(_thread.getReplID(), bmevalues())

# 3K is enough for BME280 thread
_ = _thread.stack_size(3*1024)
# start the BME280 thread
bmeth=_thread.start_new_thread("BME280", bmerun, (60,))

# === In the 3rd thread we will run Neopixels rainbow demo ===

num_np = 144
np=machine.Neopixel(machine.Pin(21), num_np)

def rainbow(pos=1, bri=0.02):
    dHue = 360*3/num_np
    for i in range(1, num_np):
        hue = (dHue * (pos+i)) % 360;
        np.setHSB(i, hue, 1.0, bri, 1, False)
    np.show()

# DEfine Neopixels thread function
#-----------------
def thrainbow_py():
    pos = 0
    bri = 0.02
    while True:
        for i in range(0, num_np):
            dHue = 360.0/num_np * (pos+i);
            hue = dHue % 360;
            np.setHSB(i, hue, 1.0, bri, 1, False)
        np.show()
        notif = _thread.getnotification()
        if (notif > 0) and (notif <= 100):
            bri =  notif / 100.0
        elif notif == 1000:
            _thread.sendmsg(_thread.getReplID(), "[%s] Run counter = %u" % (_thread.getSelfName(), pos))
        pos = pos + 1

import math
#---------------
def thrainbow():
    pos = 0
    np.brightness(25)
    while True:
        np.rainbow(pos, 3)
        notif = _thread.getnotification()
        if (notif > 0) and (notif <= 100):
            np.brightness(math.trunc(notif * 2.55))
            
        elif notif == 1000:
            _thread.sendmsg(_thread.getReplID(), "[%s] Run counter = %u" % (_thread.getSelfName(), pos))
        pos = pos + 1

# Start the Neopixels thread
npth=_thread.start_new_thread("Neopixel", thrainbow, ())


utime.sleep(1)

machine.heap_info()
_thread.list()

# Set neopixel brightnes (%)
#_thread.notify(npth, 20)
# Get counter value from Neopixel thread
#_thread.notify(npth, 1000)

