/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "block.h"          // struct block

#define SPS 10
#define HFDL_SYMBOL_RATE 1800
#define HFDL_CHANNEL_TRANSITION_BW_HZ 250

void hfdl_init_globals(void);
struct block *hfdl_channel_create(int32_t sample_rate, int32_t pre_decimation_rate,
		float transition_bw, int32_t centerfreq, int32_t frequency);

void hfdl_mpdu_decoder_init(void);
int32_t hfdl_mpdu_decoder_start(void);
void hfdl_mpdu_decoder_stop(void);
bool hfdl_mpdu_decoder_is_running(void);

void hfdl_print_summary(void);
