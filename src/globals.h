/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// global config
typedef struct {
#ifdef DEBUG
	uint32_t debug_filter;
#endif
	bool hourly, daily, utc, milliseconds;
} dumphfdl_config_t;

extern dumphfdl_config_t Config;
extern int do_exit;

// version.c
extern char const * const DUMPHFDL_VERSION;
