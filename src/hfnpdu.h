/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <sys/time.h>                   // struct timeval
#include <libacars/libacars.h>          // la_proto_node
#include <libacars/reassembly.h>        // la_reasm_ctx
#include "hfdl.h"                       // enum hfdl_pdu_direction

la_proto_node *hfnpdu_parse(uint8_t *buf, uint32_t len, enum hfdl_pdu_direction direction,
		la_reasm_ctx *reasm_ctx, struct timeval rx_timestamp);
