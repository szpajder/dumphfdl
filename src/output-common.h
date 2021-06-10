/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>

struct hfdl_msg_metadata {
	int32_t version;
	uint32_t freq;
};

// output queue entry flags
#define OUT_FLAG_ORDERED_SHUTDOWN (1 << 0)

