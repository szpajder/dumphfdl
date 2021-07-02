#include <stdint.h>
#include <stdbool.h>
#include <string.h>                 // strdup
#include <libconfig.h>              // config_*
#include <libacars/dict.h>          // la_dict_*
#include "systable.h"
#include "util.h"                   // NEW, XFREE, struct location, parse_coordinate

enum systable_err_code {
	ST_ERR_OK = 0,
	ST_ERR_LIBCONFIG,
	ST_ERR_VERSION_MISSING,
	ST_ERR_VERSION_OUT_OF_RANGE,
	ST_ERR_STATIONS_MISSING,
	ST_ERR_STATION_WRONG_TYPE,
	ST_ERR_STATION_ID_MISSING,
	ST_ERR_STATION_ID_OUT_OF_RANGE,
	ST_ERR_STATION_ID_DUPLICATE,
	ST_ERR_STATION_NAME_WRONG_TYPE,
	ST_ERR_FREQUENCIES_MISSING,
	ST_ERR_FREQUENCY_WRONG_TYPE,
	ST_ERR_MAX
};

static la_dict const systable_error_messages[] = {
	{
		.id = ST_ERR_OK,
		.val = "no error"
	},
	{
		.id = ST_ERR_VERSION_MISSING,
		.val = "version missing or wrong type (must be an integer)"
	},
	{
		.id = ST_ERR_VERSION_OUT_OF_RANGE,
		.val = "version out of range"
	},
	{
		.id = ST_ERR_STATIONS_MISSING,
		.val = "stations missing or wrong type (must be a list)"
	},
	{
		.id = ST_ERR_STATION_WRONG_TYPE,
		.val = "station setting has wrong type (must be a group)"
	},
	{
		.id = ST_ERR_STATION_ID_MISSING,
		.val = "station id missing or wrong type (must be an integer)"
	},
	{
		.id = ST_ERR_STATION_ID_OUT_OF_RANGE,
		.val = "station id out of range"
	},
	{
		.id = ST_ERR_STATION_ID_DUPLICATE,
		.val = "duplicate station id"
	},
	{
		.id = ST_ERR_STATION_NAME_WRONG_TYPE,
		.val = "name setting has wrong type (must be a string)"
	},
	{
		.id = ST_ERR_FREQUENCIES_MISSING,
		.val = "frequencies missing or wrong type (must be a list)"
	},
	{
		.id = ST_ERR_FREQUENCY_WRONG_TYPE,
		.val = "frequency setting has wrong type (must be a number)"
	},
	{
		.id = 0,
		.val = NULL
	}
};

#define STATION_ID_MAX 127

struct _systable {
	config_t cfg;                                           // system table as a libconfig object
	char *savefile_path;                                    // where to save updated table
	config_setting_t const *stations[STATION_ID_MAX+1];     // quick access to GS params (indexed by GS ID)
	enum systable_err_code err;                             // error code of last operation
	bool available;                                         // is this system table ready to use
};

struct systable {
	struct _systable *current, *new;
};

/******************************
 * Forward declarations
*******************************/

static bool systable_parse(systable *st);

/******************************
 * Public methods
*******************************/

systable *systable_create(char const *savefile) {
	NEW(systable, st);
	st->current = XCALLOC(1, sizeof(struct _systable));
	st->new = XCALLOC(1, sizeof(struct _systable));
	config_init(&st->current->cfg);
	if(savefile != NULL) {
		st->current->savefile_path = strdup(savefile);
	}
	return st;
}

bool systable_read_from_file(systable *st, char const *file) {
	if(st == NULL) {
		return false;
	}
	ASSERT(file);
	if(config_read_file(&st->current->cfg, file) != CONFIG_TRUE) {
		st->current->err = ST_ERR_LIBCONFIG;
		return false;
	}
	st->current->available = systable_parse(st);
	return st->current->available;
}

char const *systable_error_text(systable const *st) {
	if(st == NULL) {
		return NULL;
	}
	ASSERT(st->current->err < ST_ERR_MAX);
	if(st->current->err == ST_ERR_LIBCONFIG) {
		return config_error_text(&st->current->cfg);
	}
	return la_dict_search(systable_error_messages, st->current->err);
}

int32_t systable_get_version(systable const *st) {
	int32_t version = -1;       // invalid default value
	if(st != NULL) {
		config_lookup_int(&st->current->cfg, "version", &version);
	}
	return version;
}

char const *systable_get_station_name(systable const *st, int32_t id) {
	char const *name = NULL;
	if(st != NULL && id >= 0 && id < STATION_ID_MAX && st->current->stations[id] != NULL) {
		config_setting_lookup_string(st->current->stations[id], "name", &name);
	}
	return name;
}

double systable_get_station_frequency(systable const *st, int32_t gs_id, int32_t freq_id) {
	if(st != NULL && gs_id >= 0 && gs_id < STATION_ID_MAX && st->current->stations[gs_id] != NULL) {
		config_setting_t *frequencies = config_setting_get_member(st->current->stations[gs_id], "frequencies");
		config_setting_t *freq = config_setting_get_elem(frequencies, freq_id);
		if(freq != NULL) {
			int type = config_setting_type(freq);
			if(type == CONFIG_TYPE_FLOAT) {
				return config_setting_get_float(freq);
			} else if(type == CONFIG_TYPE_INT) {
				return (double)config_setting_get_int(freq);
			}
		}
	}
	return -1.0;
}

bool systable_is_available(systable const *st) {
	return st != NULL && st->current->available;
}

void systable_destroy(systable *st) {
	if(st == NULL) {
		config_destroy(&st->current->cfg);
		XFREE(st->current->savefile_path);
		XFREE(st);
	}
}

void systable_store_pdu(systable const *st, uint8_t seq_num, uint8_t pdu_seq_len, uint8_t *buf, uint32_t len);
la_proto_node *systable_decode_from_pdu_sequence(systable const *st);


/****************************************
 * Private methods
*****************************************/

#define parse_success() do { return true; } while(0)
#define parse_error(code) do { st->current->err = (code); return false; } while(0)

static bool systable_parse_version(systable *st);
static bool systable_parse_stations(systable *st);
static bool systable_parse_station(systable *st, config_setting_t const *station);
static bool systable_parse_station_id(systable *st, config_setting_t const *station);
static bool systable_parse_station_name(systable *st, config_setting_t const *station);
static bool systable_parse_frequencies(systable *st, config_setting_t const *station);
static bool systable_add_station(systable *st, config_setting_t const *station);

static bool systable_parse(systable *st) {
	ASSERT(st);

	bool result = true;
	result &= systable_parse_version(st);
	result &= systable_parse_stations(st);
	return result;
}

static bool systable_parse_version(systable *st) {
#define SYSTABLE_VERSION_MAX 4095
	ASSERT(st);

	int32_t version = 0;
	if(config_lookup_int(&st->current->cfg, "version", &version) == CONFIG_FALSE) {
		parse_error(ST_ERR_VERSION_MISSING);
	}
	if(version < 0 || version > SYSTABLE_VERSION_MAX) {
		parse_error(ST_ERR_VERSION_OUT_OF_RANGE);
	}
	parse_success();
}

static bool systable_parse_stations(systable *st) {
	ASSERT(st);

	config_setting_t *stations = config_lookup(&st->current->cfg, "stations");
	if(stations == NULL || !config_setting_is_list(stations)) {
		parse_error(ST_ERR_STATIONS_MISSING);
	}
	config_setting_t *station = NULL;
	uint32_t idx = 0;
	while((station = config_setting_get_elem(stations, idx)) != NULL) {
		if(systable_parse_station(st, station) == false ||
				systable_add_station(st, station) == false) {
			return false;
		}
		idx++;
	}
	parse_success();
}

static bool systable_parse_station(systable *st, config_setting_t const *station) {
	ASSERT(st);
	ASSERT(station);

	if(!config_setting_is_group(station)) {
		parse_error(ST_ERR_STATION_WRONG_TYPE);
	}
	bool result = true;
	result &= systable_parse_station_id(st, station);
	result &= systable_parse_station_name(st, station);
	result &= systable_parse_frequencies(st, station);
	return result;
}

static bool systable_parse_station_id(systable *st, config_setting_t const *station) {
	ASSERT(st);
	ASSERT(station);

	int32_t id = 0;
	if(config_setting_lookup_int(station, "id", &id) == CONFIG_FALSE) {
		parse_error(ST_ERR_STATION_ID_MISSING);
	}
	if(id < 0 || id > STATION_ID_MAX) {
		parse_error(ST_ERR_STATION_ID_OUT_OF_RANGE);
	}
	parse_success();
}

static bool systable_parse_frequencies(systable *st, config_setting_t const *station) {
	ASSERT(st);
	ASSERT(station);

	config_setting_t *frequencies = config_setting_get_member(station, "frequencies");
	if(frequencies == NULL || !config_setting_is_list(frequencies)) {
		parse_error(ST_ERR_FREQUENCIES_MISSING);
	}
	config_setting_t *frequency = NULL;
	uint32_t idx = 0;
	while((frequency = config_setting_get_elem(frequencies, idx)) != NULL) {
		if(config_setting_is_number(frequency) == false) {
			parse_error(ST_ERR_FREQUENCY_WRONG_TYPE);
		}
		idx++;
	}
	parse_success();
}

static bool systable_parse_station_name(systable *st, config_setting_t const *station) {
	ASSERT(st);
	ASSERT(station);

	config_setting_t *name = config_setting_get_member(station, "name");
	if(name == NULL) {
		// this setting is optional
		parse_success();
	}
	if(config_setting_type(name) != CONFIG_TYPE_STRING) {
		parse_error(ST_ERR_STATION_NAME_WRONG_TYPE);
	}
	parse_success();
}

static bool systable_add_station(systable *st, config_setting_t const *station) {
	ASSERT(st);
	ASSERT(station);

	int32_t id = 0;
	// error checking has been done in systable_parse_station()
	config_setting_lookup_int(station, "id", &id);
	if(st->current->stations[id] != NULL) {
		parse_error(ST_ERR_STATION_ID_DUPLICATE);
	}
	st->current->stations[id] = station;
	parse_success();
}

