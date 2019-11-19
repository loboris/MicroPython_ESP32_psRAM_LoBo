import machine

uart = machine.UART(1, tx=10, rx=9)

def receive(bufsize):
    return uart.read(bufsize)

def send(line, data=b""):
    uart.write(line)
    uart.write("\n")
    if data:
        uart.write(data)