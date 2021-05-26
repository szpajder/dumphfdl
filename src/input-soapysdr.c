/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>              // fprintf()
#include <stdint.h>
#include <stdlib.h>             // atof()
#include <string.h>             // strcmp()
#include <unistd.h>             // usleep()
#include <SoapySDR/Version.h>   // SOAPY_SDR_API_VERSION
#include <SoapySDR/Types.h>     // SoapySDRKwargs_*
#include <SoapySDR/Device.h>    // SoapySDRStream, SoapySDRDevice_*
#include <SoapySDR/Formats.h>   // SOAPY_SDR_CS16, SoapySDR_formatToSize()
#include <liquid/liquid.h>      // cbuffercf_create
#include "globals.h"            // do_exit
#include "block.h"              // block_*
#include "input-common.h"       // input, sample_format, input_vtable
#include "input-helpers.h"      // get_sample_full_scale_value, get_sample_size
#include "util.h"               // XCALLOC, XFREE, container_of

struct soapysdr_input {
	struct input input;
	SoapySDRDevice *sdr;
	SoapySDRStream *stream;
};

static void soapysdr_verbose_device_search() {
	size_t length;
	// enumerate devices
	SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
	for(size_t i = 0; i < length; i++) {
		fprintf(stderr, "Found device #%d:\n", (int)i);
		for(size_t j = 0; j < results[i].size; j++) {
			fprintf(stderr, "  %s = %s\n", results[i].keys[j], results[i].vals[j]);
		}
	}
	SoapySDRKwargsList_clear(results, length);
}

int soapysdr_input_init(struct input *input) {
	ASSERT(input != NULL);
	struct soapysdr_input *soapysdr_input = container_of(input, struct soapysdr_input, input);
	soapysdr_verbose_device_search();

	struct input_cfg *cfg = input->config;
	SoapySDRDevice *sdr = SoapySDRDevice_makeStrArgs(cfg->device_string);
	if(sdr == NULL) {
		fprintf(stderr, "%s: could not open SoapySDR device: %s\n", cfg->device_string, SoapySDRDevice_lastError());
		return -1;
	}
	if(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, cfg->sample_rate) != 0) {
		fprintf(stderr, "%s: setSampleRate failed: %s\n", cfg->device_string, SoapySDRDevice_lastError());
		return -1;
	}
	if(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, cfg->centerfreq, NULL) != 0) {
		fprintf(stderr, "%s: setFrequency failed: %s\n", cfg->device_string, SoapySDRDevice_lastError());
		return -1;
	}
	if(SoapySDRDevice_setFrequencyCorrection(sdr, SOAPY_SDR_RX, 0, cfg->correction) != 0) {
		fprintf(stderr, "%s: setFrequencyCorrection failed: %s\n", cfg->device_string, SoapySDRDevice_lastError());
		return -1;
	}
	if(SoapySDRDevice_hasDCOffsetMode(sdr, SOAPY_SDR_RX, 0)) {
		if(SoapySDRDevice_setDCOffsetMode(sdr, SOAPY_SDR_RX, 0, true) != 0) {
			fprintf(stderr, "%s: setDCOffsetMode failed: %s\n", cfg->device_string, SoapySDRDevice_lastError());
			return -1;
		}
	}

	// If both --gain and --soapy-gain are present, the latter takes precedence.
	if(cfg->gain_elements != NULL) {
		SoapySDRKwargs gains = SoapySDRKwargs_fromString(cfg->gain_elements);
		if(gains.size < 1) {
			fprintf(stderr, "Unable to parse gains string, "
					"must be a sequence of 'name1=value1,name2=value2,...'.\n");
			return -1;
		}
		for(size_t i = 0; i < gains.size; i++) {
			SoapySDRDevice_setGainElement(sdr, SOAPY_SDR_RX, 0, gains.keys[i], atof(gains.vals[i]));
			//debug_print(D_SDR, "Set gain %s to %.2f\n", gains.keys[i], atof(gains.vals[i]));
			double gain_value = SoapySDRDevice_getGainElement(sdr, SOAPY_SDR_RX, 0, gains.keys[i]);
			fprintf(stderr, "Gain element %s set to %.2f dB\n", gains.keys[i], gain_value);

		}
		SoapySDRKwargs_clear(&gains);
	} else {
		if(cfg->gain == AUTO_GAIN) {
			if(SoapySDRDevice_hasGainMode(sdr, SOAPY_SDR_RX, 0) == false) {
				fprintf(stderr, "%s: device does not support auto gain. "
						"Please specify gain manually.\n", cfg->device_string);
				return -1;
			}
			if(SoapySDRDevice_setGainMode(sdr, SOAPY_SDR_RX, 0, true) != 0) {
				fprintf(stderr, "%s: could not enable auto gain: %s\n", cfg->device_string, SoapySDRDevice_lastError());
				return -1;
			}
			fprintf(stderr, "%s: auto gain enabled\n", cfg->device_string);
		} else {
			if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, cfg->gain) != 0) {
				fprintf(stderr, "Could not set gain: %s\n", SoapySDRDevice_lastError());
				return -1;
			}
			fprintf(stderr, "%s: gain set to %.2f dB\n", cfg->device_string, cfg->gain);
		}
	}
// TODO: re-enable this when device-agnostic configuration framework is implemented.
/*
	if(antenna != NULL) {
		if(SoapySDRDevice_setAntenna(sdr, SOAPY_SDR_RX, 0, antenna) != 0) {
			fprintf(stderr, "Could not select antenna %s: %s\n", antenna, SoapySDRDevice_lastError());
			return -1;
		}
		XFREE(antenna);
	}
	fprintf(stderr, "Antenna: %s\n", SoapySDRDevice_getAntenna(sdr, SOAPY_SDR_RX, 0));

	if(settings != NULL) {
		SoapySDRKwargs settings_param = SoapySDRKwargs_fromString(settings);
		if(settings_param.size < 1) {
			fprintf(stderr, "Unable to parse settings string, must be a sequence of 'name1=value1,name2=value2,...'.\n");
			return -1;
		}
		for(size_t i = 0; i < settings_param.size; i++) {
			SoapySDRDevice_writeSetting(sdr, settings_param.keys[i], settings_param.vals[i]);
			//debug_print(D_SDR, "Set param %s to %s\n", settings_param.keys[i], settings_param.vals[i]);
			char *setting_value = SoapySDRDevice_readSetting(sdr, settings_param.keys[i]);
			fprintf(stderr, "Setting %s is %s => %s\n", settings_param.keys[i], setting_value,
					(strcmp(settings_param.vals[i], setting_value) == 0) ? "done" : "failed");
		}
		SoapySDRKwargs_clear(&settings_param);
		XFREE(settings);
	}
*/
	// TODO: look up and use native format
	cfg->sfmt = SFMT_CS16;
	SoapySDRStream *stream = NULL;
#if SOAPY_SDR_API_VERSION < 0x00080000
	if(SoapySDRDevice_setupStream(sdr, &stream, SOAPY_SDR_RX, SOAPY_SDR_CS16, NULL, 0, NULL) != 0)
#else
	if((stream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, NULL, 0, NULL)) == NULL)
#endif
	{
		fprintf(stderr, "%s: could not set up stream: %s\n", cfg->device_string, SoapySDRDevice_lastError());
		return -1;
	}

	input->block.producer.max_tu = SoapySDRDevice_getStreamMTU(sdr, stream);
	input->full_scale = get_sample_full_scale_value(cfg->sfmt);
	input->bytes_per_sample = get_sample_size(cfg->sfmt);
	soapysdr_input->sdr = sdr;
	soapysdr_input->stream = stream;
	return 0;
}

#define SOAPYSDR_READSTREAM_TIMEOUT_US 1000000L

void *soapysdr_input_thread(void *ctx) {
	ASSERT(ctx);
	struct block *block = ctx;
	struct input *input = container_of(block, struct input, block);
	struct soapysdr_input *soapysdr_input = container_of(input, struct soapysdr_input, input);
	uint8_t *buf = XCALLOC(input->block.producer.max_tu, input->bytes_per_sample);
	int ret;
	if((ret = SoapySDRDevice_activateStream(soapysdr_input->sdr, soapysdr_input->stream, 0, 0, 0)) != 0) {
		fprintf(stderr, "Failed to activate stream for SoapySDR device '%s': %s (ret=%d)\n",
			input->config->device_string, SoapySDRDevice_lastError(), ret);
		goto shutdown;
	}
	usleep(100000);

	while(do_exit == 0) {
		void *bufs[] = { buf };
		int flags;
		long long timeNs;
		int samples_read = SoapySDRDevice_readStream(soapysdr_input->sdr, soapysdr_input->stream, bufs,
			input->block.producer.max_tu, &flags, &timeNs, SOAPYSDR_READSTREAM_TIMEOUT_US);
		if(samples_read < 0) {	// when it's negative, it's the error code
			fprintf(stderr, "SoapySDR device '%s': readStream failed: %s\n",
				input->config->device_string, SoapySDR_errToStr(samples_read));
			continue;
		}
		//fprintf(stderr, "samples_read: %d buf_size: %zu\n", samples_read, samples_read * octets_per_complex_sample);
		input->convert_sample_buffer(input, buf, samples_read * input->bytes_per_sample);
	}
shutdown:
	fprintf(stderr, "soapysdr: Shutdown ordered, signaling consumer shutdown\n");
	SoapySDRDevice_deactivateStream(soapysdr_input->sdr, soapysdr_input->stream, 0, 0);
	SoapySDRDevice_closeStream(soapysdr_input->sdr, soapysdr_input->stream);
	SoapySDRDevice_unmake(soapysdr_input->sdr);
	block_connection_one2one_shutdown(block->producer.out);
	block->running = false;
	XFREE(buf);
	return NULL;
}

struct input_vtable const soapysdr_input_vtable = {
	.init = soapysdr_input_init,
	.rx_thread_routine = soapysdr_input_thread
};

