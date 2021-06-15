/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sys/time.h>               // struct timeval
#include "block.h"                  // struct block
#include "util.h"                   // struct octet_string
#include "metadata.h"               // struct metadata

#define SPS 10
#define HFDL_SYMBOL_RATE 1800
#define HFDL_CHANNEL_TRANSITION_BW_HZ 250

enum hfdl_pdu_direction {
	UPLINK_PDU = 0,
	DOWNLINK_PDU = 1
};

// Useful fields extracted from MPDU/SPDU header
struct hfdl_pdu_hdr_data {
	uint8_t gs_id;
	uint8_t aircraft_id;
	enum hfdl_pdu_direction direction;
	bool crc_ok;
};

struct hfdl_pdu_metadata {
	struct metadata metadata;
	struct timeval pdu_timestamp;
	char *station_id;
	int32_t version;
	int32_t freq;
	int32_t bit_rate;
	float freq_err_hz;
	char slot;              // 'S' - single slot frame, 'D' - double slot frame
};

struct hfdl_pdu_qentry {
	struct metadata *metadata;
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
