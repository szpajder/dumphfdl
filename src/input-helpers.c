/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>              // fprintf
#include <limits.h>             // SHRT_MAX, SCHAR_MAX, UCHAR_MAX
#include <complex.h>            // CMPLXF
#include <pthread.h>            // pthread_*
#include <liquid/liquid.h>      // cbuffercf_*
#include "input-common.h"       // struct input
#include "util.h"               // ASSERT, debug_print

static void convert_cs16(struct input *input, void *buf, size_t len) {
	ASSERT(input);
	ASSERT(buf);
	if(UNLIKELY(len % input->bytes_per_sample != 0)) {
		debug_print(D_SDR, "Warning: buf len %zu is not a multiple of %d, truncating\n",
				len, input->bytes_per_sample);
		len -= (len % input->bytes_per_sample);
	}
	if(UNLIKELY(len == 0)) {
		return;
	}
	int16_t *bbuf = (int16_t *)buf;
	size_t blen = len / sizeof(int16_t);
	size_t complex_sample_cnt = len / input->bytes_per_sample;
	float complex csbuf[complex_sample_cnt];
	size_t csbuf_idx = 0;
	float re = 0.f, im = 0.f;
	float const full_scale = input->full_scale;
	ASSERT(full_scale > 0.f);
	for(size_t i = 0; i < blen;) {
		re = (float)bbuf[i++] / full_scale;
		im = (float)bbuf[i++] / full_scale;
		csbuf[csbuf_idx++] = CMPLXF(re, im);
	}
	// TODO: conversion function shall not deal with producing output
	struct circ_buffer *circ_buffer = &input->block.producer.out->circ_buffer;
	pthread_mutex_lock(circ_buffer->mutex);
	size_t cbuf_available = cbuffercf_space_available(circ_buffer->buf);
	if(cbuf_available < complex_sample_cnt) {
		fprintf(stderr, "circ_buffer overrun (need %zu, has %zu free space, %zu samples lost)\n",
				complex_sample_cnt, cbuf_available, complex_sample_cnt - cbuf_available);
		complex_sample_cnt = cbuf_available;
	}
	cbuffercf_write(circ_buffer->buf, csbuf, complex_sample_cnt);
	pthread_mutex_unlock(circ_buffer->mutex);
	pthread_cond_signal(circ_buffer->cond);
}

struct sample_format_params {
	size_t sample_size;                         // octets per complex sample
	float full_scale;                           // max raw sample value
	convert_sample_buffer_fun convert_fun;      // sample conversion routine
};

static struct sample_format_params const sample_format_params[] = {
	[SFMT_CS16] = {
		.sample_size = 2 * sizeof(int16_t),
		.full_scale = (float)SHRT_MAX + 0.5f,
		.convert_fun = convert_cs16
	}
};

size_t get_sample_size(sample_format format) {
	if(format < SFMT_MAX) {
		return sample_format_params[format].sample_size;
	}
	return 0;
}

float get_sample_full_scale_value(sample_format format) {
	if(format < SFMT_MAX) {
		return sample_format_params[format].full_scale;
	}
	return 0.f;
}

convert_sample_buffer_fun get_sample_converter(sample_format format) {
	return format < SFMT_MAX ? sample_format_params[format].convert_fun : NULL;
}

