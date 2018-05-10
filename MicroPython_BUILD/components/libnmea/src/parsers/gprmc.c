/* ----------------------------- RMC Data Struct ------------------------------ */

//RMC - NMEA has its own version of essential gps pvt (position, velocity, time) data.
//It is called RMC, The Recommended Minimum, which will look similar to:
//
//$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
//
//Where:
//     RMC          Recommended Minimum sentence C
//     123519       Fix taken at 12:35:19 UTC
//     A            Status A=active or V=Void.
//     4807.038,N   Latitude 48 deg 07.038' N
//     01131.000,E  Longitude 11 deg 31.000' E
//     022.4        Speed over the ground in knots
//     084.4        Track angle in degrees True
//     230394       Date - 23rd of March 1994
//     003.1,W      Magnetic Variation
//     *6A          The checksum data, always begins with *

#include "../nmea/parser_types.h"
#include "gprmc.h"
#include "parse.h"

//-----------------------------
int init(nmea_parser_s *parser)
{
	/* Declare what sentence type to parse */
	NMEA_PARSER_TYPE(parser, NMEA_RMC);
	NMEA_PARSER_PREFIX(parser, "RMC");
	NMEA_PARSER_IDS(parser, "GPGNGL");
	return 0;
}

//--------------------------------------
int allocate_data(nmea_parser_s *parser)
{
	parser->data = malloc(sizeof (nmea_gprmc_s));
	if (NULL == parser->data) {
		return -1;
	}

	return 0;
}

//------------------------------------
int set_default(nmea_parser_s *parser)
{
	memset(parser->data, 0, sizeof (nmea_gprmc_s));
	return 0;
}

//-------------------------
int free_data(nmea_s *data)
{
	free(data);
	return 0;
}

// Parse Recommended Minimum Navigation Information sentence
//----------------------------------------------------------
int parse(nmea_parser_s *parser, char *value, int val_index)
{
	nmea_gprmc_s *data = (nmea_gprmc_s *) parser->data;
	switch (val_index) {
	case NMEA_GPRMC_TIME:
		/* Parse time */
		if (-1 == nmea_time_parse(value, &data->time)) {
			return -1;
		}
		break;

	case NMEA_GPRMC_LATITUDE:
		/* Parse latitude */
		if (-1 == nmea_position_parse(value, &data->latitude)) {
			return -1;
		}
		break;

	case NMEA_GPRMC_LATITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->latitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->latitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPRMC_LONGITUDE:
		/* Parse longitude */
		if (-1 == nmea_position_parse(value, &data->longitude)) {
			return -1;
		}
		break;

	case NMEA_GPRMC_LONGITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->longitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->longitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPRMC_DATE:
		/* Parse date */
		if (-1 == nmea_date_parse(value, &data->time)) {
			return -1;
		}
		break;

	case NMEA_GPRMC_VALID:
		/* Status, V = Navigation receiver warning */
		data->valid = (strcmp(value, "A") == 0);
		break;

	case NMEA_GPRMC_SPEED:
		/* Speed over ground, knots */
		data->speed = strtof(value, NULL);
		break;

	case NMEA_GPRMC_COURSE:
		/* Track made good, degrees true */
		data->course = strtof(value, NULL);
		break;

	default:
		break;
	}

	return 0;
}
