Notes for future development on power saving:

- frequency scaling
- moving threads between cores (in order to turn off one)
- reduce radio ops
- match LNA and Antenna impedance (to reduce tx power)
- use raw networking (avoid LwIP)
- write directly to DPORT registers (ie: no esp-idf) in order to switch off peripherials
- move everything in ram in order to turn off the cache (ie: the flash)
- reduce logs, don't send them to UART
- make the bootloader skipping flash checksumming at every boot/awake from deep sleep.
- use the ULP processor as much as possible
