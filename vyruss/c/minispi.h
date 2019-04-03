// Mini SPI driver
// based on https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-spi.c

void spiStartBuses(uint32_t freq);
void spiWriteNL(int bus, const void * data_in, size_t len);
