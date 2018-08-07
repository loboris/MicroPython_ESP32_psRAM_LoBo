/* ----------------------------- VTG Data Struct ------------------------------ */

/*
	Track Made Good and Ground Speed.

	eg1. $GPVTG,360.0,T,348.7,M,000.0,N,000.0,K*43
	eg2. $GPVTG,054.7,T,034.4,M,005.5,N,010.2,K


			   054.7,T      True track made good
			   034.4,M      Magnetic track made good
			   005.5,N      Ground speed, knots
			   010.2,K      Ground speed, Kilometers per hour


	eg3. $GPVTG,t,T,,,s.ss,N,s.ss,K*hh
	1    = Track made good
	2    = Fixed text 'T' indicates that track made good is relative to true north
	3    = not used
	4    = not used
	5    = Speed over ground in knots
	6    = Fixed text 'N' indicates that speed over ground in in knots
	7    = Speed over ground in kilometers/hour
	8    = Fixed text 'K' indicates that speed over ground is in kilometers/hour
	9    = Checksum
	The actual track made good and speed relative to the ground.

	$--VTG,x.x,T,x.x,M,x.x,N,x.x,K
	x.x,T = Track, degrees True
	x.x,M = Track, degrees Magnetic
	x.x,N = Speed, knots
	x.x,K = Speed, Km/hr
 */

#include "../nmea/parser_types.h"
#include "gpvtg.h"
#include "parse.h"

//-----------------------------
int init(nmea_parser_s *parser)
{
	/* Declare what sentence type to parse */
	NMEA_PARSER_TYPE(parser, NMEA_VTG);
	NMEA_PARSER_PREFIX(parser, "VTG");
	NMEA_PARSER_IDS(parser, "GPGNGL");
	return 0;
}

//--------------------------------------
int allocate_data(nmea_parser_s *parser)
{
	parser->data = malloc(sizeof (nmea_gpvtg_s));
	if (NULL == parser->data) {
		return -1;
	}

	return 0;
}

//------------------------------------
int set_default(nmea_parser_s *parser)
{
	memset(parser->data, 0, sizeof (nmea_gpvtg_s));
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
	nmea_gpvtg_s *data = (nmea_gpvtg_s *) parser->data;
	switch (val_index) {
	case NMEA_GPVTG_COURSE:
		/* Track made good */
		data->course = strtof(value, NULL);
		break;

	case NMEA_GPVTG_SPEED_KNOTS:
		/* Speed over ground in knots */
		data->speed_kn = strtof(value, NULL);
		break;

	case NMEA_GPVTG_SPEED_KMH:
		/* Speed over ground in km/h */
		data->speed_kmh = strtof(value, NULL);
		break;

	default:
		break;
	}

	return 0;
}
