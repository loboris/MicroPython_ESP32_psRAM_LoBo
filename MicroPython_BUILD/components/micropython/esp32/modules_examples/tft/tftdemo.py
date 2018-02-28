"""
Demo program demonstrating the capabities of the MicroPython display module
Author:	LoBo (https://github/loboris)
Date:	08/10/2017

"""

import machine, display, time, _thread, math

tft = display.TFT()

# --- Select correct configuration ---

# ESP32-WROVER-KIT v3:
#tft.init(tft.ST7789, rst_pin=18, backl_pin=5, miso=25, mosi=23, clk=19, cs=22, dc=21)

# Adafruit TFT FeatherWing:
#tft.init(tft.ILI9341, width=240, height=320, miso=19, mosi=18, clk=5, cs=15, dc=33, bgr=True, hastouch=tft.TOUCH_STMPE, tcs=32)

# M5Stack:
tft.init(tft.M5STACK, width=240, height=320, rst_pin=33, backl_pin=32, miso=19, mosi=23, clk=18, cs=14, dc=27, bgr=True, backl_on=1)

# Some others...
#tft.init(tft.ILI9341, width=240, height=320, miso=19,mosi=23,clk=18,cs=5,dc=26,tcs=27,hastouch=True, bgr=True)
#tft.init(tft.ST7735R, speed=10000000, spihost=tft.HSPI, mosi=13, miso=12, clk=14, cs=15, dc=27, rst_pin=26, hastouch=False, bgr=False, width=128, height=160)


def testt():
    while True:
        lastx = 0
        lasty = 0
        t,x,y = tft.gettouch()
        if t:
            dx = abs(x-lastx)
            dy = abs(y-lasty)
            if (dx > 2) and (dy > 2):
                tft.circle(x,y,4,tft.RED)
        time.sleep_ms(50)


maxx = 320
maxy = 240
miny = 12
touch = False

# fonts used in this demo
fontnames = (
    tft.FONT_Default,
    tft.FONT_7seg,
    tft.FONT_Ubuntu,
    tft.FONT_Comic,
    tft.FONT_Tooney,
    tft.FONT_Minya
)


# Check if the display is touched
#-------------
def touched():
    if not touch:
        return False
    else:
        tch,_,_ = tft.gettouch()
        if tch <= 0:
            return False
        else:
            return True

# print display header
#----------------------
def header(tx, setclip):
    # adjust screen dimensions (depends on used display and orientation)
    global maxx, maxy, miny

    maxx, maxy = tft.screensize()
    tft.clear()
    if maxx < 240:
        tft.font(tft.FONT_Small, rotate=0)
    else:
        tft.font(tft.FONT_Default, rotate=0)
    _,miny = tft.fontSize()
    miny += 5
    tft.rect(0, 0, maxx-1, miny-1, tft.OLIVE, tft.DARKGREY)
    tft.text(tft.CENTER, 2, tx, tft.CYAN, transparent=True)

    if setclip:
        tft.setwin(0, miny, maxx, maxy)

# Display some fonts
#-------------------
def dispFont(sec=5):
    header("DISPLAY FONTS", False)

    if maxx < 240:
        tx = "MicroPython"
    else:
        tx = "Hi from MicroPython"
    starty = miny + 4

    n = time.time() + sec
    while time.time() < n:
        y = starty
        x = 0
        i = 0
        while y < maxy:
            if i == 0: 
                x = 0
            elif i == 1:
                x = tft.CENTER
            elif i == 2:
                x = tft.RIGHT
            i = i + 1
            if i > 2:
                i = 0
            
            for font in fontnames:
                if font == tft.FONT_7seg:
                    tft.font(font)
                    tft.text(x,y,"-12.45/",machine.random(0xFFFFFF))
                else:
                    tft.font(font)
                    tft.text(x,y,tx, machine.random(0xFFFFFF))
                _,fsz = tft.fontSize()
                y = y + 2 + fsz
                if y > (maxy-fsz):
                    y = maxy
        if touched():
            break

# Display random fonts
#------------------------------
def fontDemo(sec=5, rot=False):
    tx = "FONTS"
    if rot:
        tx = "ROTATED " + tx
    header(tx, True)

    tx = "ESP32-MicrpPython"
    n = time.time() + sec
    while time.time() < n:
        frot = 0
        if rot:
            frot = math.floor(machine.random(359)/5)*5
        for font in fontnames:
            if (not rot) or (font != tft.FONT_7seg):
                x = machine.random(maxx-8)
                if font != tft.FONT_7seg:
                    tft.font(font, rotate=frot)
                    _,fsz = tft.fontSize()
                    y = machine.random(miny, maxy-fsz)
                    tft.text(x,y,tx, machine.random(0xFFFFFF))
                else:
                    l = machine.random(6,12)
                    w = machine.random(1,l // 3)
                    tft.font(font, rotate=frot, dist=l, width=w)
                    _,fsz = tft.fontSize()
                    y = machine.random(miny, maxy-fsz)
                    tft.text(x,y,"-12.45/", machine.random(0xFFFFFF))
        if touched():
            break
    tft.resetwin()

# Display random lines
#-------------------
def lineDemo(sec=5):
    header("LINE DEMO", True)

    n = time.time() + sec
    while time.time() < n:
        x1 = machine.random(maxx-4)
        y1 = machine.random(miny, maxy-4)
        x2 = machine.random(maxx-1)
        y2 = machine.random(miny, maxy-1)
        color = machine.random(0xFFFFFF)
        tft.line(x1,y1,x2,y2,color)
        if touched():
            break
    tft.resetwin()

# Display random circles
#----------------------------------
def circleDemo(sec=5,dofill=False):
    tx = "CIRCLE"
    if dofill:
        tx = "FILLED " + tx
    header(tx, True)

    n = time.time() + sec
    while time.time() < n:
        color = machine.random(0xFFFFFF)
        fill = machine.random(0xFFFFFF)
        x = machine.random(4, maxx-2)
        y = machine.random(miny+2, maxy-2)
        if x < y:
            r = machine.random(2, x)
        else:
            r = machine.random(2, y)
        if dofill:
            tft.circle(x,y,r,color,fill)
        else:
            tft.circle(x,y,r,color)
        if touched():
            break
    tft.resetwin()

#------------------
def circleSimple():
    tx = "CIRCLE"
    header(tx, True)

    x = maxx // 2
    y = (maxy-miny) // 2 + (miny // 2)
    if x > y:
        r = y - miny
    else:
        r = x - miny
    while r > 0:
        color = machine.random(0xFFFFFF)
        fill = machine.random(0xFFFFFF)
        tft.circle(x,y,r,color,fill)
        r -= 10
        x += 10

# Display random ellipses
#-----------------------------------
def ellipseDemo(sec=5,dofill=False):
    tx = "ELLIPSE"
    if dofill:
        tx = "FILLED " + tx
    header(tx, True)

    n = time.time() + sec
    while time.time() < n:
        x = machine.random(4, maxx-2)
        y = machine.random(miny+2, maxy-2)
        if x < y:
            rx = machine.random(2, x)
        else:
            rx = machine.random(2, y)
        if x < y:
            ry = machine.random(2, x)
        else:
            ry = machine.random(2, y)
        color = machine.random(0xFFFFFF)
        if dofill:
            fill = machine.random(0xFFFFFF)
            tft.ellipse(x,y,rx,ry,15, color,fill)
        else:
            tft.ellipse(x,y,rx,ry,15,color)
        if touched():
            break
    tft.resetwin()

# Display random rectangles
#---------------------------------
def rectDemo(sec=5, dofill=False):
    tx = "RECTANGLE"
    if dofill:
        tx = "FILLED " + tx
    header(tx, True)

    n = time.time() + sec
    while time.time() < n:
        x = machine.random(4, maxx-2)
        y = machine.random(miny, maxy-2)
        w = machine.random(2, maxx-x)
        h = machine.random(2, maxy-y)
        color = machine.random(0xFFFFFF)
        if dofill:
            fill = machine.random(0xFFFFFF)
            tft.rect(x,y,w,h,color,fill)
        else:
            tft.rect(x,y,w,h,color)
        if touched():
            break
    tft.resetwin()

# Display random rounded rectangles
#--------------------------------------
def roundrectDemo(sec=5, dofill=False):
    tx = "ROUND RECT"
    if dofill:
        tx = "FILLED " + tx
    header(tx, True)

    n = time.time() + sec
    while time.time() < n:
        x = machine.random(2, maxx-18)
        y = machine.random(miny, maxy-18)
        w = machine.random(12, maxx-x)
        h = machine.random(12, maxy-y)
        if w > h:
            r = machine.random(2, h // 2)
        else:
            r = machine.random(2, w // 2)
        color = machine.random(0xFFFFFF)
        if dofill:
            fill = machine.random(0xFFFFFF)
            tft.roundrect(x,y,w,h,r,color,fill)
        else:
            tft.roundrect(x,y,w,h,r,color)
        if touched():
            break
    tft.resetwin()

# Fisplay all demos
#--------------------------------------
def fullDemo(sec=5, rot=tft.LANDSCAPE):
    tft.orient(rot)
    dispFont(sec)
    time.sleep(0.1)
    fontDemo(sec, rot=False)
    time.sleep(0.1)
    fontDemo(sec, rot=True)
    time.sleep(0.1)
    lineDemo(sec)
    time.sleep(0.1)
    circleDemo(sec, dofill=False)
    time.sleep(0.1)
    circleDemo(sec, dofill=True)
    time.sleep(0.1)
    circleSimple()
    time.sleep(0.1)
    time.sleep(sec)
    time.sleep(0.1)
    ellipseDemo(sec, dofill=False)
    time.sleep(0.1)
    ellipseDemo(sec, dofill=True)
    time.sleep(0.1)
    rectDemo(sec, dofill=False)
    time.sleep(0.1)
    rectDemo(sec, dofill=True)
    time.sleep(0.1)
    roundrectDemo(sec, dofill=False)
    time.sleep(0.1)
    roundrectDemo(sec, dofill=True)
    time.sleep(0.1)

# Run demo in thread
def dispDemo_th():
    while True:
        fullDemo(rot=tft.LANDSCAPE)
        fullDemo(rot=tft.PORTRAIT)
        fullDemo(rot=tft.LANDSCAPE_FLIP)
        fullDemo(rot=tft.PORTRAIT_FLIP)

# dispth=_thread.start_new_thread("TFTDemo", dispDemo_th, ())

