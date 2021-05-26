/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>             // atoi
#define _GNU_SOURCE             // getopt_long
#include <getopt.h>
#include <signal.h>             // sigaction, SIG*
#include <string.h>             // strlen
#include <math.h>               // roundf
#include <unistd.h>             // usleep
#ifdef PROFILING
#include <gperftools/profiler.h>
#endif
#include "config.h"
#include "globals.h"            // do_exit
#include "block.h"              // block_*
#include "libcsdr.h"            // compute_filter_relative_transition_bw
#include "fft.h"                // csdr_fft_init, fft_create
#include "util.h"               // ASSERT
#include "input-common.h"       // sample_format_t, input_create
#include "hfdl.h"               // hfdl_channel_create

void sighandler(int sig) {
	fprintf(stderr, "Got signal %d, ", sig);
	if(do_exit == 0) {
		fprintf(stderr, "exiting gracefully (send signal once again to force quit)\n");
	} else {
		fprintf(stderr, "forcing quit\n");
	}
	do_exit++;
}

static void setup_signals() {
	struct sigaction sigact = {0}, pipeact = {0};

	pipeact.sa_handler = SIG_IGN;
	sigact.sa_handler = &sighandler;
	sigaction(SIGPIPE, &pipeact, NULL);
	sigaction(SIGHUP, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
}

// help text pretty-printing constants and macros
#define USAGE_INDENT_STEP 4
#define USAGE_OPT_NAME_COLWIDTH 48
#define IND(n) (n * USAGE_INDENT_STEP)

void print_version() {
	fprintf(stderr, "dumphfdl %s\n", DUMPHFDL_VERSION);
}

void describe_option(char const *name, char const *description, int indent) {
	int descr_shiftwidth = USAGE_OPT_NAME_COLWIDTH - (int)strlen(name) - indent * USAGE_INDENT_STEP;
	if(descr_shiftwidth < 1) {
		descr_shiftwidth = 1;
	}
	fprintf(stderr, "%*s%s%*s%s\n", IND(indent), "", name, descr_shiftwidth, "", description);
}

void usage() {
	fprintf(stderr, "Usage:\n");
#ifdef WITH_SOAPYSDR
	fprintf(stderr, "\nSOAPYSDR compatible receiver:\n\n"
			"%*sdumphfdl [output_options] --soapysdr <device_string> [soapysdr_options] [<freq_1> [<freq_2> [...]]]\n",
			IND(1), "");
#endif
	fprintf(stderr, "\nRead I/Q samples from file:\n\n"
			"%*sdumphfdl [output_options] --iq-file <input_file> [file_options] [<freq_1> [<freq_2> [...]]]\n",
			IND(1), "");
	fprintf(stderr, "\nGeneral options:\n");
	describe_option("--help", "Displays this text", 1);
	describe_option("--version", "Displays program version number", 1);
#ifdef DEBUG
	describe_option("--debug <filter_spec>", "Debug message classes to display (default: none) (\"--debug help\" for details)", 1);
#endif
	fprintf(stderr, "common options:\n");
	describe_option("<freq_1> [<freq_2> [...]]", "HFDL channel frequencies, in Hz", 1);
#ifdef WITH_SOAPYSDR
	fprintf(stderr, "\nsoapysdr_options:\n");
	describe_option("--soapysdr <device_string>", "Use SoapySDR compatible device identified with the given string", 1);
	//describe_option("--device-settings <key1=val1,key2=val2,...>", "Set device-specific parameters (default: none)", 1);
	describe_option("--sample-rate <sample_rate>", "Set sampling rate (samples per second)", 1);
	describe_option("--centerfreq <center_frequency>", "Center frequency of the receiver, in Hz (default: 0)", 1);
	describe_option("--gain <gain>", "Set end-to-end gain (decibels)", 1);
	describe_option("--gain-elements <gain1=val1,gain2=val2,...>", "Set gain elements (default: none)", 1);
	//describe_option("--correction <correction>", "Set freq correction (ppm)", 1);
	//describe_option("--soapy-antenna <antenna>", "Set antenna port selection (default: RX)", 1);
#endif
	fprintf(stderr, "\nfile_options:\n");
	describe_option("--iq-file <input_file>", "Read I/Q samples from file", 1);
	describe_option("--sample-rate <sample_rate>", "Set sampling rate (samples per second)", 1);
	describe_option("--centerfreq <center_frequency>", "Center frequency of the input data, in Hz (default: 0)", 1);
	describe_option("--sample-format <sample_format>", "Input sample format. Supported formats:", 1);
	describe_option("CU8", "8-bit unsigned (eg. recorded with rtl_sdr) (default)", 2);
	describe_option("CS16", "16-bit signed, little-endian (eg. recorded with miri_sdr)", 2);
	describe_option("CF32", "32-bit float, little-endian", 2);
}

int32_t main(int32_t argc, char **argv) {

#define OPT_VERSION 1
#define OPT_HELP 2
#define OPT_IQ_FILE 3
#ifdef WITH_SOAPYSDR
#define OPT_SOAPYSDR 4
#endif

#define OPT_SAMPLE_FORMAT 10
#define OPT_SAMPLE_RATE 11
#define OPT_CENTERFREQ 12
#define OPT_GAIN 13
#define OPT_GAIN_ELEMENTS 14

	static struct option opts[] = {
		{ "iq-file",            required_argument,  NULL,   OPT_IQ_FILE },
#ifdef WITH_SOAPYSDR
		{ "soapysdr",           required_argument,  NULL,   OPT_SOAPYSDR },
#endif
		{ "sample-format",      required_argument,  NULL,   OPT_SAMPLE_FORMAT },
		{ "sample-rate",        required_argument,  NULL,   OPT_SAMPLE_RATE },
		{ "centerfreq",         required_argument,  NULL,   OPT_CENTERFREQ },
		{ "gain",               required_argument,  NULL,   OPT_GAIN },
		{ "gain-elements",      required_argument,  NULL,   OPT_GAIN_ELEMENTS },
		{ "version",            no_argument,        NULL,   OPT_VERSION },
		{ "help",               no_argument,        NULL,   OPT_HELP },
#ifdef DEBUG
		{ "debug",              required_argument,  NULL,   OPT_DEBUG },
#endif
		{ 0,                    0,                  0,      0 }
	};

	struct input_cfg *input_cfg = input_cfg_create();
	input_cfg->sfmt = SFMT_CS16;     // TEMP
	input_cfg->type = INPUT_TYPE_UNDEF;

	print_version();

	int c = -1;
	while((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
		switch(c) {
			case OPT_IQ_FILE:
				input_cfg->device_string = optarg;
				input_cfg->type = INPUT_TYPE_FILE;
				break;
#ifdef WITH_SOAPYSDR
			case OPT_SOAPYSDR:
				input_cfg->device_string = optarg;
				input_cfg->type = INPUT_TYPE_SOAPYSDR;
				break;
#endif
			case OPT_SAMPLE_FORMAT:
				// TODO
				break;
			case OPT_SAMPLE_RATE:
				input_cfg->sample_rate = atoi(optarg);
				break;
			case OPT_CENTERFREQ:
				input_cfg->centerfreq = atoi(optarg);
				break;
			case OPT_GAIN:
				// TODO
				break;
			case OPT_GAIN_ELEMENTS:
				input_cfg->gain_elements = optarg;
				break;
#ifdef DEBUG
			case OPT_DEBUG:
				// TODO
				break;
#endif
			case OPT_VERSION:
				// No-op - the version has been printed before getopt().
				_exit(0);
			case OPT_HELP:
				usage();
				return 0;
			default:
				usage();
				return 1;
		}
	}
	int channel_cnt = argc - optind;
	if(channel_cnt < 1) {
		fprintf(stderr, "No channel frequencies given\n");
		usage();
		return 1;
	}
	if(input_cfg->sample_rate < HFDL_SYMBOL_RATE * SPS) {
		fprintf(stderr, "Sample rate must be greater or equal to %d\n", HFDL_SYMBOL_RATE * SPS);
		usage();
		return 1;
	}
	if(input_cfg->centerfreq < 0) {
		fprintf(stderr, "Center frequency must be non-negative\n");
		usage();
		return 1;
	}

	struct block *input = input_create(input_cfg);
	if(input == NULL) {
		fprintf(stderr, "Invalid input specified");
		usage();
		return 1;
	}
	if(input_init(input) < 0) {
		fprintf(stderr, "Unable to initialize input\n");
		return 1;
	}

	csdr_fft_init();

	int fft_decimation_rate = compute_fft_decimation_rate(input_cfg->sample_rate, HFDL_SYMBOL_RATE * SPS);
	ASSERT(fft_decimation_rate > 0);
	int sample_rate_post_fft = roundf((float)input_cfg->sample_rate / (float)fft_decimation_rate);
	float fftfilt_transition_bw = compute_filter_relative_transition_bw(input_cfg->sample_rate, HFDL_CHANNEL_TRANSITION_BW_HZ);
	fprintf(stderr, "fft_decimation_rate: %d sample_rate_post_fft: %d transition_bw: %.f\n",
			fft_decimation_rate, sample_rate_post_fft, fftfilt_transition_bw);

	struct block *fft = fft_create(fft_decimation_rate, fftfilt_transition_bw);
	if(fft == NULL) {
		return 1;
	}

	hfdl_init_globals();

	struct block *channels[channel_cnt];
	for(int32_t i = 0; i < channel_cnt; i++) {
		channels[i] = hfdl_channel_create(input_cfg->sample_rate, fft_decimation_rate,
				fftfilt_transition_bw, input_cfg->centerfreq, atoi(argv[optind + i]));
		if(channels[i] == NULL) {
			fprintf(stderr, "Failed to initialize channel %s\n",
					argv[optind + i]);
			return 1;
		}
	}

	if(block_connect_one2one(input, fft) != 1 ||
			block_connect_one2many(fft, channel_cnt, channels) != channel_cnt) {
		return 1;
	}

	setup_signals();

#ifdef PROFILING
	ProfilerStart("dumphfdl.prof");
#endif

	fprintf(stderr, "Starting blocks\n");
	if(block_set_start(channel_cnt, channels) != channel_cnt ||
		block_start(fft) != 1 ||
		block_start(input) != 1) {
		return 1;
	}
	while(!do_exit) {
		sleep(1);
	}
	fprintf(stderr, "Waiting for threads to finish\n");
	while(do_exit < 2 && (
			block_is_running(input) ||
			block_is_running(fft) ||
			block_set_is_any_running(channel_cnt, channels)
			)) {
		usleep(500000);
	}

#ifdef PROFILING
	ProfilerStop();
#endif

	hfdl_print_summary();

	fprintf(stderr, "Exiting\n");
	return 0;
}

