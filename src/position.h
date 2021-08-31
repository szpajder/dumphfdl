/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>                   // struct tm
#include <libacars/libacars.h>      // la_proto_node
#include "util.h"                   // struct location

struct ac_info {
	char *flight_id;
	uint32_t icao_address;
	int32_t freq;
	uint8_t ac_id;
	bool flight_id_present;
	bool icao_address_present;
	bool freq_present;
	bool ac_id_present;
};

struct timestamp {
	time_t t;
	struct tm tm;
	// Presence flags for struct tm fields
	bool tm_sec_present;
	bool tm_min_present;
	bool tm_hour_present;
	bool tm_mday_present;
	bool tm_mon_present;
	bool tm_year_present;
};

struct position {
	struct timestamp timestamp;
	struct location location;
};

struct position_info {
	struct ac_info aircraft;
	struct position position;
};

struct position_info *position_info_create(void);
struct position_info *position_info_extract(la_proto_node *tree);
void position_info_destroy(struct position_info *pos_info);
