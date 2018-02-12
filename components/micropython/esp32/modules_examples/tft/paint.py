'''
Simple paint program to demonstrate display & touch on ILI9341 based displays
Author: LoBo, loboris@gmail.com (https://github.com/loboris)

Usage:

import paint
p = paint.Paint(tft)

To run the program:
  execute: 'p.start()' to start the program
  or
  execute: 'p.start(tft.LANDSCAPE)' to set the orientation andstart the program

Program can also be run in the thread:
  'paint_th = _thread.start("Paint", p.start, ())'
  or
  'paint_th = _thread.start("Paint", p.start, (tft.PORTRAIT,))'

'''

import display, time, math


class Paint:
    def __init__(self, tft):
        self.tft = tft
        self._started = False
        self.orient = display.TFT.PORTRAIT

    def start(self, orient=display.TFT.PORTRAIT, threaded=False):
        if not self._started :
            self.orient = orient
            if threaded:
                _ = _thread.stack_size(4*1024)
                _thread.start_new_thread("TFT_Paint", self.run, ())
            else:
                self.run()
        else:
            print("Already running.")

    #Wait for touch event
    #----------------------
    def wait(self, tpwait):
        while True:
            touch, _, _ = self.tft.gettouch()
            if (touch and tpwait) or ((not touch) and (not tpwait)):
                break
            time.sleep_ms(10)

    #Display selection bars and some info
    #--------------------
    def paint_info(self):
        self.tft.orient(self.orient)
        self.tft.font(self.tft.FONT_Default, rotate=0)
        self.tft.clear(self.tft.BLACK)
        dispx, dispy = self.tft.screensize()
        dx = dispx // 8
        dw,dh = self.tft.fontSize()
        dy = dispy - dh - 5
        fp = math.ceil((dx // 2) - (dw // 2))

        # draw color bar, orange selected
        self.tft.rect(dx*0,0,dx-2,18,self.tft.BLACK,self.tft.BLACK)
        self.tft.rect(dx*1,0,dx-2,18,self.tft.WHITE,self.tft.WHITE)
        self.tft.rect(dx*2,0,dx-2,18,self.tft.RED,self.tft.RED)
        self.tft.rect(dx*3,0,dx-2,18,self.tft.GREEN,self.tft.GREEN)
        self.tft.rect(dx*4,0,dx-2,18,self.tft.BLUE,self.tft.BLUE)
        self.tft.rect(dx*5,0,dx-2,18,self.tft.YELLOW,self.tft.YELLOW)
        self.tft.rect(dx*6,0,dx-2,18,self.tft.CYAN,self.tft.CYAN)
        self.tft.rect(dx*7,0,dx-2,18,self.tft.ORANGE,self.tft.ORANGE)

        self.tft.rect(dx*7+2,2,dx-6,14,self.tft.WHITE)
        self.tft.rect(dx*7+3,3,dx-8,12,self.tft.BLACK)

        for i in range(1, 8):
            self.tft.line(dx*i-1,0,dx*i-1,18,self.tft.LIGHTGREY)
            self.tft.line(dx*i-2,0,dx*i-2,18,self.tft.LIGHTGREY)
        self.tft.line(0,18,dispx,18,self.tft.LIGHTGREY)

        # draw functions bar, size 2 selected
        self.tft.rect(dx*0,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*1,dy,dx-2,dispy-dy,self.tft.YELLOW)
        self.tft.rect(dx*2,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*3,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*4,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*5,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*6,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
        self.tft.rect(dx*7,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)

        # write info
        self.tft.text(dx*0+fp,dy+3,"2", self.tft.CYAN)
        self.tft.text(dx*1+fp,dy+3,"4", self.tft.CYAN)
        self.tft.text(dx*2+fp,dy+3,"6", self.tft.CYAN)
        self.tft.text(dx*3+fp,dy+3,"8", self.tft.CYAN)
        self.tft.text(dx*4+fp-3,dy+3,"10", self.tft.CYAN)
        #self.tft.text(dx*5+fp,dy+3,"S", self.tft.CYAN)
        self.tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, self.tft.LIGHTGREY, self.tft.LIGHTGREY)
        self.tft.text(dx*6+fp,dy+3,"C")
        self.tft.text(dx*7+fp,dy+3,"R")

        self.tft.text(40,40,"C ", self.tft.YELLOW)
        self.tft.text(self.tft.LASTX,self.tft.LASTY," Change shape/outline", self.tft.CYAN)
        self.tft.circle(40+(dw // 2), 40+(dh // 2), (dh // 2)+1, self.tft.YELLOW, self.tft.LIGHTGREY)

        self.tft.text(40,dh*2+40,"C ", self.tft.YELLOW)
        self.tft.text(self.tft.LASTX,self.tft.LASTY," Clear screen", self.tft.CYAN)

        self.tft.text(40,dh*4+40,"R ", self.tft.YELLOW)
        self.tft.text(self.tft.LASTX,self.tft.LASTY," Return, exit program", self.tft.CYAN)

        self.tft.text(40,dh*6+40,"2,4,6,8,10", self.tft.YELLOW)
        self.tft.text(self.tft.LASTX,self.tft.LASTY," draw size", self.tft.CYAN)

        self.tft.font(self.tft.FONT_Ubuntu)
        fw,fh = self.tft.fontSize()
        tw = self.tft.textWidth("Touch screen to start");
        self.tft.text(self.tft.CENTER,dh*10+40,"Touch screen to start", self.tft.ORANGE)
        self.tft.rect(((dispx-tw)//2)-4, dh*10+36, tw+8, fw+8, self.tft.BLUE)
        self.tft.font(self.tft.FONT_Default)

        self.wait(0)
        self.wait(1)
        self.wait(0)
    
        self.tft.rect(0,20,dispx,dy-20,self.tft.BLACK,self.tft.BLACK)
    
        return dx, dy


    # Paint main loop
    # --_---------
    def run(self):
        self._started = True
        dx, dy = self.paint_info()
        dispx, dispy = self.tft.screensize()

        first = True
        drw = 1
        color = self.tft.ORANGE
        lastc = dx*7
        r = 4
        lastr = dx

        lx = 0
        ly = 0
        while True:
            time.sleep_ms(10)
            # get touch status and coordinates
            touch, x, y = self.tft.gettouch()
            if touch:
                if (y < 20) or (y > dy):
                    # color or functions bar touched
                    # check if after 100 ms we are on the same position
                    lx = x
                    ly = y
                    time.sleep_ms(100)
                    self.wait(1)
                    touch, x, y = self.tft.gettouch()
                    if touch and (abs(x-lx) < 4) and (abs(y-ly) < 4):
                        if (y < 20):
                            # === first touch, upper bar touched: color select ===
                            self.tft.rect(lastc+2,2,dx-6,14,color)
                            self.tft.rect(lastc+3,3,dx-8,12,color)
                            self.tft.rect(lastc+2,2,dx-6,14,color)
                            self.tft.rect(lastc+3,3,dx-8,12,color)
                            if x > (dx*7):
                                color = self.tft.ORANGE
                                lastc = dx*7
                            elif x > (dx*6):
                                color = self.tft.CYAN
                                lastc = dx*6
                            elif x > (dx*5):
                                color = self.tft.YELLOW
                                lastc = dx*5
                            elif x > (dx*4):
                                color = self.tft.BLUE
                                lastc = dx*4
                            elif x > (dx*3):
                                color = self.tft.GREEN
                                lastc = dx*3
                            elif x > (dx*2):
                                color = self.tft.RED
                                lastc = dx*2
                            elif x > dx:
                                color = self.tft.WHITE
                                lastc = dx
                            elif x > 1:
                                color = self.tft.BLACK
                                lastc = 0
                            self.tft.rect(lastc+2,2,dx-6,14,self.tft.WHITE)
                            self.tft.rect(lastc+3,3,dx-8,12,self.tft.BLACK)
    
                            # wait for touch release
                            self.wait(0)
                            first = True

                        elif (y > dy):
                            # === first touch, lower bar touched: change size, r, erase, shape select, return ===
                            if x < (dx*5):
                                self.tft.rect(lastr,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
                                self.tft.rect(lastr,dy,dx-2,dispy-dy,self.tft.LIGHTGREY)
                            if x > (dx*7):
                                break
                            elif x > (dx*6):
                                # clear drawing area
                                self.tft.rect(0,20,dispx,dy-20,self.tft.BLACK,self.tft.BLACK)
                            elif x > (dx*5):
                                # change drawing shape
                                drw = drw + 1
                                if drw > 4:
                                    drw = 1
                                self.tft.rect(dx*5,dy,dx-2,dispy-dy,self.tft.LIGHTGREY, self.tft.BLACK)
                                if drw == 1:
                                    self.tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, self.tft.LIGHTGREY, self.tft.LIGHTGREY)
                                elif drw == 3:
                                    self.tft.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, self.tft.LIGHTGREY, self.tft.LIGHTGREY)
                                elif drw == 2:
                                    self.tft.circle(dx*5+((dx-2) // 2), dy+((dispy-dy) // 2), (dispy-dy) // 2 - 2, self.tft.YELLOW, self.tft.DARKGREY)
                                elif drw == 4:
                                    self.tft.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, self.tft.YELLOW, self.tft.DARKGREY)
                            # drawing size
                            elif x > (dx*4):
                                r = 10
                                lastr = dx*4
                            elif x > (dx*3):
                                r = 8
                                lastr = dx*3
                            elif x > (dx*2):
                                r = 6
                                lastr = dx*2
                            elif x > dx:
                                r = 4
                                lastr = dx
                            elif x > 0:
                                r = 2
                                lastr = 0
                            if x < (dx*5):
                                self.tft.rect(lastr,dy,dx-2,dispy-dy,self.tft.YELLOW)
                            # wait for touch release
                            self.wait(0)
                            first = True
                elif (x > r) and (y > (r+20)) and (y < (dy-r)):
                    # === touch on drawing area, draw shape ===
                    dodrw = True
                    if not first:
                        # it is NOT first touch to drawing area, draw only if coordinates changed, but no more than 5 pixels
                        if ((x == lx) and (y == ly)) or (abs(x-lx) > 5) or (abs(y-ly) > 5):
                            dodrw = False

                    if dodrw:
                        # draw with active shape
                        if drw == 1:
                            self.tft.circle(x, y, r, color, color)
                        elif drw == 3:
                            self.tft.rect(x-r, y-r, r*2, r*2, color, color)
                        elif drw == 2:
                            self.tft.circle(x, y, r, color, self.tft.DARKGREY)
                        elif drw == 4:
                            self.tft.rect(x-r, y-r, r*2, r*2, color, self.tft.DARKGREY)
                    # save touched coordinates
                    lx = x
                    ly = y
                    first = False
            else:
                # not touched
                first = True
                self.wait(1)

        self.tft.rect(0,dy,dispx,dispy-dy,self.tft.YELLOW,self.tft.BLACK)
        self.tft.text(self.tft.CENTER, dy+3, "FINISHED", self.tft.CYAN)
        self._started = False

