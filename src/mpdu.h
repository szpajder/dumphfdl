/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "output-common.h"      // struct hfdl_msg_metadata
#include "util.h"               // struct octet_string

struct hfdl_mpdu_qentry {
	struct hfdl_msg_metadata *metadata;
	struct octet_string *frame;
	uint32_t flags;
};

