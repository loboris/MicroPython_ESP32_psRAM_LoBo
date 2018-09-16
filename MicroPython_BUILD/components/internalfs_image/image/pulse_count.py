


"""
================================================================================

Pulse Counter using ESP32 ULP processor

    Uses ULP to count pulses on GPIO4 (which is TOUCH0/RTCIO10 in RTC domain)

================================================================================

Includes debouncer taken from code as popularised by Kenneth A. Kuhn.

This is an algorithm that debounces or removes random or spurious
transitions of a digital signal read as an input by a computer.  This is
particularly applicable when the input is from a mechanical contact.

The algorithm uses integration as opposed to edge logic
(differentiation), which makes it very robust in the presence of noise.

An integrator is used to perform a time hysteresis so that the signal must
persistently be in a logical state (0 or 1) in order for the output to change
to that state.  Random transitions of the input will not affect the output
except in the rare case where statistical clustering is longer than the
specified integration time.

The following example illustrates how this algorithm works. "real signal"
represents the real intended signal with no noise.  "corrupted" has significant
random transitions added to the real signal. "integrator" represents the
integration which is constrained to be between 0 and 3.  "output" only makes a
transition when the integrator reaches either 0 or 3.

Note that the output signal lags the input signal by the integration time but
is free of spurious transitions.

real signal 0000111111110000000111111100000000011111111110000000000111111100000

corrupted   0100111011011001000011011010001001011100101111000100010111011100010
integrator  0100123233233212100012123232101001012321212333210100010123233321010
output      0000001111111111100000001111100000000111111111110000000001111111000


--------------------------------------------------------------------------------

"""

from machine import mem32
import utime
import machine

from esp32_ulp.__main__ import src_to_binary

source = """\
pulse_count:        .long 0             # 16 bit count of pulses returned to main processor
debounce:           .long 0             # maximum saturation value of integrator, from main
last_state:         .long 0             # internal record of last debounced state of pin
integrator:         .long 0


 .set RTC_GPIO_IN_REG, 0x3ff48424       # = DR_REG_RTCIO_BASE + 0x24

      /* Step 1: Update the integrator based on the input signal.  Note that the
      integrator follows the input, decreasing or increasing towards the limits as
      determined by the input state (0 or 1). */

entry:      move r3, integrator         # get integrator address
            reg_rd RTC_GPIO_IN_REG, 14, 24   # get all 16 ULP GPIOs into r0
            and r0, r0, 1               # isolate TOUCH0/RTCIO10/GPIO4
            jump pin_low, eq            # if pin high
            ld r0, r3, 0                # get integrator value
            jumpr done, debounce, ge    # if integrator < debounce max count
            add r0, r0, 1               # integrator +=1
            st r0, r3, 0
            jump chk_state

pin_low:
            ld r0, r3, 0                # get integrator value
            jumpr chk_state, 1, lt      # if integrator >= 1
            sub r0, r0, 1               # integrator -=1
            st r0, r3, 0

      /* Step 2: Update the output state based on the integrator.  Note that the
      output will only change state if the integrator has reached a limit, either
      0 or MAXIMUM. */

chk_state:
            move r2, last_state         # get last qualified state of pin
            ld r0, r2, 0
            jumpr chk_debounce, 1, lt   # if previous state =  0
            ld r0, r3, 0                # and
            jumpr done, 1, ge           # integrator < 1
            move r0, 0                  # falling edge qualified
            st r0, r2, 0                # so update last_state=0
            move r3, pulse_count
            ld r1, r3, 0                # and increment pulse count
            add r1, r1, 1
            st r1, r3, 0
            halt                        # halt 'til next wakeup

chk_debounce:                           # previous state was low
            ld r0, r3, 0                # get integrator value
            jumpr done, debounce, lt    # if integrator >= debounce max
            move r0,1
            st r0, r2, 0                # update last_state=1

done:
            halt                        # halt 'til next wakeup
"""



ULP_MEM_BASE = 0x50000000
ULP_DATA_MASK = 0xffff              # ULP data is only in lower 16 bits

ULP_VARS = 4                        # number of 32-bit vars used by ULP program

load_addr = 0
entry_addr = ULP_MEM_BASE + (ULP_VARS * 4)  # entry address is offset by length of vars

ulp_pulse_count = ULP_MEM_BASE + 0
ulp_debounce = ULP_MEM_BASE + 4


machine.Pin(4, machine.Pin.IN, machine.Pin.PULL_UP)
pin21 = machine.Pin(21, machine.Pin.OUT)


print ("\nLoading ULP...\n")

binary = src_to_binary(source)

ulp = machine.ULP()
ulp.set_wakeup_period(0, 100)       # use timer0, wakeup after ~100us
ulp.load_binary(load_addr, binary)


if  machine.nvs_getint('pulse_count_nv') == None:
    machine.nvs_setint('pulse_count_nv', 0)

init_count = machine.nvs_getint('pulse_count_nv')

pulse_count = init_count            # init pulse counts
pulse_gen_count = init_count
mem32[ulp_pulse_count] = init_count

mem32[ulp_debounce] = 3             # 3 sample debounce

ulp.run(entry_addr)


print ("\n\nPulse Count Test: \n----------------- \n")

while True:
    pulse_countL = mem32[ulp_pulse_count] & ULP_DATA_MASK
    if (pulse_count & 0xffff) > pulse_countL:
        pulse_count += 0x10000
    pulse_count =  (pulse_count & ~0xffff) + pulse_countL
    machine.nvs_setint('pulse_count_nv', pulse_count)

    print('Pulses Out:', pulse_gen_count)
    print('Pulses In :', pulse_count, '\n')

    for t in range (500):
        pin21.value(1)
        utime.sleep_ms(1)
        pin21.value(0)
        pulse_gen_count += 1
        utime.sleep_ms(1)
