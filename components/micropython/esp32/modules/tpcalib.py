# Touch pannel calibration for ILI9341 based displays


import display, math, machine

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
        sx = 0
        sy = 0
        n = 0
        
        while n < 8:
            self.tft.text(self.tft.CENTER, 110, " PRESS ", self.tft.CYAN)
            self.tft.text(self.tft.CENTER, 110, " PRESS ", self.tft.CYAN)
            # wait for touch
            touch, x, y = self.tft.gettouch(True)
            while not touch:
                touch, x, y = self.tft.gettouch(True)
            # wait for release
            while touch:
                if n == 8:
                    self.tft.text(self.tft.CENTER, 110, "RELEASE", self.tft.CYAN)
                    self.tft.text(self.tft.CENTER, 110, "RELEASE", self.tft.CYAN)
                touch, x, y = self.tft.gettouch(True)
                if touch and (n < 256):
                    sx += x
                    sy += y
                    n += 1

        return (sx / n), (sy / n)

    #----------------------------
    def calibrate(self, x, y, i):
        self.drawCrossHair(x,y, self.tft.YELLOW)
        self.rx[i], self.ry[i] = self.readCoordinates()
        self.drawCrossHair(x,y,self.tft.GREEN)

    #-------------------------------------
    def tpcalib(self, tptype, save=False):
        if tptype == self.tft.TOUCH_XPT:
            self.tft.orient(display.TFT.LANDSCAPE)
        if tptype == self.tft.TOUCH_STMPE:
            self.tft.orient(display.TFT.PORTRAIT)
        else:
            print("Wrong touch type, use tft.TOUCH_XPT or TOUCH_STMPE")
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

        self.calibrate(10, 10, 0)
        self.calibrate(10, dispy//2, 1)
        self.calibrate(10, dispy-11, 2)
        self.calibrate(dispx//2, 10, 3)
        self.calibrate(dispx//2, dispy-11, 4)
        self.calibrate(dispx-11, 10, 5)
        self.calibrate(dispx-11, dispy//2, 6)
        self.calibrate(dispx-11, dispy-11, 7)

        px = abs((((self.rx[3]+self.rx[4]+self.rx[7]) / 3) - ((self.rx[0]+self.rx[0]+self.rx[2]) / 3)) / (dispy-20))  # LANDSCAPE
        clx = (((self.rx[0]+self.rx[1]+self.rx[2])/3))  # LANDSCAPE
        crx = (((self.rx[5]+self.rx[6]+self.rx[7])/3))  # LANDSCAPE

        if (clx < crx):
            clx = clx - (px*10)
            crx = crx + (px*10)
        else:
            clx = clx + (px*10)
            crx = crx - (px*10)

        py = abs((((self.ry[0]+self.ry[3]+self.ry[5])/3) - ((self.ry[2]+self.ry[4]+self.ry[7])/3))/(dispx-20))  # LANDSCAPE
        cty = (((self.ry[0]+self.ry[3]+self.ry[5])/3))  # LANDSCAPE
        cby = (((self.ry[2]+self.ry[4]+self.ry[7])/3))  # LANDSCAPE

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
        self.tft.text(self.tft.CENTER, self.tft.CENTER, "Calibration completed")
        if save:
            self.tft.setCalib(calx, caly)
            machine.nvs_setint("tpcalibX", calx)
            machine.nvs_setint("tpcalibY", caly)
            print("Calibration completed and saved to NVS memory.")
        else:
            print("Calibration completed.")
        print("X:", math.ceil(clx), math.ceil(crx), "Y:", math.ceil(cty), math.ceil(cby))
        return calx, caly

