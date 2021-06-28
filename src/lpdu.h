/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <sys/time.h>                   // struct timeval
#include <stdint.h>
#include <libacars/libacars.h>          // la_proto_node
#include <libacars/libacars.h>          // la_type_descriptor, la_proto_node
#include "hfdl.h"                       // struct hfdl_pdu_hdr_data

la_proto_node *lpdu_parse(uint8_t *buf, uint32_t len, struct hfdl_pdu_hdr_data mpdu_header,
		la_reasm_ctx *reasm_ctx, struct timeval rx_timestamp);
