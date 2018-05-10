/* ----------------------------- GGA Data Struct ------------------------------ */

//GGA - essential fix data which provide 3D location and accuracy data.
//
// $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
//
//Where:
//     GGA          Global Positioning System Fix Data
//     123519       Fix taken at 12:35:19 UTC
//     4807.038,N   Latitude 48 deg 07.038' N
//     01131.000,E  Longitude 11 deg 31.000' E
//     1            Fix quality: 0 = invalid
//                               1 = GPS fix (SPS)
//                               2 = DGPS fix
//                               3 = PPS fix
//			      				 4 = Real Time Kinematic
//			    				 5 = Float RTK
//            				     6 = estimated (dead reckoning) (2.3 feature)
//			  				     7 = Manual input mode
//			    				 8 = Simulation mode
//     08           Number of satellites being tracked
//     0.9          Horizontal dilution of position
//     545.4,M      Altitude, Meters, above mean sea level
//     46.9,M       Height of geoid (mean sea level) above WGS84
//                      ellipsoid
//     (empty field) time in seconds since last DGPS update
//     (empty field) DGPS station ID number
//     *47          the checksum data, always begins with *
//
//	 If the height of geoid is missing then the altitude should be suspect. Some non-standard
//	 implementations report altitude with respect to the ellipsoid rather than geoid altitude.
//	 Some units do not report negative altitudes at alUART driver interface. This is the only
//	 sentence that reports altitude.

#include "../nmea/parser_types.h"
#include "gpgga.h"
#include "parse.h"

//-----------------------------
int init(nmea_parser_s *parser)
{
	/* Declare what sentence type to parse */
	NMEA_PARSER_TYPE(parser, NMEA_GGA);
	NMEA_PARSER_PREFIX(parser, "GGA");
	NMEA_PARSER_IDS(parser, "GPGNGL");
	return 0;
}

//--------------------------------------
int allocate_data(nmea_parser_s *parser)
{
	parser->data = malloc(sizeof (nmea_gpgga_s));
	if (NULL == parser->data) {
		return -1;
	}

	return 0;
}

//------------------------------------
int set_default(nmea_parser_s *parser)
{
	memset(parser->data, 0, sizeof (nmea_gpgga_s));
	return 0;
}

//-------------------------
int free_data(nmea_s *data)
{
	free(data);
	return 0;
}

// Parse Global Positioning System Fix Data sentence
//----------------------------------------------------------
int parse(nmea_parser_s *parser, char *value, int val_index)
{
	nmea_gpgga_s *data = (nmea_gpgga_s *) parser->data;

	switch (val_index) {
	case NMEA_GPGGA_TIME:
		/* Parse time */
		if (-1 == nmea_time_parse(value, &data->time)) {
			return -1;
		}
		break;

	case NMEA_GPGGA_LATITUDE:
		/* Parse latitude */
		if (-1 == nmea_position_parse(value, &data->latitude)) {
			return -1;
		}
		break;

	case NMEA_GPGGA_LATITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->latitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->latitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPGGA_LONGITUDE:
		/* Parse longitude */
		if (-1 == nmea_position_parse(value, &data->longitude)) {
			return -1;
		}
		break;

	case NMEA_GPGGA_LONGITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->longitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->longitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPGGA_N_SATELLITES:
		/* Parse number of satellies */
		data->n_satellites = atoi(value);
		break;

	case NMEA_GPGGA_ALTITUDE:
		/* Parse altitude */
		data->altitude = strtof(value, NULL);
		break;

	case NMEA_GPGGA_ALTITUDE_UNIT:
		/* Parse altitude unit */
		data->altitude_unit = *value;
		break;

	case NMEA_GPGGA_QUALITY:
		/* GPS Quality Indicator */
		data->quality = (int)strtol(value, NULL, 0);
		break;

	case NMEA_GPGGA_DOP:
		/* Horizontal Dilution of precision */
		data->dop = strtof(value, NULL);
		break;

	default:
		break;
	}

	return 0;
}
