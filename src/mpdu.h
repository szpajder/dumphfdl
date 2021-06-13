/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include "hfdl.h"                   // struct hfdl_pdu_qentry
#include "util.h"                   // struct octet_string

la_proto_node *mpdu_parse(struct hfdl_pdu_qentry *q, la_reasm_ctx *reasm_ctx);
