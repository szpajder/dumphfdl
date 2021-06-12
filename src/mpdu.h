/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include "output-common.h"          // struct hfdl_msg_metadata
#include "util.h"                   // struct octet_string

struct hfdl_mpdu_qentry {
	struct hfdl_msg_metadata *metadata;
	struct octet_string *pdu;
	uint32_t flags;
};

la_proto_node *mpdu_parse(struct hfdl_mpdu_qentry *q, la_reasm_ctx *reasm_ctx);
