/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>             // size_t
#include "input-common.h"       // sample_format, convert_sample_buffer_fun

size_t get_sample_size(sample_format format);
float get_sample_full_scale_value(sample_format format);
convert_sample_buffer_fun get_sample_converter(sample_format format);
