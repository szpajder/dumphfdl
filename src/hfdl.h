/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "block.h"          // struct block

#define SPS 10
#define HFDL_SYMBOL_RATE 1800
#define HFDL_CHANNEL_TRANSITION_BW_HZ 250

void hfdl_init_globals();
struct block *hfdl_channel_create(int sample_rate, int pre_decimation_rate,
		float transition_bw, int centerfreq, int frequency);
void hfdl_print_summary();
