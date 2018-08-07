/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* GPS module based on nmea parsing library from
 * 'https://github.com/jacketizer/libnmea', modified by LoBo
 */

#include "sdkconfig.h"

#ifdef CONFIG_MICROPY_USE_GPS

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "driver/uart.h"

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"

#include "machine_uart.h"
#include "modmachine.h"
#include "nmea.h"
#include "gpgll.h"
#include "gpgga.h"
#include "gprmc.h"
#include "gpgst.h"
#include "gpvtg.h"

#define EARTH_RADIUS_KM	6371.0


const char *GPS_TAG = "MODGPS";

typedef struct {
	struct tm datetime;
	float latitude;
	float longitude;
	float altitude;
	float speed;
	float course;
	float dop;
	uint8_t quality;
	uint8_t nsat;
} gps_data_t;

typedef struct {
	void *cb_func;
	uint8_t type;
	uint8_t compare;
	nmea_position position;
} cb_func_coord_t;

typedef struct {
	void *cb_func;
	uint8_t compare;
	float value;
} cb_func_float_t;

typedef struct {
	void *cb_func;
	uint8_t compare;
	int value;
} cb_func_int_t;

//---------------------------------
typedef struct _machine_gps_obj_t {
    mp_obj_base_t base;
    mp_obj_t uart;
    int timeout;
    bool use_crc;
    bool task_running;
    bool task_stop;
    uint32_t sent_read;
    gps_data_t gps_data;
    gps_data_t last_gps_data;
    cb_func_coord_t cb_latitude;
    cb_func_coord_t cb_longitude;
} machine_gps_obj_t;

static const char* const known_parsers[] = {
	"RMC",
	"GGA",
	"GGL",
	"GST",
	"VTG",
};

static const char* const known_talkers[] = {
	"GP",
	"GN",
	"GL",
};

extern int MainTaskCore;

static QueueHandle_t gps_mutex = NULL;

const mp_obj_type_t machine_gps_type;

//---------------------------------------------------
static float coord_to_degrees(nmea_position position)
{
	double sign = 1.0;
	if ((position.cardinal == NMEA_CARDINAL_DIR_SOUTH) || (position.cardinal == NMEA_CARDINAL_DIR_WEST)) sign = -1.0;
	return (float)(((double)position.degrees + (position.minutes / 60.0)) * sign);
}

//---------------------------------------------------
static float coord_to_radians(nmea_position position)
{
	return (coord_to_degrees(position) * M_PI / 180.0);
}

//-------------------------------------------------------------------------------
static float distance(float lat_from, float lat_to, float lon_from, float lon_to)
{
	float dLat = (lat_to - lat_from) * M_PI / 180.0;
	float dLon = (lon_to - lon_from) * M_PI / 180.0;

	float lat1 = lat_from * M_PI / 180.0;
	float lat2 = lat_to * M_PI / 180.0;

	float a = sin(dLat/2) * sin(dLat/2) + sin(dLon/2) * sin(dLon/2) * cos(lat1) * cos(lat2);
	float c = 2 * atan2(sqrt(a), sqrt(1-a));

	return (EARTH_RADIUS_KM * c);
}

//------------------------------------------
static mp_obj_t _getTime(struct tm *tm_info)
{
	mp_obj_t tuple[8] = {
		mp_obj_new_int(tm_info->tm_year + 1900),
		mp_obj_new_int(tm_info->tm_mon + 1),
		mp_obj_new_int(tm_info->tm_mday),
		mp_obj_new_int(tm_info->tm_hour),
		mp_obj_new_int(tm_info->tm_min),
		mp_obj_new_int(tm_info->tm_sec),
		mp_obj_new_int(tm_info->tm_wday + 1),
		mp_obj_new_int(tm_info->tm_yday + 1)
	};

	return mp_obj_new_tuple(8, tuple);
}

//-------------------------------
static long _currTime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec*1000) + (tv.tv_usec / 1000);
}

//--------------------------------------------------------------------------------------------
static nmea_s *get_nmea_data(uart_port_t uart_num, char *sent_type, int timeout, bool use_crc)
{
    long end_time = _currTime() + timeout;

    nmea_s *data;
    char *sentence = NULL;;

    while (_currTime() < end_time) {
		sentence = _uart_read(uart_num, timeout, "\r\n", "$G");
		if (sentence) {
			if (strstr(sentence, sent_type) == sentence) {
				data = nmea_parse((char *)sentence, strlen(sentence), use_crc);
				free(sentence);
				if (data == NULL) continue;
				return data;
			}
			else {
				// not expected sentence type
				free(sentence);
				continue;
			}
		}
    }
	return NULL; // no date received, timeout
}

//-----------------------------------------------------------------------------------------------------
static mp_obj_t nmea_data(nmea_s *data, bool settuple, gps_data_t *gps_data, gps_data_t *gps_last_data)
{
	mp_obj_t res_tuple = mp_const_none;

    if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	if (data->errors != 0) {
		if (settuple) {
			mp_obj_t tuple[2] = {
				mp_obj_new_str("ERRORS", 6),
				mp_obj_new_int(data->errors)
			};

			res_tuple = mp_obj_new_tuple(2, tuple);
		}
	    if (gps_mutex) xSemaphoreGive(gps_mutex);
		return res_tuple;
	}

	if ((gps_data) && (gps_last_data)) {
		memcpy(gps_last_data, gps_data, sizeof(gps_data_t));
	}

	if (NMEA_GGA == data->type) {
		nmea_gpgga_s *gpgga = (nmea_gpgga_s *) data;
		if (gps_data) {
			gps_data->nsat = gpgga->n_satellites;
			gps_data->quality = (uint8_t)gpgga->quality;
			if ((gpgga->n_satellites > 0) && (gpgga->quality > 0)) {
				gps_data->altitude = gpgga->altitude;
				gps_data->dop = gpgga->dop;
				gps_data->latitude = coord_to_degrees(gpgga->latitude);
				gps_data->longitude = coord_to_degrees(gpgga->longitude);
				gps_data->datetime.tm_hour = gpgga->time.tm_hour;
				gps_data->datetime.tm_min = gpgga->time.tm_min;
				gps_data->datetime.tm_sec = gpgga->time.tm_sec;
			}
		}
		if (settuple) {
			if ((gpgga->n_satellites > 0) && (gpgga->quality > 0)) {
				mp_obj_t tuple[8] = {
					mp_obj_new_str("GGA", 3),
					_getTime(&gpgga->time),
					mp_obj_new_float(coord_to_degrees(gpgga->latitude)),
					mp_obj_new_float(coord_to_degrees(gpgga->longitude)),
					mp_obj_new_float(gpgga->altitude),
					mp_obj_new_int(gpgga->n_satellites),
					mp_obj_new_int(gpgga->quality),
					mp_obj_new_float(gpgga->dop)
				};
				res_tuple = mp_obj_new_tuple(8, tuple);
			}
			else {
				mp_obj_t tuple[3] = {
					mp_obj_new_str("GGA", 3),
					mp_obj_new_int(gpgga->n_satellites),
					mp_obj_new_int(gpgga->quality)
				};
				res_tuple = mp_obj_new_tuple(3, tuple);
			}
		}
	}
	else if (NMEA_GLL == data->type) {
		nmea_gpgll_s *gpgll = (nmea_gpgll_s *) data;
		if ((gps_data) && (gpgll->valid)) {
			gps_data->latitude = coord_to_degrees(gpgll->latitude);
			gps_data->longitude = coord_to_degrees(gpgll->longitude);
			gps_data->datetime.tm_hour = gpgll->time.tm_hour;
			gps_data->datetime.tm_min = gpgll->time.tm_min;
			gps_data->datetime.tm_sec = gpgll->time.tm_sec;
		}
		if (settuple) {
			if (gpgll->valid) {
				mp_obj_t tuple[5] = {
					mp_obj_new_str("GLL", 3),
					mp_obj_new_bool(gpgll->valid),
					_getTime(&gpgll->time),
					mp_obj_new_float(coord_to_degrees(gpgll->latitude)),
					mp_obj_new_float(coord_to_degrees(gpgll->longitude))
				};
				res_tuple = mp_obj_new_tuple(5, tuple);
			}
			else {
				mp_obj_t tuple[2] = {
					mp_obj_new_str("GLL", 3),
					mp_obj_new_bool(gpgll->valid)
				};
				res_tuple = mp_obj_new_tuple(2, tuple);
			}
		}
	}
	else if (NMEA_RMC == data->type) {
		nmea_gprmc_s *gprmc = (nmea_gprmc_s *) data;
		if ((gps_data) && (gprmc->valid)) {
			gps_data->speed = gprmc->speed * 1.85200; // knots -> km/h
			gps_data->course = gprmc->course;
			gps_data->latitude =coord_to_degrees(gprmc->latitude);
			gps_data->longitude = coord_to_degrees(gprmc->longitude);
			memcpy(&gps_data->datetime, &gprmc->time, sizeof(struct tm));
		}
		if (settuple) {
			if (gprmc->valid) {
				mp_obj_t tuple[7] = {
					mp_obj_new_str("RMC", 3),
					mp_obj_new_bool(gprmc->valid),
					_getTime(&gprmc->time),
					mp_obj_new_float(coord_to_degrees(gprmc->latitude)),
					mp_obj_new_float(coord_to_degrees(gprmc->longitude)),
					mp_obj_new_float(gprmc->speed * 1.85200), // knots -> km/h
					mp_obj_new_float(gprmc->course)
				};
				res_tuple = mp_obj_new_tuple(7, tuple);
			}
			else {
				mp_obj_t tuple[2] = {
					mp_obj_new_str("RMC", 3),
					mp_obj_new_bool(gprmc->valid)
				};
				res_tuple = mp_obj_new_tuple(2, tuple);
			}
		}
	}
	else if (NMEA_VTG == data->type) {
		nmea_gpvtg_s *gpvtg = (nmea_gpvtg_s *) data;
		if (gps_data) {
			gps_data->speed = gpvtg->speed_kmh;
			gps_data->course = gpvtg->course;
		}
		if (settuple) {
			mp_obj_t tuple[4] = {
				mp_obj_new_str("VTG", 3),
				mp_obj_new_float(gpvtg->speed_kmh),
				mp_obj_new_float(gpvtg->speed_kn),
				mp_obj_new_float(gpvtg->course)
			};
			res_tuple = mp_obj_new_tuple(4, tuple);
		}
	}
	else if (NMEA_GST == data->type) {
		if (settuple) {
			nmea_gpgst_s *gpgst = (nmea_gpgst_s *) data;
			mp_obj_t tuple[9] = {
				mp_obj_new_str("GST", 3),
				_getTime(&gpgst->time),
				mp_obj_new_float(gpgst->rmssd),
				mp_obj_new_float(gpgst->sdmaj),
				mp_obj_new_float(gpgst->sdmin),
				mp_obj_new_float(gpgst->ori),
				mp_obj_new_float(gpgst->latsd),
				mp_obj_new_float(gpgst->lonsd),
				mp_obj_new_float(gpgst->altsd)
			};
			res_tuple = mp_obj_new_tuple(9, tuple);
		}
	}
    if (gps_mutex) xSemaphoreGive(gps_mutex);
    return res_tuple;
}

//------------------------------------------
static char *_get_sent_type(const char *sent)
{
    char *sent_type = calloc(8, 1);
    if (sent_type == NULL) return NULL;

    if (sent[0] != '$') snprintf(sent_type, 7, "$%s", sent);
    else snprintf(sent_type, 7, "%s", sent);

    bool f = false;
    if (strcmp(sent_type, "$G") == 0) {
    	f = true;
    }
    else {
		for (int i=0; i<sizeof(known_talkers); i++) {
			if (strstr(sent_type+1, known_talkers[i]) == (sent_type+1)) {
				f = true;
				break;
			}
		}
		if (f) {
			for (int i=0; i<sizeof(known_parsers); i++) {
				if (strstr(sent_type+1, known_parsers[i]) == (sent_type+1)) {
					f = true;
					break;
				}
			}
		}
    }
    if (f) return sent_type;
    free(sent_type);
    return NULL;
}


//============================
static void gps_task(void *pv)
{
	machine_gps_obj_t *gps_obj = (machine_gps_obj_t *)pv;
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	gps_obj->task_running = true;
	if (gps_mutex) xSemaphoreGive(gps_mutex);
    machine_uart_obj_t *uart = (machine_uart_obj_t *)gps_obj->uart;

	nmea_s *data;
	char *sentence;

	while (true) {
		if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
		if (gps_obj->task_stop) {
			gps_obj->task_stop = false;
			if (gps_mutex) xSemaphoreGive(gps_mutex);
			break;
		}
		if (gps_mutex) xSemaphoreGive(gps_mutex);
		sentence = _uart_read(uart->uart_num, 2000, "\r\n", "$G");
		if (sentence) {
			if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
			data = nmea_parse(sentence, strlen(sentence), gps_obj->use_crc);
			if (data != NULL) gps_obj->sent_read++;
			if (gps_mutex) xSemaphoreGive(gps_mutex);
			if (data != NULL) {
				// store to gps_data only
				nmea_data(data, false, &gps_obj->gps_data, &gps_obj->last_gps_data);
				nmea_free(data);
			}
			free(sentence);
			// Check callbacks
			if (gps_obj->cb_latitude.cb_func) {

			}
		}
	}

	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	gps_obj->task_running = false;
	if (gps_mutex) xSemaphoreGive(gps_mutex);

   	esp_log_level_set(GPS_TAG, CONFIG_MICRO_PY_LOG_LEVEL);
    ESP_LOGI(GPS_TAG, "GPS task ended, min free stack: %d", uxTaskGetStackHighWaterMark(NULL));

	vTaskDelete(NULL);
}

//----------------------------------------------------------
static bool _check_task(machine_gps_obj_t *self, bool start)
{
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	bool res = self->task_running;
	if (gps_mutex) xSemaphoreGive(gps_mutex);

	if ((!res) && (start)) {
		esp_log_level_set(GPS_TAG, ESP_LOG_ERROR);
		esp_log_level_set(NMEA_TAG, ESP_LOG_ERROR);
		self->sent_read = 0;
		#if CONFIG_MICROPY_USE_BOTH_CORES
    	int tres = xTaskCreate(gps_task, "gps_task", CONFIG_MICROPY_GPS_SERVICE_STACK, self, CONFIG_MICROPY_TASK_PRIORITY, NULL);
		#else
    	int tres = xTaskCreatePinnedToCore(gps_task, "gps_task", CONFIG_MICROPY_GPS_SERVICE_STACK, self, CONFIG_MICROPY_TASK_PRIORITY, NULL, MainTaskCore);
		#endif
        if (tres != pdTRUE) {
            ESP_LOGE(GPS_TAG, "Error creating GPS task");
            res = false;
        }
        else res = true;
	}
	return res;
}

/******************************************************************************/
// MicroPython bindings for GPS

//--------------------------------------------------------------------------------------------
STATIC void machine_gps_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
    bool task_running = self->task_running;
    uint32_t sent_read = self->sent_read;
	if (gps_mutex) xSemaphoreGive(gps_mutex);

    mp_printf(print, "GPS(default_timeout=%u, use_crc=%s, task_running=%s, read_sentences=%u)",
        self->timeout, self->use_crc ? "True" : "False", task_running ? "True" : "False", sent_read);
}

//--------------------------------------
static const mp_arg_t allowed_args[] = {
    { MP_QSTR_timeout,		MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_crc,			MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_service,		MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
};

enum { ARG_timeout, ARG_crc, ARG_service };

//------------------------------------------------------------------------------------------------------------------------
STATIC void machine_gps_init_helper(machine_gps_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_timeout].u_int > 0) self->timeout = args[ARG_timeout].u_int;
    if (args[ARG_crc].u_int >= 0) self->use_crc = (args[ARG_crc].u_int != 0);
    if (args[ARG_service].u_bool) {
    	_check_task(self, true);
    }
}

//-----------------------------------------------------------------------------------------------------------------
STATIC mp_obj_t machine_gps_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    machine_gps_obj_t *self = m_new_obj(machine_gps_obj_t);
    memset(self, 0, sizeof(machine_gps_obj_t));

    self->base.type = &machine_gps_type;

    if (!MP_OBJ_IS_TYPE(args[0], &machine_uart_type)) {
		mp_raise_ValueError("uart object expected as 1st argument");
    }
    self->uart = args[0];
    self->timeout = 1500;
    self->use_crc = true;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);

    mp_arg_val_t kargs[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args-1, args+1, &kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, kargs);

    machine_gps_init_helper(self, n_args - 1, args + 1, &kw_args);

    if (gps_mutex == NULL) {
		gps_mutex = xSemaphoreCreateMutex();
	}

    self->gps_data.datetime.tm_mday = 1;

    return MP_OBJ_FROM_PTR(self);
}

//--------------------------------------------------------------------------------------
STATIC mp_obj_t machine_gps_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
    machine_gps_init_helper(args[0], n_args - 1, args + 1, kw_args);
	if (gps_mutex) xSemaphoreGive(gps_mutex);

	return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_gps_init_obj, 1, machine_gps_init);

//---------------------------------------------------------------------------
STATIC mp_obj_t machine_gps_readsentence(size_t n_args, const mp_obj_t *args)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (_check_task(self, false)) {
		mp_raise_ValueError("GPS task running");
    }
    machine_uart_obj_t *uart = (machine_uart_obj_t *)self->uart;

    char *sentence = NULL;
    char *sent_type = NULL;
	int timeout = 0;
	if (n_args > 1) timeout = mp_obj_get_int(args[1]);
	if (n_args > 2) {
	    const char *sent = mp_obj_str_get_str(args[2]);
	    sent_type = _get_sent_type(sent);
	    if (sent_type == NULL) {
			mp_raise_ValueError("Invalid sentence type");
	    }
		MP_THREAD_GIL_EXIT();
		sentence = _uart_read(uart->uart_num, timeout, "\r\n", sent_type);
		MP_THREAD_GIL_ENTER();
		free(sent_type);
	}
	else {
		MP_THREAD_GIL_EXIT();
		sentence = _uart_read(uart->uart_num, timeout, "\r\n", "$G");
		MP_THREAD_GIL_ENTER();
	}


	if (sentence == NULL) return mp_obj_new_str("", 0);

	mp_obj_t res_str = mp_obj_new_str((const char *)sentence, strlen(sentence));

	if (sentence != NULL) free(sentence);
    return res_str;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_gps_readsentence_obj, 1, 3, machine_gps_readsentence);

//-------------------------------------------------------------------
STATIC mp_obj_t machine_gps_parse(mp_obj_t self_in, mp_obj_t sent_in)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);

    const char *sentence = mp_obj_str_get_str(sent_in);
	mp_obj_t res = mp_const_none;

	char *sent = strdup(sentence);
	if (sent) {
		nmea_s *data = nmea_parse(sent, strlen(sent), self->use_crc);
		if (data != NULL) {
			// store to dict only
			res = nmea_data(data, true, NULL, NULL);
			nmea_free(data);
		}
		free(sent);
	}

	return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(machine_gps_parse_obj, machine_gps_parse);

//-------------------------------------------------------------------------
STATIC mp_obj_t machine_gps_read_parse(size_t n_args, const mp_obj_t *args)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (_check_task(self, false)) {
		mp_raise_ValueError("GPS task running");
    }

    machine_uart_obj_t *uart = (machine_uart_obj_t *)self->uart;
    const char *sent = mp_obj_str_get_str(args[1]);
    char *sent_type = NULL;

    sent_type = _get_sent_type(sent);
    if (sent_type == NULL) {
		mp_raise_ValueError("Invalid sentence type");
    }

    int timeout = self->timeout;
	if (n_args > 2) {
		timeout = mp_obj_get_int(args[2]);
	}

    if (timeout < 1200) timeout = 1200;
	mp_obj_t res = mp_const_none;

	MP_THREAD_GIL_EXIT();
	nmea_s *data = get_nmea_data(uart->uart_num, sent_type, timeout, self->use_crc);
	MP_THREAD_GIL_ENTER();
	free(sent_type);

	if (data != NULL) {
		// store to dict and gps_data
		res = nmea_data(data, true, &self->gps_data, NULL);
		nmea_free(data);
	}

	return res;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_gps_read_parse_obj, 2, 3, machine_gps_read_parse);

//---------------------------------------------------
STATIC mp_obj_t machine_gps_getdata(mp_obj_t self_in)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);

	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	mp_obj_t tuple[9] = {
		_getTime(&self->gps_data.datetime),
		mp_obj_new_float(self->gps_data.latitude),
		mp_obj_new_float(self->gps_data.longitude),
		mp_obj_new_float(self->gps_data.altitude),
		mp_obj_new_int(self->gps_data.nsat),
		mp_obj_new_int(self->gps_data.quality),
		mp_obj_new_float(self->gps_data.speed),
		mp_obj_new_float(self->gps_data.course),
		mp_obj_new_float(self->gps_data.dop)
	};
    if (gps_mutex) xSemaphoreGive(gps_mutex);

    return mp_obj_new_tuple(9, tuple);;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_gps_getdata_obj, machine_gps_getdata);

//--------------------------------------------------------
STATIC mp_obj_t machine_gps_startservice(mp_obj_t self_in)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (_check_task(self, true)) return mp_const_true;
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_gps_startservice_obj, machine_gps_startservice);

//-------------------------------------------------------
STATIC mp_obj_t machine_gps_stopservice(mp_obj_t self_in)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
    if (self->task_running) {
    	self->task_stop = true;
    }
	if (gps_mutex) xSemaphoreGive(gps_mutex);
    return mp_const_true;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_gps_stopservice_obj, machine_gps_stopservice);

//-------------------------------------------------------
STATIC mp_obj_t machine_gps_taskrunning(mp_obj_t self_in)
{
    machine_gps_obj_t *self = MP_OBJ_TO_PTR(self_in);
	if (gps_mutex) xSemaphoreTake(gps_mutex, 200 / portTICK_PERIOD_MS);
	bool res = self->task_running;
	if (gps_mutex) xSemaphoreGive(gps_mutex);

	if (res) return mp_const_true;
    return mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_gps_taskrunning_obj, machine_gps_taskrunning);

//-----------------------------------------------------------------------
STATIC mp_obj_t machine_gps_distance(size_t n_args, const mp_obj_t *args)
{
	float lat1 = mp_obj_get_float(args[1]);
	float lon1 = mp_obj_get_float(args[2]);
	float lat2 = mp_obj_get_float(args[3]);
	float lon2 = mp_obj_get_float(args[4]);

	return mp_obj_new_float(distance(lat1, lat2, lon1, lon2));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_gps_distance_obj, 5, 5, machine_gps_distance);


//================================================================
STATIC const mp_rom_map_elem_t machine_gps_locals_dict_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_init),			MP_ROM_PTR(&machine_gps_init_obj) },
	{ MP_ROM_QSTR(MP_QSTR_parse),			MP_ROM_PTR(&machine_gps_parse_obj) },
	{ MP_ROM_QSTR(MP_QSTR_read),			MP_ROM_PTR(&machine_gps_readsentence_obj) },
	{ MP_ROM_QSTR(MP_QSTR_read_parse),		MP_ROM_PTR(&machine_gps_read_parse_obj) },
	{ MP_ROM_QSTR(MP_QSTR_getdata),			MP_ROM_PTR(&machine_gps_getdata_obj) },
	{ MP_ROM_QSTR(MP_QSTR_startservice),	MP_ROM_PTR(&machine_gps_startservice_obj) },
	{ MP_ROM_QSTR(MP_QSTR_stopservice),		MP_ROM_PTR(&machine_gps_stopservice_obj) },
	{ MP_ROM_QSTR(MP_QSTR_service),			MP_ROM_PTR(&machine_gps_taskrunning_obj) },
	{ MP_ROM_QSTR(MP_QSTR_distance),		MP_ROM_PTR(&machine_gps_distance_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_gps_locals_dict, machine_gps_locals_dict_table);


//======================================
const mp_obj_type_t machine_gps_type = {
    { &mp_type_type },
    .name = MP_QSTR_GPS,
    .print = machine_gps_print,
    .make_new = machine_gps_make_new,
    .locals_dict = (mp_obj_dict_t*)&machine_gps_locals_dict,
};


#endif

