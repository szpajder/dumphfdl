/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <limits.h>             // SHRT_MAX, SCHAR_MAX, UCHAR_MAX
#include <complex.h>            // CMPLXF
#include <strings.h>            // strcasecmp()
#include <pthread.h>            // pthread_*
#include <liquid/liquid.h>      // cbuffercf_*
#include "input-common.h"       // struct input
#include "util.h"               // ASSERT, debug_print

static void convert_cf32(struct input *input, void *inbuf, size_t len,
		float complex *outbuf) {
	if(UNLIKELY(len % input->bytes_per_sample != 0)) {
		debug_print(D_SDR, "Warning: buf len %zu is not a multiple of %d, truncating\n",
				len, input->bytes_per_sample);
		len -= (len % input->bytes_per_sample);
	}
	if(UNLIKELY(len == 0)) {
		return;
	}
	float *floatbuf = inbuf;
	size_t floatbuf_len = len / sizeof(float);
	size_t outbuf_idx = 0;
	float re = 0.f, im = 0.f;
	float const full_scale = input->full_scale;
	ASSERT(full_scale > 0.f);
	for(size_t i = 0; i < floatbuf_len;) {
		re = floatbuf[i++] / full_scale;
		im = floatbuf[i++] / full_scale;
		outbuf[outbuf_idx++] = CMPLXF(re, im);
	}
}

static void convert_cs16(struct input *input, void *inbuf, size_t len,
		float complex *outbuf) {
	if(UNLIKELY(len % input->bytes_per_sample != 0)) {
		debug_print(D_SDR, "Warning: buf len %zu is not a multiple of %d, truncating\n",
				len, input->bytes_per_sample);
		len -= (len % input->bytes_per_sample);
	}
	if(UNLIKELY(len == 0)) {
		return;
	}
	int16_t *shortbuf = inbuf;
	size_t shortbuf_len = len / sizeof(int16_t);
	size_t outbuf_idx = 0;
	float re = 0.f, im = 0.f;
	float const full_scale = input->full_scale;
	ASSERT(full_scale > 0.f);
	for(size_t i = 0; i < shortbuf_len;) {
		re = (float)shortbuf[i++] / full_scale;
		im = (float)shortbuf[i++] / full_scale;
		outbuf[outbuf_idx++] = CMPLXF(re, im);
	}
}

static void convert_cu8(struct input *input, void *inbuf, size_t len,
		float complex *outbuf) {
	if(UNLIKELY(len % input->bytes_per_sample != 0)) {
		debug_print(D_SDR, "Warning: buf len %zu is not a multiple of %d, truncating\n",
				len, input->bytes_per_sample);
		len -= (len % input->bytes_per_sample);
	}
	if(UNLIKELY(len == 0)) {
		return;
	}
	uint8_t *bytebuf = inbuf;
	size_t bytebuf_len = len / sizeof(uint8_t);
	size_t outbuf_idx = 0;
	float re = 0.f, im = 0.f;
	float const full_scale = input->full_scale;
	ASSERT(full_scale > 0.f);
	float const shift = input->full_scale / 2.0f;
	for(size_t i = 0; i < bytebuf_len;) {
		re = (bytebuf[i++] - shift) / full_scale;
		im = (bytebuf[i++] - shift) / full_scale;
		outbuf[outbuf_idx++] = CMPLXF(re, im);
	}
}

void complex_samples_produce(struct circ_buffer *circ_buffer,
		float complex *samples, size_t num_samples) {
	pthread_mutex_lock(circ_buffer->mutex);
	size_t cbuf_available = cbuffercf_space_available(circ_buffer->buf);
	if(cbuf_available < num_samples) {
		debug_print(D_SDR, "circ_buffer overrun (need %zu, has %zu free space, %zu samples lost)\n",
				num_samples, cbuf_available, num_samples - cbuf_available);
		num_samples = cbuf_available;
	}
	cbuffercf_write(circ_buffer->buf, samples, num_samples);
	pthread_mutex_unlock(circ_buffer->mutex);
	pthread_cond_signal(circ_buffer->cond);
}

struct sample_format_params {
	char const *name;
	size_t sample_size;                         // octets per complex sample
	float full_scale;                           // max raw sample value
	convert_sample_buffer_fun convert_fun;      // sample conversion routine
};

static struct sample_format_params const sample_format_params[] = {
	[SFMT_UNDEF] = {
		.name = "",
		.sample_size = 0,
		.full_scale = 0.f,
		.convert_fun = NULL
	},
	[SFMT_CU8] = {
		.name = "CU8",
		.sample_size = 2 * sizeof(uint8_t),
		.full_scale = (float)SCHAR_MAX,
		.convert_fun = convert_cu8
	},
	[SFMT_CS16] = {
		.name = "CS16",
		.sample_size = 2 * sizeof(int16_t),
		.full_scale = (float)SHRT_MAX + 0.5f,
		.convert_fun = convert_cs16
	},
	[SFMT_CF32] = {
		.name = "CF32",
		.sample_size = 2 * sizeof(float),
		.full_scale = 1.0f,
		.convert_fun = convert_cf32
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

sample_format sample_format_from_string(char const *str) {
	if(str == NULL) {
		return SFMT_UNDEF;
	}
	for(int32_t i = 0; i < SFMT_MAX; i++) {
		if(strcasecmp(sample_format_params[i].name, str) == 0) {
			return i;
		}
	}
	return SFMT_UNDEF;
}
