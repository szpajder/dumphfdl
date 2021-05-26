/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>         // size_t
#include "block.h"          // struct block, struct producer

#define AUTO_GAIN -100

typedef enum {
	INPUT_TYPE_UNDEF,
	INPUT_TYPE_SOAPYSDR,
	INPUT_TYPE_FILE,
	INPUT_TYPE_MAX
} input_type;

typedef enum {
	SFMT_UNDEF = 0,
	//SFMT_CU8,
	//SFMT_CS8,
	SFMT_CS16,
	SFMT_MAX
} sample_format;

struct input_cfg {
	char *device_string;
	char *gain_elements;
	float gain;
	int32_t sample_rate;
	int32_t centerfreq;
	int32_t correction;
	input_type type;
	sample_format sfmt;
};

struct input;   // forward declaration

struct input_vtable {
	int32_t (*init)(struct input *);
	void* (*rx_thread_routine)(void *);
};

typedef void (*convert_sample_buffer_fun)(struct input *, void *, size_t);

struct input {
	struct block block;
	struct input_vtable *vtable;
	struct input_cfg *config;
	convert_sample_buffer_fun convert_sample_buffer;
	size_t overflow_count;          // TODO: replace with statsd
	float full_scale;
	int32_t bytes_per_sample;
};

struct input_cfg *input_cfg_create();
struct block *input_create(struct input_cfg *cfg);
int32_t input_init(struct block *block);
