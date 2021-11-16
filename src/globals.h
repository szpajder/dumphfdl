/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "config.h"         // DATADUMPS
#include "systable.h"
#include "ac_cache.h"
#include "ac_data.h"

enum ac_data_details {
	AC_DETAILS_NORMAL = 0,
	AC_DETAILS_VERBOSE = 1
};

// global config
struct dumphfdl_config {
#ifdef DEBUG
	uint32_t debug_filter;
#endif
	char *station_id;
	int32_t output_queue_hwm;
	enum ac_data_details ac_data_details;
	bool utc;
	bool milliseconds;
	bool output_raw_frames;
	bool output_mpdus;
	bool output_corrupted_pdus;
	bool freq_as_squawk;
	bool ac_data_available;
#ifdef DATADUMPS
	bool datadumps;
#endif
};

#define STATION_ID_LEN_MAX 255

extern struct dumphfdl_config Config;
extern int32_t do_exit;

// version.c
extern char const * const DUMPHFDL_VERSION;

extern systable *Systable;
extern pthread_mutex_t Systable_lock;
#define Systable_lock() do { pthread_mutex_lock(&Systable_lock); } while(0)
#define Systable_unlock() do { pthread_mutex_unlock(&Systable_lock); } while(0)

extern ac_cache *AC_cache;
extern pthread_mutex_t AC_cache_lock;
#define AC_cache_lock() do { pthread_mutex_lock(&AC_cache_lock); } while(0)
#define AC_cache_unlock() do { pthread_mutex_unlock(&AC_cache_lock); } while(0)

extern ac_data *AC_data;
