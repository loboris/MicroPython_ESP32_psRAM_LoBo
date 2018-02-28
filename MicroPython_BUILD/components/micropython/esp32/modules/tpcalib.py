# Touch pannel calibration for ILI9341 based displays


import display, math, machine, utime

class Calibrate:
    def __init__(self, tft):
        self.tft = tft
        self.rx = [0,0,0,0,0,0,0,0]
        self.ry = [0,0,0,0,0,0,0,0]

    #----------------------------------
    def drawCrossHair(self, x, y, clr):
        self.tft.rect(x-10, y-10, 20, 20, clr)
        self.tft.line(x-5, y, x+5, y, clr)
        self.tft.line(x, y-5, x, y+5, clr)

    #-------------------------
    def readCoordinates(self):
        x = 0
        y = 0
        n = 0
        xlist = [0,0,0,0,0,0,0,0,0,0]
        ylist = [0,0,0,0,0,0,0,0,0,0]

        # wait for touch
        while n < 10:
            n = 0
            self.tft.text(self.tft.CENTER, 110, " PRESS ", self.tft.CYAN)
            self.tft.text(self.tft.CENTER, 110, " PRESS ", self.tft.CYAN)
            touch, x, y = self.tft.gettouch(True, wait=30000)
            if not touch:
                return (False, False)

            while touch:
                if n == 10:
                    self.tft.text(self.tft.CENTER, 110, "RELEASE", self.tft.ORANGE)
                    self.tft.text(self.tft.CENTER, 110, "RELEASE", self.tft.ORANGE)
                touch, x, y = self.tft.gettouch(True)
                if touch and (n < 10):
                    xlist[n] = x
                    ylist[n] = y
                n += 1
                if n > 3000:
                    return (False, False)
                utime.sleep_ms(10)

        xlist.remove(max(xlist))
        xlist.remove(min(xlist))
        ylist.remove(max(ylist))
        ylist.remove(min(ylist))
        return (sum(xlist) / len(xlist), sum(ylist) / len(ylist))

    #----------------------------
    def calibrate(self, x, y, i):
        self.drawCrossHair(x,y, self.tft.YELLOW)
        rx, ry = self.readCoordinates()
        if rx == False:
            return False
        self.rx[i] = rx
        self.ry[i] = ry
        self.drawCrossHair(x,y,self.tft.GREEN)
        return True

    # -------------------
    def calibError(self):
        self.tft.clear()
        self.tft.font(self.tft.FONT_Default, rotate=0, fixedwidth=False)
        self.tft.text(self.tft.CENTER, self.tft.CENTER, "Calibration ERROR", self.tft.ORANGE)
        print("Calibration ERROR.")
    
    #-----------------------------
    def tpcalib(self, save=False):
        if self.tft.getTouchType() == self.tft.TOUCH_XPT:
            self.tft.orient(display.TFT.LANDSCAPE)
        elif self.tft.getTouchType() == self.tft.TOUCH_STMPE:
            self.tft.orient(display.TFT.PORTRAIT)
        else:
            print("Touch not configured")
            return

        dispx, dispy = self.tft.screensize()
        
        self.tft.font(self.tft.FONT_Default, rotate=0, fixedwidth=False)
        self.tft.text(self.tft.CENTER,40,"Touch yellow point and release", self.tft.GREEN)
        self.tft.text(self.tft.CENTER,60,"Repeat for all calibration points", self.tft.GREEN)
        
        self.tft.font(self.tft.FONT_Default, rotate=0, fixedwidth=True)
        self.drawCrossHair(dispx-11, 10, self.tft.WHITE)
        self.drawCrossHair(dispx//2, 10, self.tft.WHITE)
        self.drawCrossHair(10, 10, self.tft.WHITE)
        self.drawCrossHair(dispx-11, dispy//2, self.tft.WHITE)
        self.drawCrossHair(10, dispy//2, self.tft.WHITE)
        self.drawCrossHair(dispx-11, dispy-11, self.tft.WHITE)
        self.drawCrossHair(dispx//2, dispy-11, self.tft.WHITE)
        self.drawCrossHair(10, dispy-11, self.tft.WHITE)

        if not self.calibrate(10, 10, 0):
            self.calibError()
            return False
        if not self.calibrate(10, dispy//2, 1):
            self.calibError()
            return False
        if not self.calibrate(10, dispy-11, 2):
            self.calibError()
            return False
        if not self.calibrate(dispx//2, 10, 3):
            self.calibError()
            return False
        if not self.calibrate(dispx//2, dispy-11, 4):
            self.calibError()
            return False
        if not self.calibrate(dispx-11, 10, 5):
            self.calibError()
            return False
        if not self.calibrate(dispx-11, dispy//2, 6):
            self.calibError()
            return False
        if not self.calibrate(dispx-11, dispy-11, 7):
            self.calibError()
            return False

        px = abs((((self.rx[3]+self.rx[4]+self.rx[7]) / 3) - ((self.rx[0]+self.rx[0]+self.rx[2]) / 3)) / (dispy-20))
        clx = (((self.rx[0]+self.rx[1]+self.rx[2])/3))
        crx = (((self.rx[5]+self.rx[6]+self.rx[7])/3))

        if (clx < crx):
            clx = clx - (px*10)
            crx = crx + (px*10)
        else:
            clx = clx + (px*10)
            crx = crx - (px*10)

        py = abs((((self.ry[0]+self.ry[3]+self.ry[5])/3) - ((self.ry[2]+self.ry[4]+self.ry[7])/3))/(dispx-20))
        cty = (((self.ry[0]+self.ry[3]+self.ry[5])/3))
        cby = (((self.ry[2]+self.ry[4]+self.ry[7])/3))

        if (cty < cby):
            cty = cty - (py*10)
            cby = cby + (py*10)
        else:
            cty = cty + (py*10)
            cby = cby - (py*10)

        calx = (math.ceil(clx) << 16) + math.ceil(crx)
        caly = (math.ceil(cty) << 16) + math.ceil(cby)

        self.tft.clear()
        self.tft.font(self.tft.FONT_Default, rotate=0, fixedwidth=False)
        self.tft.text(self.tft.CENTER, self.tft.CENTER, "Calibration completed\n")
        if save:
            self.tft.setCalib(calx, caly)
            machine.nvs_setint("tpcalibX", calx)
            machine.nvs_setint("tpcalibY", caly)
            self.tft.text(self.tft.CENTER, self.tft.LAST_Y, "Saved to NVS", self.tft.ORANGE)
            print("Calibration completed and saved to NVS memory.")
        else:
            print("Calibration completed.")
        print("X = ({},{})  Y = ({},{})".format(math.ceil(clx), math.ceil(crx), math.ceil(cty), math.ceil(cby)))
        return calx, caly

