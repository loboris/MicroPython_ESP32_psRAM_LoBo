#include "../nmea/parser_types.h"
#include "gpgll.h"
#include "parse.h"

//-----------------------------
int init(nmea_parser_s *parser)
{
	/* Declare what sentence type to parse */
	NMEA_PARSER_TYPE(parser, NMEA_GLL);
	NMEA_PARSER_PREFIX(parser, "GLL");
	NMEA_PARSER_IDS(parser, "GPGNGL");
	return 0;
}

//--------------------------------------
int allocate_data(nmea_parser_s *parser)
{
	parser->data = malloc(sizeof (nmea_gpgll_s));
	if (NULL == parser->data) {
		return -1;
	}

	return 0;
}

//------------------------------------
int set_default(nmea_parser_s *parser)
{
	memset(parser->data, 0, sizeof (nmea_gpgll_s));
	return 0;
}

//-------------------------
int free_data(nmea_s *data)
{
	free(data);
	return 0;
}

// Parse Geographic Position – Latitude/Longitude sentence
//----------------------------------------------------------
int parse(nmea_parser_s *parser, char *value, int val_index)
{
	nmea_gpgll_s *data = (nmea_gpgll_s *) parser->data;

	switch (val_index) {
	case NMEA_GPGLL_TIME:
		/* Parse time */
		if (-1 == nmea_time_parse(value, &data->time)) {
			return -1;
		}
		break;

	case NMEA_GPGLL_LATITUDE:
		/* Parse latitude */
		if (-1 == nmea_position_parse(value, &data->latitude)) {
			return -1;
		}
		break;

	case NMEA_GPGLL_LATITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->latitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->latitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPGLL_LONGITUDE:
		/* Parse longitude */
		if (-1 == nmea_position_parse(value, &data->longitude)) {
			return -1;
		}
		break;

	case NMEA_GPGLL_LONGITUDE_CARDINAL:
		/* Parse cardinal direction */
		data->longitude.cardinal = nmea_cardinal_direction_parse(value);
		if (NMEA_CARDINAL_DIR_UNKNOWN == data->longitude.cardinal) {
			return -1;
		}
		break;

	case NMEA_GPGLL_VALID:
		/* Status A - Data Valid, V - Data Invalid */
		data->valid = (strcmp(value, "A") == 0);
		break;

	default:
		break;
	}

	return 0;
}
