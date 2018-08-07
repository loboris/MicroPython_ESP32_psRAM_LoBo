import sys
import os
import time

class LS:

    def __repr__(self):
        self.__call__()
        return ""

    def __call__(self, path="."):
        if path == "/":
            l = os.listdir('/')
            for f in l:
                print("   <dir>  %s" % (f))
            return

        if path == ".":
            path = os.getcwd()
        try:
            st = os.stat(path)
        except:
            print("Not a directory")
            return
            
        if not (st[0] & 0x4000):
            print("Not a directory")
            return

        origpath = os.getcwd()
        os.chdir(path)
        l = os.listdir()
        l.sort()
        total = 0
        for f in l:
            st = os.stat(f)
            ftime = time.strftime("%Y/%m/%d %X", time.localtime(st[8]))
            if st[0] & 0x4000:  # stat.S_IFDIR
                print("   <dir>  %s  %s" % (ftime, f))
            else:
                print("% 8d  %s  %s" % (st[6], ftime, f))
                total += st[6]
        drive = os.getdrive()
        if drive != "/":
            stvfs = os.statvfs(drive)
            if total < (10*1024):
                stotal = "{} B".format(total)
            elif total < (1024*1024):
                stotal = "{:0.2f} KB".format(total/1024)
            else:
                stotal = "{:0.3f} MB".format(total/(1024*1024))
            print("\nTotal in '{}': {}".format(path, stotal))

            dsize = stvfs[0]*stvfs[2]
            dfree = stvfs[0]*stvfs[3]
            if dsize < (1024*1024):
                ssize = "{:0.2f} KB".format(dsize/(1024))
            elif dsize < (1024*1024*1024):
                ssize = "{:0.3f} MB".format(dsize/(1024*1024))
            else:
                ssize = "{:0.3f} GB".format(dsize/(1024*1024*1024))

            if dfree < (1024*1024):
                sfree = "{:0.2f} KB".format(dfree/(1024))
            elif dsize < (1024*1024*1024):
                sfree = "{:0.3f} MB".format(dfree/(1024*1024))
            else:
                sfree = "{:0.3f} GB".format(dfree/(1024*1024*1024))

            print("Drive: '{}' size: {}, free: {}".format(drive, ssize, sfree))
        os.chdir(origpath)

class PWD:

    def __repr__(self):
        return os.getcwd()

    def __call__(self):
        return self.__repr__()

class CLEAR:
    def __repr__(self):
        return "\x1b[2J\x1b[H"

    def __call__(self):
        return self.__repr__()

class CP:

    def __repr__(self):
        self.__call__()
        return ""

    def __call__(self, srcf, destf):
        if srcf == destf:
            print("Destination file must be different than source")
            return
        try:
            st = os.stat(srcf)
        except:
            print("'{}' not found".format(srcf))
            return
        try:
            st = os.stat(destf)
            print("'{}' exists".format(destf))
            return
        except:
            pass
        try:
            fs = open(srcf, 'rb')
            try:
                fd = open(destf, 'wb')
            except:
                fs.close()
                print("Error opening '{}'".format(srcf))
                return
            try:
                while True:
                    buf = fs.read(1024)
                    if len(buf) == 0:
                        break
                    else:
                        fd.write(buf)
            except:
                try:
                    fd.close()
                    os.remove(destf)
                except:
                    pass
                print("Error during copy")
            fd.close()
            fs.close()
        except:
            print("Error opening '{}'".format(srcf))
        

pwd = PWD()
ls = LS()
clear = CLEAR()
cp = CP()

cd = os.chdir
mkdir = os.mkdir
mv = os.rename
rm = os.remove
rmdir = os.rmdir

def head(f, n=10):
    with open(f) as f:
        for i in range(n):
            l = f.readline()
            if not l: break
            sys.stdout.write(l)

def cat(f):
    head(f, 1 << 30)

def newfile(path):
    print("Type file contents line by line, finish with EOF (Ctrl+D).")
    with open(path, "w") as f:
        while 1:
            try:
                l = input()
            except EOFError:
                break
            f.write(l)
            f.write("\n")

class Man():

    def __repr__(self):
        return("""
upysh is intended to be imported using:
from upysh import *

To see this help text again, type "man".

upysh commands:
pwd, cd("new_dir"), ls, ls(...), head(...), cat(...)
newfile(...), mv("old", "new"), rm(...), mkdir(...), rmdir(...)
cp("from", "to")
clear
""")

man = Man()

print(man)
