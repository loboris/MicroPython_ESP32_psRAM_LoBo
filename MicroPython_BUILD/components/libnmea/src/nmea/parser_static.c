#include "nmea.h"
#include "parser.h"

#define PARSER_COUNT	5

#define DECLARE_PARSER_API(modname) \
	extern int nmea_##modname##_init(nmea_parser_s *parser); \
	extern int nmea_##modname##_allocate_data(nmea_parser_s *parser); \
	extern int nmea_##modname##_set_default(nmea_parser_s *parser); \
	extern int nmea_##modname##_free_data(nmea_s *data); \
	extern int nmea_##modname##_parse(nmea_parser_s *parser, char *value, int val_index);

#define PARSER_LOAD(modname) \
	parser = &(parsers[i]); \
	parser->handle = NULL; \
	parser->allocate_data = nmea_##modname##_allocate_data; \
	parser->set_default = nmea_##modname##_set_default; \
	parser->free_data = nmea_##modname##_free_data; \
	parser->parse = nmea_##modname##_parse; \
	if (-1 == nmea_##modname##_init((nmea_parser_s *) parser)) { \
		return -1; \
	} \
	i++;

DECLARE_PARSER_API(gpgll)
DECLARE_PARSER_API(gpgga)
DECLARE_PARSER_API(gprmc)
DECLARE_PARSER_API(gpgst)
DECLARE_PARSER_API(gpvtg)

nmea_parser_module_s parsers[PARSER_COUNT];

//----------------------------------------------------------
nmea_parser_module_s *nmea_init_parser(const char *filename)
{
	/* This function intentionally returns NULL */
	return NULL;
}

//---------------------
int nmea_load_parsers()
{
	int i = 0;
	nmea_parser_module_s *parser;

	PARSER_LOAD(gpgll);
	PARSER_LOAD(gpgga);
	PARSER_LOAD(gprmc);
	PARSER_LOAD(gpgst);
	PARSER_LOAD(gpvtg);

	return PARSER_COUNT;
}

//------------------------
void nmea_unload_parsers()
{
	/* This function body is intentionally left empty,
	     because there is no dynamic memory allocations. */
}

//--------------------------------------------------------
nmea_parser_module_s *nmea_get_parser_by_type(nmea_t type)
{
	int i;

	for (i = 0; i < PARSER_COUNT; i++) {
		if (type == parsers[i].parser.type) {
			return &(parsers[i]);
		}
	}

	return (nmea_parser_module_s *) NULL;
}

//---------------------------------------------------------------------
nmea_parser_module_s *nmea_get_parser_by_sentence(const char *sentence)
{
	int i;

	char type_prefix[NMEA_ID_LENGTH+1] = {'\0'};
	memcpy(type_prefix, sentence+1, NMEA_ID_LENGTH);
	for (i = 0; i < PARSER_COUNT; i++) {
		if (strstr(parsers[i].parser.type_prefixes, type_prefix) == NULL) {
			continue;
		}
		if (0 == strncmp(sentence + NMEA_ID_LENGTH + 1, parsers[i].parser.type_word, NMEA_PREFIX_LENGTH)) {
			return &(parsers[i]);
		}
	}

	return (nmea_parser_module_s *) NULL;
}
