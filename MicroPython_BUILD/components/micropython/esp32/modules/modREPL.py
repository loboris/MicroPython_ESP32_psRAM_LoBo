import _thread
import sys

def thREPL():
    while True:
        print(">>> Starting REPL in thread <<<\n")
        sys.REPL()

_thread.stack_size(sys.stackSize())
REPLthread = _thread.start_new_thread("REPLthread", thREPL, ())
_thread.stack_size(4096)
