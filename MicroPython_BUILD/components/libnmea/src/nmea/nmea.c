#include "nmea.h"
#include "parser.h"
#include "parser_types.h"
#include "esp_log.h"

#define ARRAY_LENGTH(a) (sizeof a / sizeof (a[0]))

const char *NMEA_TAG = "NMEA";

/**
 * Check if a value is not NULL and not empty.
 *
 * Returns 0 if set, otherwise -1.
 */
//-----------------------------------------
static int _is_value_set(const char *value)
{
	if (NULL == value || '\0' == *value) {
		return -1;
	}

	return 0;
}

/**
 * Crop a sentence from the type word and checksum.
 *
 * The type word at the beginning along with the dollar sign ($) will be
 * removed. If there is a checksum, it will also be removed. The two end
 * characters (usually <CR><LF>) will not be included in the new string.
 *
 * sentence is a validated NMEA sentence string.
 * length is the char length of the sentence string.
 *
 * Returns pointer (char *) to the new string.
 */
//--------------------------------------------------------
static char *_crop_sentence(char *sentence, size_t length)
{
	/* Skip type word, 7 characters (including $ and ,) */
	sentence += NMEA_PREFIX_LENGTH + NMEA_ID_LENGTH + 2;

	/* Null terminate before end of line/sentence, 2 characters */
	sentence[length - 9] = '\0';

	/* Remove checksum, if there is one */
	if ('*' == sentence[length - 12]) {
		sentence[length - 12] = '\0';
	}

	return sentence;
}

/**
 * Splits a string by comma.
 *
 * string is the string to split, will be manipulated. Needs to be
 *        null-terminated.
 * values is a char pointer array that will be filled with pointers to the
 *        splitted values in the string.
 * max_values is the maximum number of values to be parsed.
 *
 * Returns the number of values found in string.
 */
//----------------------------------------------------------------------------
static int _split_string_by_comma(char *string, char **values, int max_values)
{
	int i = 0;

	values[i++] = string;
	while (i < max_values && NULL != (string = strchr(string, ','))) {
		*string = '\0';
		values[i++] = ++string;
	}

	return i;
}

/**
 * Initiate the NMEA library and load the parser modules.
 *
 * This function will be called before the main() function.
 */
void __attribute__ ((constructor)) nmea_init(void);
//--------------
void nmea_init()
{
	nmea_load_parsers();
}

/**
 * Unload the parser modules.
 *
 * This function will be called after the exit() function.
 */
void __attribute__ ((destructor)) nmea_cleanup(void);
//-----------------
void nmea_cleanup()
{
	nmea_unload_parsers();
}

//----------------------------------------
nmea_t nmea_get_type(const char *sentence)
{
	nmea_parser_module_s *parser = nmea_get_parser_by_sentence(sentence);
	if (NULL == parser) {
		return NMEA_UNKNOWN;
	}

	return parser->parser.type;
}

//---------------------------------------------
uint8_t nmea_get_checksum(const char *sentence)
{
	const char *n = sentence + 1;
	uint8_t chk = 0;

	/* While current char isn't '*' or sentence ending (newline) */
	while ('*' != *n && NMEA_END_CHAR_1 != *n && '\0' != *n) {
		chk ^= (uint8_t) *n;
		n++;
	}

	return chk;
}

//--------------------------------------------------------
int nmea_has_checksum(const char *sentence, size_t length)
{
	if ('*' == sentence[length - 5]) {
		return 0;
	}

	return -1;
}

//------------------------------------------------------------------------
int nmea_validate(const char *sentence, size_t length, int check_checksum)
{
	const char *n;

	/* should have atleast 9 characters */
	if (9 > length) {
		ESP_LOGD(NMEA_TAG, "Sentence too short (%d)", length);
		return -1;
	}

	/* should be less or equal to NMEA_MAX_LENGTH characters */
	if (NMEA_MAX_LENGTH < length) {
		ESP_LOGD(NMEA_TAG, "Sentence too long (%d)", length);
		return -2;
	}

	/* should start with $ */
	if ('$' != *sentence) {
		ESP_LOGD(NMEA_TAG, "Sentence not starting with '$'");
		return -3;
	}

	/* should end with \r\n, or other... */
	if (NMEA_END_CHAR_2 != sentence[length - 1] || NMEA_END_CHAR_1 != sentence[length - 2]) {
		ESP_LOGD(NMEA_TAG, "Sentence does not end with 'CRLF'");
		return -4;
	}

	/* should have a 5 letter, upper case word */
	n = sentence;
	while (++n < sentence + 6) {
		if (*n < 'A' || *n > 'Z') {
			/* not upper case letter */
			ESP_LOGD(NMEA_TAG, "Wrong sentence header");
			return -5;
		}
	}

	/* should have a comma after the type word */
	if (',' != sentence[6]) {
		ESP_LOGD(NMEA_TAG, "Wrong sentence header, no comma");
		return -6;
	}

	/* test for not allowed characters */
	n = sentence+6;
	while (++n < (sentence + length - 2)) {
		if ((*n < ' ') || (*n > 'z') || (*n == '$')) {
			/* not allowed character */
			ESP_LOGD(NMEA_TAG, "Sentence contains invalid characters");
			return -7;
		}
	}

	/* check for checksum */
	if (1 == check_checksum && 0 == nmea_has_checksum(sentence, length)) {
		uint8_t actual_chk;
		uint8_t expected_chk;
		char checksum[3];

		checksum[0] = sentence[length - 4];
		checksum[1] = sentence[length - 3];
		checksum[2] = '\0';
		actual_chk = nmea_get_checksum(sentence);
		expected_chk = (uint8_t) strtol(checksum, NULL, 16);
		if (expected_chk != actual_chk) {
			ESP_LOGD(NMEA_TAG, "Wrong sentence checksum");
			return -8;
		}
	}

	return 0;
}

//--------------------------
void nmea_free(nmea_s *data)
{
	nmea_parser_module_s *parser;

	if (NULL == data) {
		return;
	}

	parser = nmea_get_parser_by_type(data->type);
	if (NULL == parser) {
		return;
	}

	parser->free_data(data);
}

//-------------------------------------------------------------------
nmea_s *nmea_parse(char *sentence, size_t length, int check_checksum)
{
	unsigned int n_vals, val_index;
	char *value, *val_string;
	char *values[255];
	nmea_parser_module_s *parser;
	nmea_t type;
	int res;

	/* Validate sentence string */
	res = nmea_validate(sentence, length, check_checksum);
	if (res < 0) {
		ESP_LOGD(NMEA_TAG, "Validate error (%d)", res);
		return (nmea_s *) NULL;
	}

	type = nmea_get_type(sentence);
	if (NMEA_UNKNOWN == type) {
		ESP_LOGD(NMEA_TAG, "Get type error");
		return (nmea_s *) NULL;
	}

	/* Crop sentence from type word and checksum */
	val_string = _crop_sentence(sentence, length);
	if (NULL == val_string) {
		ESP_LOGD(NMEA_TAG, "Sentence crop error");
	    return (nmea_s *) NULL;
	}

	/* Split the sentence into values */
	n_vals = _split_string_by_comma(val_string, values, ARRAY_LENGTH(values));
	if (0 == n_vals) {
		ESP_LOGD(NMEA_TAG, "Sentence split error");
		return (nmea_s *) NULL;
	}

	/* Get the right parser */
	parser = nmea_get_parser_by_type(type);
	if (NULL == parser) {
		ESP_LOGD(NMEA_TAG, "Get parser error");
		return (nmea_s *) NULL;
	}

	/* Allocate memory for parsed data */
	parser->allocate_data((nmea_parser_s *) parser);
	if (NULL == parser->parser.data) {
		ESP_LOGD(NMEA_TAG, "Error allocating parser data");
		return (nmea_s *) NULL;
	}

	/* Set default values */
	parser->set_default((nmea_parser_s *) parser);
	parser->errors = 0;

	/* Loop through the values and parse them... */
	for (val_index = 0; val_index < n_vals; val_index++) {
		value = values[val_index];
		if (-1 == _is_value_set(value)) {
			continue;
		}

		if (-1 == parser->parse((nmea_parser_s *) parser, value, val_index)) {
			parser->errors++;
			ESP_LOGD(NMEA_TAG, "Parser error at index %d",val_index);
		}
	}

	parser->parser.data->type = type;
	parser->parser.data->errors = parser->errors;

	return parser->parser.data;
}
