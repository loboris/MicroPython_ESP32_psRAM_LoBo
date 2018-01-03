from machine import Pin, SPI, reset
import config_lora
import controller


'''
import time
PIN_ID_FOR_LORA_RESET = 18
PIN_ID_FOR_LORA_SS = 26
PIN_ID_SCK = 19
PIN_ID_MOSI = 23
PIN_ID_MISO = 25
PIN_ID_FOR_LORA_DIO0 = 5
ON_BOARD_LED_PIN_NO = 2

#spi = machine.SPI(1, baudrate = 8000000, sck = machine.Pin(PIN_ID_SCK, machine.Pin.OUT), mosi = machine.Pin(PIN_ID_MOSI, machine.Pin.OUT), miso = machine.Pin(PIN_ID_MISO, machine.Pin.IN), cs = machine.Pin(PIN_ID_FOR_LORA_SS, machine.Pin.OUT), duplex=False)
#spi = machine.SPI(1, baudrate = 8000000, sck = machine.Pin(PIN_ID_SCK, machine.Pin.OUT), mosi = machine.Pin(PIN_ID_MOSI, machine.Pin.OUT), miso = machine.Pin(PIN_ID_MISO, machine.Pin.IN), duplex=False)
spi = machine.SPI(1, baudrate = 8000000, sck = machine.Pin(PIN_ID_SCK, machine.Pin.OUT), mosi = machine.Pin(PIN_ID_MOSI, machine.Pin.OUT), miso = machine.Pin(PIN_ID_MISO, machine.Pin.IN))

dio=machine.Pin(PIN_ID_FOR_LORA_DIO0, machine.Pin.IN)
cs=machine.Pin(PIN_ID_FOR_LORA_SS, machine.Pin.OUT)
rst=machine.Pin(PIN_ID_FOR_LORA_RESET, machine.Pin.OUT)
rst.value(0)
response = bytearray(4)

def xrst():
    rst.value(0)
    time.sleep_ms(100)
    rst.value(1)

xrst()

def xrd(reg):
    cs.value(0)
    spi.write_readinto(bytes([reg]), response);
    cs.value(1)
    print(response)

def yrd(reg):
    cs.value(0)
    spi.write(bytes([reg]));
    spi.readinto(response)
    cs.value(1)
    print(response)

def zrd(reg):
    cs.value(0)
    spi.write(bytes([reg]));
    spi.write_readinto(bytes([0]),response)
    cs.value(1)
    print(response)

def wrd(reg):
    cs.value(0)
    res = spi.readfrom_mem(reg,4)
    cs.value(1)
    print(res)

'''

class Controller(controller.Controller):

    PIN_ID_FOR_LORA_RESET = 18

    PIN_ID_FOR_LORA_SS = 26
    PIN_ID_SCK = 19
    PIN_ID_MOSI = 23
    PIN_ID_MISO = 25

    PIN_ID_FOR_LORA_DIO0 = 5
    PIN_ID_FOR_LORA_DIO1 = None
    PIN_ID_FOR_LORA_DIO2 = None
    PIN_ID_FOR_LORA_DIO3 = None
    PIN_ID_FOR_LORA_DIO4 = None
    PIN_ID_FOR_LORA_DIO5 = None

    spi = None

    ON_BOARD_LED_PIN_NO = 2
    ON_BOARD_LED_HIGH_IS_ON = True
    GPIO_PINS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                    12, 13, 14, 15, 16, 17, 18, 19, 21, 22,
                    23, 25, 26, 27, 32, 34, 35, 36, 37, 38, 39)
    try:
        if not spi:
            spi = SPI(1, baudrate = 5000000, polarity = 0, phase = 0, bits = 8, firstbit = SPI.MSB,
                        sck = Pin(PIN_ID_SCK, Pin.OUT),
                        mosi = Pin(PIN_ID_MOSI, Pin.OUT),
                        miso = Pin(PIN_ID_MISO, Pin.IN))
            #spi.init()
            print(spi)

    except Exception as e:
        print(e)
        if spi:
            spi.deinit()
            spi = None
        reset()  # in case SPI is already in use, need to reset.


    def __init__(self,
                 spi = spi,
                 pin_id_led = ON_BOARD_LED_PIN_NO,
                 on_board_led_high_is_on = ON_BOARD_LED_HIGH_IS_ON,
                 pin_id_reset = PIN_ID_FOR_LORA_RESET,
                 blink_on_start = (2, 0.5, 0.5)):

        super().__init__(spi,
                         pin_id_led,
                         on_board_led_high_is_on,
                         pin_id_reset,
                         blink_on_start)


    def prepare_pin(self, pin_id, in_out = Pin.OUT):
        if pin_id is not None:
            pin = Pin(pin_id, in_out)
            new_pin = Controller.Mock()
            new_pin.pin_id = pin_id
            new_pin.value = pin.value

            if in_out == Pin.OUT:
                new_pin.low = lambda : pin.value(0)
                new_pin.high = lambda : pin.value(1)
            else:
                new_pin.irq = pin.irq

            return new_pin


    def prepare_irq_pin(self, pin_id):
        pin = self.prepare_pin(pin_id, Pin.IN)
        if pin:
            pin.set_handler_for_irq_on_rising_edge = lambda handler: pin.irq(handler = handler, trigger = Pin.IRQ_RISING)
            pin.detach_irq = lambda : pin.irq(handler = None, trigger = 0)
            return pin


    def prepare_spi(self, spi):
        if spi:
            new_spi = Controller.Mock()

            def transfer(pin_ss, address, value = 0x00):
                response = bytearray(1)

                pin_ss.low()

                spi.write(bytes([address])) # write register address
                spi.write_readinto(bytes([value]), response) # write or read register walue
                #spi.write_readinto(bytes([address]), response)

                pin_ss.high()

                return response

            new_spi.transfer = transfer
            new_spi.close = spi.deinit
            return new_spi


    def __exit__(self):
        self.spi.close()

