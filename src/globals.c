/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <pthread.h>                // pthread_mutex_t
#include "globals.h"                // dumphfdl_config
#include "systable.h"               // systable
#include "ac_cache.h"               // ac_cache
#include "ac_data.h"                // ac_data

int32_t do_exit = 0;
struct dumphfdl_config Config = {0};

// Global system table
systable *Systable;
pthread_mutex_t Systable_lock = PTHREAD_MUTEX_INITIALIZER;

// HFDL ID -> ICAO mapping table
ac_cache *AC_cache;
pthread_mutex_t AC_cache_lock = PTHREAD_MUTEX_INITIALIZER;

// basestation.sqb cache
ac_data *AC_data;
