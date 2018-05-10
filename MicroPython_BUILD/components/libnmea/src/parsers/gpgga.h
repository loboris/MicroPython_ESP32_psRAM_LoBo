#ifndef INC_NMEA_GPGGA_H
#define INC_NMEA_GPGGA_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nmea.h>

typedef struct {
	nmea_s base;
	struct tm time;
	nmea_position longitude;
	nmea_position latitude;
	int n_satellites;
	float altitude;
	char altitude_unit;
	int quality;
	float dop;
} nmea_gpgga_s;

/* Value indexes */
#define NMEA_GPGGA_TIME					0
#define NMEA_GPGGA_LATITUDE				1
#define NMEA_GPGGA_LATITUDE_CARDINAL	2
#define NMEA_GPGGA_LONGITUDE			3
#define NMEA_GPGGA_LONGITUDE_CARDINAL	4
#define NMEA_GPGGA_QUALITY				5
#define NMEA_GPGGA_N_SATELLITES			6
#define NMEA_GPGGA_DOP					7
#define NMEA_GPGGA_ALTITUDE				8
#define NMEA_GPGGA_ALTITUDE_UNIT		9

#endif  /* INC_NMEA_GPGGA_H */
