#include "../nmea/parser_types.h"
#include "gpgst.h"
#include "parse.h"

//-----------------------------
int init(nmea_parser_s *parser)
{
	/* Declare what sentence type to parse */
	NMEA_PARSER_TYPE(parser, NMEA_GST);
	NMEA_PARSER_PREFIX(parser, "GST");
	NMEA_PARSER_IDS(parser, "GPGNGL");
	return 0;
}

//--------------------------------------
int allocate_data(nmea_parser_s *parser)
{
	parser->data = malloc(sizeof (nmea_gpgst_s));
	if (NULL == parser->data) {
		return -1;
	}

	return 0;
}

//------------------------------------
int set_default(nmea_parser_s *parser)
{
	memset(parser->data, 0, sizeof (nmea_gpgst_s));
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
	nmea_gpgst_s *data = (nmea_gpgst_s *) parser->data;

	switch (val_index) {
	case NMEA_GPGST_TIME:
		/* Parse time */
		if (-1 == nmea_time_parse(value, &data->time)) {
			return -1;
		}
		break;

	case NMEA_GPGST_RMSSD:
		/* Parse RMS value of the standard deviation of the range inputs */
		data->rmssd = strtof(value, NULL);
		break;

	case NMEA_GPGST_SDMAJ:
		/* Parse Standard deviation of semi-major axis of error ellipse */
		data->sdmaj = strtof(value, NULL);
		break;

	case NMEA_GPGST_SDMIN:
		/* Parse Standard deviation of semi-minor axis of error ellipse */
		data->sdmin = strtof(value, NULL);
		break;

	case NMEA_GPGST_ORI:
		/* Parse Orientation of semi-major axis of error ellipse */
		data->ori = strtof(value, NULL);
		break;

	case NMEA_GPGST_LATSD:
		/* Parse Standard deviation of latitude error, in meters */
		data->latsd = strtof(value, NULL);
		break;

	case NMEA_GPGST_LONSD:
		/* Parse Standard deviation of longitude error, in meters */
		data->lonsd = strtof(value, NULL);
		break;

	case NMEA_GPGST_ALTSD:
		/* Parse Standard deviation of altitude error, in meters */
		data->altsd = strtof(value, NULL);
		break;

	default:
		break;
	}

	return 0;
}
