#ifndef INC_NMEA_H
#define INC_NMEA_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* NMEA sentence types */
typedef enum {
	NMEA_UNKNOWN,
	NMEA_GGA,
	NMEA_GLL,
	NMEA_RMC,
	NMEA_GST,
	NMEA_VTG
} nmea_t;

/* NMEA cardinal direction types */
typedef char nmea_cardinal_t;
#define NMEA_CARDINAL_DIR_NORTH		(nmea_cardinal_t) 'N'
#define NMEA_CARDINAL_DIR_EAST		(nmea_cardinal_t) 'E'
#define NMEA_CARDINAL_DIR_SOUTH		(nmea_cardinal_t) 'S'
#define NMEA_CARDINAL_DIR_WEST		(nmea_cardinal_t) 'W'
#define NMEA_CARDINAL_DIR_UNKNOWN	(nmea_cardinal_t) '\0'

extern const char *NMEA_TAG;

/**
 * NMEA data base struct
 *
 * This struct will be extended by the parser data structs (ex: nmea_gpgll_s).
 */
typedef struct {
	nmea_t type;
	int errors;
} nmea_s;

/* GPS position struct */
typedef struct {
	double minutes;
	int degrees;
	nmea_cardinal_t cardinal;
} nmea_position;

/* NMEA sentence max length, including \r\n (chars) */
#define NMEA_MAX_LENGTH		83

/* NMEA sentence endings, should be \r\n according the NMEA 0183 standard */
#define NMEA_END_CHAR_1		'\r'
#define NMEA_END_CHAR_2		'\n'

/* NMEA sentence prefix length (num chars), Ex: GPGLL */
#define NMEA_PREFIX_LENGTH	3
#define NMEA_IDS_LENGTH	6
#define NMEA_ID_LENGTH	2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get the sentence type.
 *
 * sentence needs to be a validated NMEA sentence string.
 *
 * Returns nmea_t (int).
 */
extern nmea_t nmea_get_type(const char *sentence);

/**
 * Calculate the checksum of the sentence.
 *
 * sentence needs to be a validated NMEA sentence string.
 *
 * Returns the calculated checksum (uint8_t).
 */
extern uint8_t nmea_get_checksum(const char *sentence);

/**
 * Check if the sentence contains a precalculated checksum.
 *
 * sentence needs to be a validated NMEA sentence string.
 * length is the character length of the sentence string.
 *
 * Return 0 if checksum exists, otherwise -1.
 */
extern int nmea_has_checksum(const char *sentence, size_t length);

/**
 * Validate the sentence according to NMEA 0183.
 *
 * Criterias:
 *   - Should be between the correct length.
 *   - Should start with a dollar sign.
 *   - The next five characters should be uppercase letters.
 *   - If it has a checksum, check it.
 *   - Ends with the correct 2 characters.
 *
 * length is the character length of the sentence string.
 *
 * Returns 0 if sentence is valid, otherwise error code (< 0).
 */
extern int nmea_validate(const char *sentence, size_t length, int check_checksum);

/**
 * Free an nmea data struct.
 *
 * data should be a pointer to a struct of type nmea_s.
 */
extern void nmea_free(nmea_s *data);

/**
 * Parse an NMEA sentence string to a struct.
 *
 * sentence needs to be a validated NMEA sentence string.
 * length is the character length of the sentence string.
 * check_checksum, if 1 and there is a checksum, validate it.
 *
 * Returns a pointer to an NMEA data struct, or (nmea_s *) NULL if an error occurs.
 */
extern nmea_s *nmea_parse(char *sentence, size_t length, int check_checksum);

#ifdef __cplusplus
}
#endif

#endif  /* INC_NMEA_H */
