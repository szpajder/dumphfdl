/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "block.h"                  // struct block
#include "util.h"                   // struct octet_string
#include "output-common.h"          // struct hfdl_msg_metadata

#define SPS 10
#define HFDL_SYMBOL_RATE 1800
#define HFDL_CHANNEL_TRANSITION_BW_HZ 250

struct hfdl_pdu_qentry {
	struct hfdl_msg_metadata *metadata;
	struct octet_string *pdu;
	uint32_t flags;
};

void hfdl_init_globals(void);
struct block *hfdl_channel_create(int32_t sample_rate, int32_t pre_decimation_rate,
		float transition_bw, int32_t centerfreq, int32_t frequency);

void hfdl_pdu_decoder_init(void);
int32_t hfdl_pdu_decoder_start(void *ctx);
void hfdl_pdu_decoder_stop(void);
bool hfdl_pdu_decoder_is_running(void);

void hfdl_print_summary(void);
