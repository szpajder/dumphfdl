/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "output-common.h"          // output_descriptor_t

// Maximum allowed length of a binary-serialized frame (including length field)
#define OUT_BINARY_FRAME_LEN_MAX       65536
// Size of the length field preceding binary-serialized frame
#define OUT_BINARY_FRAME_LEN_OCTETS    2

extern output_descriptor_t out_DEF_file;
