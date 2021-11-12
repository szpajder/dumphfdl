/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>         // usleep
#include <errno.h>          // errno
#include <liquid/liquid.h>  // cbuffercf_*
#include "block.h"          // block_*
#include "input-common.h"   // input, sample_format, input_vtable
#include "input-helpers.h"  // get_sample_full_scale_value, get_sample_size
#include "util.h"	        // debug_print, ASSERT, XCALLOC
#include "globals.h"        // do_exit

#define FILE_BUFSIZE 320000U

struct file_input {
	struct input input;
	FILE *fh;
};

struct input *file_input_create(struct input_cfg *cfg) {
	UNUSED(cfg);
	NEW(struct file_input, file_input);
	return &file_input->input;
}

void file_input_destroy(struct input *input) {
	if(input != NULL) {
		struct file_input *fi = container_of(input, struct file_input, input);
		XFREE(fi);
	}
}

void *file_input_thread(void *ctx) {
	ASSERT(ctx);
	struct block *block = ctx;
	struct input *input = container_of(block, struct input, block);
	struct file_input *file_input = container_of(input, struct file_input, input);
	struct circ_buffer *circ_buffer = &block->producer.out->circ_buffer;

	ASSERT(file_input->fh != NULL);

	void *inbuf = XCALLOC(FILE_BUFSIZE, sizeof(uint8_t));
	float complex *outbuf = XCALLOC(FILE_BUFSIZE / input->bytes_per_sample,
			sizeof(float complex));
	size_t space_available, len, samples_read;
	do {
		len = fread(inbuf, 1, FILE_BUFSIZE, file_input->fh);
		samples_read = len / input->bytes_per_sample;
		while(true) {
			pthread_mutex_lock(circ_buffer->mutex);
			space_available = cbuffercf_space_available(circ_buffer->buf);
			pthread_mutex_unlock(circ_buffer->mutex);
			if(space_available * input->bytes_per_sample >= len) {
				break;
			}
			usleep(100000);
		}
		input->convert_sample_buffer(input, inbuf, len, outbuf);
		complex_samples_produce(circ_buffer, outbuf, samples_read);
	} while(len == FILE_BUFSIZE && do_exit == 0);
	fclose(file_input->fh);
	file_input->fh = NULL;
	debug_print(D_MISC, "Shutdown ordered, signaling consumer shutdown\n");
	block_connection_one2one_shutdown(block->producer.out);
	do_exit = 1;
	block->running = false;
	XFREE(inbuf);
	XFREE(outbuf);
	return NULL;
}

int32_t file_input_init(struct input *input) {
	ASSERT(input != NULL);
	struct file_input *file_input = container_of(input, struct file_input, input);

	if(input->config->sfmt == SFMT_UNDEF) {
		fprintf(stderr, "Sample format must be specified for file inputs\n");
		return -1;
	}
	if(strcmp(input->config->device_string, "-") == 0) {
		file_input->fh = stdin;
	} else {
		file_input->fh = fopen(input->config->device_string, "rb");
	}
	if(file_input->fh == NULL) {
		fprintf(stderr, "Failed to open input file %s: %s\n",
				input->config->device_string, strerror(errno));
		return -1;
	}

	input->full_scale = get_sample_full_scale_value(input->config->sfmt);
	input->bytes_per_sample = get_sample_size(input->config->sfmt);
	ASSERT(input->bytes_per_sample > 0);
	input->block.producer.max_tu = FILE_BUFSIZE / input->bytes_per_sample;
	debug_print(D_SDR, "%s: max_tu=%zu\n",
			input->config->device_string, input->block.producer.max_tu);
	return 0;
}

struct input_vtable const file_input_vtable = {
	.create = file_input_create,
	.init = file_input_init,
	.destroy = file_input_destroy,
	.rx_thread_routine = file_input_thread
};

