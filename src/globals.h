/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "systable.h"

// global config
struct dumphfdl_config {
#ifdef DEBUG
	uint32_t debug_filter;
#endif
	int32_t output_queue_hwm;
	bool utc;
	bool milliseconds;
	bool output_raw_frames;
};

extern struct dumphfdl_config Config;
extern int32_t do_exit;

// version.c
extern char const * const DUMPHFDL_VERSION;

extern systable *Systable;
extern pthread_mutex_t Systable_lock;
#define Systable_lock() do { pthread_mutex_lock(&Systable_lock); } while(0)
#define Systable_unlock() do { pthread_mutex_unlock(&Systable_lock); } while(0)
