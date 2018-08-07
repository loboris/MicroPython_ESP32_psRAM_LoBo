#ifndef INC_NMEA_GPGLL_H
#define INC_NMEA_GPGLL_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nmea.h>

typedef struct {
	nmea_s base;
	nmea_position longitude;
	nmea_position latitude;
	struct tm time;
	uint8_t valid;
} nmea_gpgll_s;

/* Value indexes */
#define NMEA_GPGLL_LATITUDE				0
#define NMEA_GPGLL_LATITUDE_CARDINAL	1
#define NMEA_GPGLL_LONGITUDE			2
#define NMEA_GPGLL_LONGITUDE_CARDINAL	3
#define NMEA_GPGLL_TIME					4
#define NMEA_GPGLL_VALID				5

#endif  /* INC_NMEA_GPGLL_H */
