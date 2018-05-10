#ifndef INC_NMEA_GPVTG_H
#define INC_NMEA_GPVTG_H

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nmea.h>

typedef struct {
	nmea_s base;
	float course;
	float speed_kn;
	float speed_kmh;
} nmea_gpvtg_s;

/* Value indexes */
#define NMEA_GPVTG_COURSE				0
#define NMEA_GPVTG_SPEED_KNOTS			4
#define NMEA_GPVTG_SPEED_KMH			6

#endif  /* INC_NMEA_GPVTG_H */
