/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <pthread.h>                // pthread_mutex_t
#include "globals.h"                // dumphfdl_config
#include "systable.h"

int32_t do_exit = 0;
struct dumphfdl_config Config = {0};

// Global system table
systable *Systable;
pthread_mutex_t Systable_lock = PTHREAD_MUTEX_INITIALIZER;
