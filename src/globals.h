/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// global config
struct dumphfdl_config {
#ifdef DEBUG
	uint32_t debug_filter;
#endif
	bool hourly, daily, utc, milliseconds;
};

extern struct dumphfdl_config Config;
extern int32_t do_exit;

// version.c
extern char const * const DUMPHFDL_VERSION;
