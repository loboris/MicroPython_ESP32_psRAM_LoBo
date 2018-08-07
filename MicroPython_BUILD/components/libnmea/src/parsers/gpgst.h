#ifndef INC_NMEA_GPGST_H
#define INC_NMEA_GPGST_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nmea.h>

typedef struct {
	nmea_s base;
	struct tm time;
	float	rmssd;
	float	sdmaj;
	float	sdmin;
	float	ori;
	float	latsd;
	float	lonsd;
	float	altsd;
} nmea_gpgst_s;

/* Value indexes */
#define NMEA_GPGST_TIME					0
#define NMEA_GPGST_RMSSD				1
#define NMEA_GPGST_SDMAJ				2
#define NMEA_GPGST_SDMIN				3
#define NMEA_GPGST_ORI					4
#define NMEA_GPGST_LATSD				5
#define NMEA_GPGST_LONSD				6
#define NMEA_GPGST_ALTSD				7

#endif  /* INC_NMEA_GPGGA_H */
