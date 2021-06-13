/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // struct hfdl_pdu_qentry
#include "util.h"                   // struct octet_string

enum mpdu_direction {
	UPLINK_MPDU = 0,
	DOWNLINK_MPDU = 2
};

la_list *mpdu_parse(struct hfdl_pdu_qentry *q, la_reasm_ctx *reasm_ctx);
