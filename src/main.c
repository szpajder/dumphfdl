/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>             // atoi
#define _GNU_SOURCE             // getopt_long
#include <getopt.h>
#include <signal.h>             // sigaction, SIG*
#include <string.h>             // strlen, strsep
#include <math.h>               // roundf
#include <unistd.h>             // usleep
#include <libacars/libacars.h>  // la_config_set_int
#include <libacars/acars.h>     // LA_ACARS_BEARER_HFDL
#include <libacars/list.h>      // la_list
#ifdef PROFILING
#include <gperftools/profiler.h>
#endif
#include "config.h"
#include "options.h"            // IND(), describe_option
#include "globals.h"            // do_exit, Systable
#include "block.h"              // block_*
#include "libcsdr.h"            // compute_filter_relative_transition_bw
#include "fft.h"                // csdr_fft_init, fft_create
#include "util.h"               // ASSERT
#include "ac_cache.h"           // ac_cache_create, ac_cache_destroy
#include "input-common.h"       // sample_format_t, input_create
#include "input-helpers.h"      // sample_format_from_string
#include "output-common.h"      // output_*, fmtr_*
#include "kvargs.h"             // kvargs
#include "hfdl.h"               // hfdl_channel_create
#include "systable.h"           // systable_*
#include "statsd.h"             // statsd_*

typedef struct {
	char *output_spec_string;
	char *intype, *outformat, *outtype;
	kvargs *outopts;
	char const *errstr;
	bool err;
} output_params;

// Forward declarations
la_list *setup_output(la_list *fmtr_list, char *output_spec);
output_params output_params_from_string(char *output_spec);
fmtr_instance_t *find_fmtr_instance(la_list *fmtr_list,
		fmtr_descriptor_t *fmttd, fmtr_input_type_t intype);
void start_all_output_threads(la_list *fmtr_list);
void start_all_output_threads_for_fmtr(void *p, void *ctx);
void start_output_thread(void *p, void *ctx);

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

void print_version() {
	fprintf(stderr, "dumphfdl %s\n", DUMPHFDL_VERSION);
}

#ifdef DEBUG
typedef struct {
	char *token;
	uint32_t value;
	char *description;
} msg_filterspec_t;

void print_msg_filterspec_list(msg_filterspec_t const *filters) {
	for(msg_filterspec_t const *ptr = filters; ptr->token != NULL; ptr++) {
		describe_option(ptr->token, ptr->description, 2);
	}
}

static msg_filterspec_t const debug_filters[] = {
	{ "none",               D_NONE,                         "No messages" },
	{ "all",                D_ALL,                          "All messages" },
	{ "sdr",                D_SDR,                          "SDR device handling" },
	{ "demod",              D_DEMOD,                        "DSP and demodulation" },
	{ "demod_detail",       D_DEMOD_DETAIL,                 "DSP and demodulation - details with raw data dumps" },
	{ "burst",              D_BURST,                        "HFDL burst decoding" },
	{ "burst_detail",       D_BURST_DETAIL,                 "HFDL burst decoding - details with raw data dumps" },
	{ "proto",              D_PROTO,                        "Frame payload decoding" },
	{ "proto_detail",       D_PROTO_DETAIL,                 "Frame payload decoding - details with raw data dumps" },
	{ "stats",              D_STATS,                        "Statistics generation" },
	{ "cache",              D_CACHE,                        "Operations on caches" },
	{ "output",             D_OUTPUT,                       "Data output operations" },
	{ "misc",               D_MISC,                         "Messages not falling into other categories" },
	{ 0,                    0,                              0 }
};

static void debug_filter_usage() {
	fprintf(stderr,
			"<filter_spec> is a comma-separated list of words specifying debug classes which should\n"
			"be printed.\n\nSupported debug classes:\n\n"
		   );

	print_msg_filterspec_list(debug_filters);

	fprintf(stderr,
			"\nBy default, no debug messages are printed.\n"
		   );
}

static void update_filtermask(msg_filterspec_t const *filters, char *token, uint32_t *fmask) {
	bool negate = false;
	if(token[0] == '-') {
		negate = true;
		token++;
		if(token[0] == '\0') {
			fprintf(stderr, "Invalid filtermask: no token after '-'\n");
			_exit(1);
		}
	}
	for(msg_filterspec_t const *ptr = filters; ptr->token != NULL; ptr++) {
		if(!strcmp(token, ptr->token)) {
			if(negate)
				*fmask &= ~ptr->value;
			else
				*fmask |= ptr->value;
			return;
		}
	}
	fprintf(stderr, "Unknown filter specifier: %s\n", token);
	_exit(1);
}

static uint32_t parse_msg_filterspec(msg_filterspec_t const *filters, void (*help)(), char *filterspec) {
	if(!strcmp(filterspec, "help")) {
		help();
		_exit(0);
	}
	uint32_t fmask = 0;
	char *token = strtok(filterspec, ",");
	if(token == NULL) {
		fprintf(stderr, "Invalid filter specification\n");
		_exit(1);
	}
	update_filtermask(filters, token, &fmask);
	while((token = strtok(NULL, ",")) != NULL) {
		update_filtermask(filters, token, &fmask);
	}
	return fmask;
}
#endif      // DEBUG

static bool parse_frequency(char const *freq_str, int32_t *result) {
	ASSERT(result);
	char *endptr = NULL;
	float val = strtof(freq_str, &endptr);
	int32_t ret = 0;
	if(endptr == freq_str) {
		fprintf(stderr, "'%s': not a valid frequency value (must be a numeric value in kHz)\n", freq_str);
		return false;
	} else if(errno == ERANGE) {
		goto overflow;
	}
	ret = (int32_t)(1e3 * val);
	if(ret == INT_MAX || ret == INT_MIN) {
		goto overflow;
	}
	debug_print(D_MISC, "str: %s val: %d\n", freq_str, ret);
	*result = ret;
	return true;
overflow:
	fprintf(stderr, "'%s': not a valid frequency value (overflow)\n", freq_str);
	return false;
}

static bool compute_centerfreq(int32_t *freqs, int32_t cnt, int32_t source_rate, int32_t *result) {
	ASSERT(result);
	ASSERT(cnt > 0);
	int32_t freq_min, freq_max;
	freq_min = freq_max = freqs[0];
	for(int i = 0; i < cnt; i++) {
		if(freqs[i] < freq_min) freq_min = freqs[i];
		if(freqs[i] > freq_max) freq_max = freqs[i];
	}
	int32_t span = abs(freq_max - freq_min);
	if(span >= source_rate) {
		fprintf(stderr, "Error: channel frequencies are too far apart"
				" (span is larger than receiver bandwidth)\n");
		return false;
	}
	*result = freq_min + (freq_max - freq_min) / 2;
	return true;
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
	describe_option("<freq_1> [<freq_2> [...]]", "HFDL channel frequencies, in kHz", 1);
#ifdef WITH_SOAPYSDR
	fprintf(stderr, "\nsoapysdr_options:\n");
	describe_option("--soapysdr <device_string>", "Use SoapySDR compatible device identified with the given string", 1);
	//describe_option("--device-settings <key1=val1,key2=val2,...>", "Set device-specific parameters (default: none)", 1);
	describe_option("--sample-rate <sample_rate>", "Set sampling rate (samples per second)", 1);
	describe_option("--centerfreq <center_frequency>", "Center frequency of the receiver, in kHz (default: auto)", 1);
	describe_option("--gain <gain>", "Set end-to-end gain (decibels)", 1);
	describe_option("--gain-elements <gain1=val1,gain2=val2,...>", "Set gain elements (default: none)", 1);
	//describe_option("--correction <correction>", "Set freq correction (ppm)", 1);
	//describe_option("--soapy-antenna <antenna>", "Set antenna port selection (default: RX)", 1);
#endif
	fprintf(stderr, "\nfile_options:\n");
	describe_option("--iq-file <input_file>", "Read I/Q samples from file", 1);
	describe_option("--sample-rate <sample_rate>", "Set sampling rate (samples per second)", 1);
	describe_option("--centerfreq <center_frequency>", "Center frequency of the input data, in kHz (default: auto)", 1);
	describe_option("--sample-format <sample_format>", "Input sample format. Supported formats:", 1);
	describe_option("CU8", "8-bit unsigned (eg. recorded with rtl_sdr)", 2);
	describe_option("CS16", "16-bit signed, little-endian (eg. recorded with sdrplay)", 2);
	describe_option("CF32", "32-bit float, little-endian", 2);

	fprintf(stderr, "\nOutput options:\n");
	describe_option("--output <output_specifier>", "Output specification (default: " DEFAULT_OUTPUT ")", 1);
	describe_option("", "(See \"--output help\" for details)", 1);
	describe_option("--output-queue-hwm <integer>", "High water mark value for output queues (0 = no limit)", 1);
	fprintf(stderr, "%*s(default: %d messages, not applicable when using --iq-file or --raw-frames-file)\n", USAGE_OPT_NAME_COLWIDTH, "", OUTPUT_QUEUE_HWM_DEFAULT);
	describe_option("--output-mpdus", "Include media access control protocol data units in the output (default: false)", 1);
	describe_option("--station-id <name>", "Receiver site identifier", 1);
	fprintf(stderr, "%*sMaximum length: %u characters\n", USAGE_OPT_NAME_COLWIDTH, "", STATION_ID_LEN_MAX);

	fprintf(stderr, "\nText output formatting options:\n");
	describe_option("--utc", "Use UTC timestamps in output and file names", 1);
	describe_option("--milliseconds", "Print milliseconds in timestamps", 1);
	describe_option("--raw-frames", "Print raw AVLC frame as hex", 1);
	describe_option("--prettify-xml", "Pretty-print XML payloads in ACARS and MIAM CORE PDUs", 1);

	fprintf(stderr, "\nSystem table options:\n");
	describe_option("--system-table <file>", "Load system table from file", 1);
	describe_option("--system-table-save <file>", "Save updated system table to the given file", 1);

#ifdef WITH_STATSD
	fprintf(stderr, "\nEtsy StatsD options:\n");
	describe_option("--statsd <host>:<port>", "Send statistics to Etsy StatsD server <host>:<port>", 1);
#endif
}

int32_t main(int32_t argc, char **argv) {

#define OPT_VERSION 1
#define OPT_HELP 2
#ifdef DEBUG
#define OPT_DEBUG 3
#endif

#define OPT_IQ_FILE 10
#ifdef WITH_SOAPYSDR
#define OPT_SOAPYSDR 11
#endif

#define OPT_SAMPLE_FORMAT 20
#define OPT_SAMPLE_RATE 21
#define OPT_CENTERFREQ 22
#define OPT_GAIN 23
#define OPT_GAIN_ELEMENTS 24

#define OPT_OUTPUT 40
#define OPT_OUTPUT_QUEUE_HWM 41
#define OPT_UTC 44
#define OPT_MILLISECONDS 45
#define OPT_RAW_FRAMES 46
#define OPT_PRETTIFY_XML 47
#define OPT_STATION_ID 48
#define OPT_OUTPUT_MPDUS 49

#define OPT_SYSTABLE_FILE 60
#define OPT_SYSTABLE_SAVE_FILE 61

#ifdef WITH_STATSD
#define OPT_STATSD 70
#endif

#define DEFAULT_OUTPUT "decoded:text:file:path=-"

	static struct option opts[] = {
		{ "version",            no_argument,        NULL,   OPT_VERSION },
		{ "help",               no_argument,        NULL,   OPT_HELP },
#ifdef DEBUG
		{ "debug",              required_argument,  NULL,   OPT_DEBUG },
#endif
		{ "iq-file",            required_argument,  NULL,   OPT_IQ_FILE },
#ifdef WITH_SOAPYSDR
		{ "soapysdr",           required_argument,  NULL,   OPT_SOAPYSDR },
#endif
		{ "sample-format",      required_argument,  NULL,   OPT_SAMPLE_FORMAT },
		{ "sample-rate",        required_argument,  NULL,   OPT_SAMPLE_RATE },
		{ "centerfreq",         required_argument,  NULL,   OPT_CENTERFREQ },
		{ "gain",               required_argument,  NULL,   OPT_GAIN },
		{ "gain-elements",      required_argument,  NULL,   OPT_GAIN_ELEMENTS },
		{ "output",             required_argument,  NULL,   OPT_OUTPUT },
		{ "output-queue-hwm",   required_argument,  NULL,   OPT_OUTPUT_QUEUE_HWM },
		{ "utc",                no_argument,        NULL,   OPT_UTC },
		{ "milliseconds",       no_argument,        NULL,   OPT_MILLISECONDS },
		{ "raw-frames",         no_argument,        NULL,   OPT_RAW_FRAMES },
		{ "prettify-xml",       no_argument,        NULL,   OPT_PRETTIFY_XML },
		{ "station-id",         required_argument,  NULL,   OPT_STATION_ID },
		{ "output-mpdus",       no_argument,        NULL,   OPT_OUTPUT_MPDUS },
		{ "system-table",       required_argument,  NULL,   OPT_SYSTABLE_FILE },
		{ "system-table-save",  required_argument,  NULL,   OPT_SYSTABLE_SAVE_FILE },
#ifdef WITH_STATSD
		{ "statsd",             required_argument,  NULL,   OPT_STATSD },
#endif
		{ 0,                    0,                  0,      0 }
	};

	// Initialize default config
	Config.output_queue_hwm = OUTPUT_QUEUE_HWM_DEFAULT;

	struct input_cfg *input_cfg = input_cfg_create();
	input_cfg->sfmt = SFMT_UNDEF;
	input_cfg->type = INPUT_TYPE_UNDEF;
	la_list *fmtr_list = NULL;
	char const *systable_file = NULL;
	char const *systable_save_file = NULL;
#ifdef WITH_STATSD
	char *statsd_addr = NULL;
#endif

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
				input_cfg->sfmt = sample_format_from_string(optarg);
				// Validate the result only when the sample format
				// has been supplied by the user. Otherwise it is left
				// to the input driver to guess the correct value. Before
				// this happens, the value of SFMT_UNDEF is not an error.
				if(input_cfg->sfmt == SFMT_UNDEF) {
					fprintf(stderr, "Sample format '%s' is unknown\n", optarg);
					return 1;
				}
				break;
			case OPT_SAMPLE_RATE:
				input_cfg->sample_rate = atoi(optarg);
				break;
			case OPT_CENTERFREQ:
				if(parse_frequency(optarg, &input_cfg->centerfreq) == false) {
					return 1;
				}
				break;
			case OPT_GAIN:
				// TODO
				break;
			case OPT_GAIN_ELEMENTS:
				input_cfg->gain_elements = optarg;
				break;
			case OPT_OUTPUT:
				fmtr_list = setup_output(fmtr_list, optarg);
				break;
			case OPT_OUTPUT_QUEUE_HWM:
				Config.output_queue_hwm = atoi(optarg);
				break;
			case OPT_UTC:
				Config.utc = true;
				break;
			case OPT_MILLISECONDS:
				Config.milliseconds = true;
				break;
			case OPT_RAW_FRAMES:
				Config.output_raw_frames = true;
				break;
			case OPT_PRETTIFY_XML:
				la_config_set_bool("prettify_xml", true);
				break;
			case OPT_STATION_ID:
				if(strlen(optarg) > STATION_ID_LEN_MAX) {
					fprintf(stderr, "Warning: --station-id argument too long; truncated to %d characters\n",
							STATION_ID_LEN_MAX);
				}
				Config.station_id = strndup(optarg, STATION_ID_LEN_MAX);
				break;
			case OPT_OUTPUT_MPDUS:
				Config.output_mpdus = true;
				break;
			case OPT_SYSTABLE_FILE:
				systable_file = optarg;
				break;
			case OPT_SYSTABLE_SAVE_FILE:
				systable_save_file = optarg;
				break;
#ifdef WITH_STATSD
			case OPT_STATSD:
				statsd_addr = strdup(optarg);
				break;
#endif
#ifdef DEBUG
			case OPT_DEBUG:
				Config.debug_filter = parse_msg_filterspec(debug_filters, debug_filter_usage, optarg);
				debug_print(D_MISC, "debug filtermask: 0x%x\n", Config.debug_filter);
				break;
#endif
			case OPT_VERSION:
				// No-op - the version has been printed before getopt().
				return 0;
			case OPT_HELP:
				usage();
				return 0;
			default:
				usage();
				return 1;
		}
	}
	if(input_cfg->device_string == NULL) {
		fprintf(stderr, "No input specified\n");
		return 1;
	}
	int32_t channel_cnt = argc - optind;
	if(channel_cnt < 1) {
		fprintf(stderr, "No channel frequencies given\n");
		return 1;
	}
	int32_t frequencies[channel_cnt];
	for(int32_t i = 0; i < channel_cnt; i++) {
		if(parse_frequency(argv[optind + i], &frequencies[i]) == false) {
			return 1;
		}
	}

	if(input_cfg->sample_rate < HFDL_SYMBOL_RATE * SPS) {
		fprintf(stderr, "Sample rate must be greater or equal to %d\n", HFDL_SYMBOL_RATE * SPS);
		return 1;
	}

	if(input_cfg->centerfreq < 0) {
		if(compute_centerfreq(frequencies, channel_cnt, input_cfg->sample_rate, &input_cfg->centerfreq) == true) {
			fprintf(stderr, "%s: computed center frequency: %d Hz\n", input_cfg->device_string, input_cfg->centerfreq);
		} else {
			fprintf(stderr, "%s: failed to compute center frequency\n", input_cfg->device_string);
			return 2;
		}
	}
	if(Config.output_queue_hwm < 0) {
		fprintf(stderr, "Invalid --output-queue-hwm value: must be a non-negative integer\n");
		return 1;
	}

	Systable = systable_create(systable_save_file);
	if(systable_file != NULL) {
		Systable_lock();
		if(systable_read_from_file(Systable, systable_file) == false) {
			fprintf(stderr, "Could not load system table from file %s:", systable_file);
			if(systable_error_type(Systable) == SYSTABLE_ERR_FILE_PARSE) {
				fprintf(stderr, " line %d:", systable_file_error_line(Systable));
			}
			fprintf(stderr, " %s\n", systable_error_text(Systable));
			systable_destroy(Systable);
			return 1;
		}
		Systable_unlock();
		fprintf(stderr, "System table loaded from %s\n", systable_file);
	}

	if((AC_cache = ac_cache_create()) == NULL) {
		fprintf(stderr, "Unable to initialize aircraft address cache\n");
		return 1;
	}

	// no --output given?
	if(fmtr_list == NULL) {
		fmtr_list = setup_output(fmtr_list, DEFAULT_OUTPUT);
	}
	ASSERT(fmtr_list != NULL);

	struct block *input = input_create(input_cfg);
	if(input == NULL) {
		fprintf(stderr, "Invalid input specified\n");
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
	debug_print(D_DEMOD, "fft_decimation_rate: %d sample_rate_post_fft: %d transition_bw: %.f\n",
			fft_decimation_rate, sample_rate_post_fft, fftfilt_transition_bw);

	struct block *fft = fft_create(fft_decimation_rate, fftfilt_transition_bw);
	if(fft == NULL) {
		return 1;
	}

#ifdef WITH_STATSD
	if(statsd_addr != NULL) {
		if(statsd_initialize(statsd_addr) < 0) {
			fprintf(stderr, "Failed to initialize StatsD client - disabling\n");
		} else {
			for(int i = 0; i < channel_cnt; i++) {
				statsd_initialize_counters_per_channel(frequencies[i]);
			}
			statsd_initialize_counters_per_msgdir();
		}
		XFREE(statsd_addr);
	}
#endif

	la_config_set_int("acars_bearer", LA_ACARS_BEARER_HFDL);
	hfdl_init_globals();

	struct block *channels[channel_cnt];
	for(int32_t i = 0; i < channel_cnt; i++) {
		channels[i] = hfdl_channel_create(input_cfg->sample_rate, fft_decimation_rate,
				fftfilt_transition_bw, input_cfg->centerfreq, frequencies[i]);
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

	start_all_output_threads(fmtr_list);
	hfdl_pdu_decoder_init();
	if(hfdl_pdu_decoder_start(fmtr_list) != 0) {
	    fprintf(stderr, "Failed to start decoder thread, aborting\n");
	    return 1;
	}

	setup_signals();

#ifdef PROFILING
	ProfilerStart("dumphfdl.prof");
#endif

	if(block_set_start(channel_cnt, channels) != channel_cnt ||
		block_start(fft) != 1 ||
		block_start(input) != 1) {
		return 1;
	}
	while(!do_exit) {
		sleep(1);
	}
	hfdl_pdu_decoder_stop();
	fprintf(stderr, "Waiting for all threads to finish\n");
	while(do_exit < 2 && (
			block_is_running(input) ||
			block_is_running(fft) ||
			block_set_is_any_running(channel_cnt, channels) ||
			hfdl_pdu_decoder_is_running() ||
			output_thread_is_any_running(fmtr_list)
			)) {
		usleep(500000);
	}

#ifdef PROFILING
	ProfilerStop();
#endif

	hfdl_print_summary();

	Systable_lock();
	systable_destroy(Systable);
	Systable_unlock();

	AC_cache_lock();
	ac_cache_destroy(AC_cache);
	AC_cache_unlock();

	return 0;
}

la_list *setup_output(la_list *fmtr_list, char *output_spec) {
	if(!strcmp(output_spec, "help")) {
		output_usage();
		_exit(0);
	}
	output_params oparams = output_params_from_string(output_spec);
	if(oparams.err == true) {
		fprintf(stderr, "Could not parse output specifier '%s': %s\n", output_spec, oparams.errstr);
		_exit(1);
	}
	debug_print(D_MISC, "intype: %s outformat: %s outtype: %s\n",
			oparams.intype, oparams.outformat, oparams.outtype);

	fmtr_input_type_t intype = fmtr_input_type_from_string(oparams.intype);
	if(intype == FMTR_INTYPE_UNKNOWN) {
		fprintf(stderr, "Data type '%s' is unknown\n", oparams.intype);
		_exit(1);
	}

	output_format_t outfmt = output_format_from_string(oparams.outformat);
	if(outfmt == OFMT_UNKNOWN) {
		fprintf(stderr, "Output format '%s' is unknown\n", oparams.outformat);
		_exit(1);
	}

	fmtr_descriptor_t *fmttd = fmtr_descriptor_get(outfmt);
	ASSERT(fmttd != NULL);
	fmtr_instance_t *fmtr = find_fmtr_instance(fmtr_list, fmttd, intype);
	if(fmtr == NULL) {      // we haven't added this formatter to the list yet
		if(!fmttd->supports_data_type(intype)) {
			fprintf(stderr,
					"Unsupported data_type:format combination: '%s:%s'\n",
					oparams.intype, oparams.outformat);
			_exit(1);
		}
		fmtr = fmtr_instance_new(fmttd, intype);
		ASSERT(fmtr != NULL);
		fmtr_list = la_list_append(fmtr_list, fmtr);
	}

	output_descriptor_t *otd = output_descriptor_get(oparams.outtype);
	if(otd == NULL) {
		fprintf(stderr, "Output type '%s' is unknown\n", oparams.outtype);
		_exit(1);
	}
	if(!otd->supports_format(outfmt)) {
		fprintf(stderr, "Unsupported format:output combination: '%s:%s'\n",
				oparams.outformat, oparams.outtype);
		_exit(1);
	}

	void *output_cfg = otd->configure(oparams.outopts);
	if(output_cfg == NULL) {
		fprintf(stderr, "Invalid output configuration\n");
		_exit(1);
	}

	output_instance_t *output = output_instance_new(otd, outfmt, output_cfg);
	ASSERT(output != NULL);
	fmtr->outputs = la_list_append(fmtr->outputs, output);

	// oparams is no longer needed after this point.
	// No need to free intype, outformat and outtype fields, because they
	// point into output_spec_string.
	XFREE(oparams.output_spec_string);
	kvargs_destroy(oparams.outopts);

	return fmtr_list;
}

#define SCAN_FIELD_OR_FAIL(str, field_name, errstr) \
	(field_name) = strsep(&(str), ":"); \
	if((field_name)[0] == '\0') { \
		(errstr) = "field_name is empty"; \
		goto fail; \
	} else if((str) == NULL) { \
		(errstr) = "not enough fields"; \
		goto fail; \
	}

output_params output_params_from_string(char *output_spec) {
	output_params out_params = {
		.intype = NULL, .outformat = NULL, .outtype = NULL, .outopts = NULL,
		.errstr = NULL, .err = false
	};

	// We have to work on a copy of output_spec, because strsep() modifies its
	// first argument. The copy gets stored in the returned out_params structure
	// so that the caller can free it later.
	debug_print(D_MISC, "output_spec: %s\n", output_spec);
	out_params.output_spec_string = strdup(output_spec);
	char *ptr = out_params.output_spec_string;

	// output_spec format is: <input_type>:<output_format>:<output_type>:<output_options>
	SCAN_FIELD_OR_FAIL(ptr, out_params.intype, out_params.errstr);
	SCAN_FIELD_OR_FAIL(ptr, out_params.outformat, out_params.errstr);
	SCAN_FIELD_OR_FAIL(ptr, out_params.outtype, out_params.errstr);
	debug_print(D_MISC, "intype: %s outformat: %s outtype: %s\n",
			out_params.intype, out_params.outformat, out_params.outtype);

	debug_print(D_MISC, "kvargs input string: %s\n", ptr);
	kvargs_parse_result outopts = kvargs_from_string(ptr);
	if(outopts.err == 0) {
		out_params.outopts = outopts.result;
	} else {
		out_params.errstr = kvargs_get_errstr(outopts.err);
		goto fail;
	}

	out_params.outopts = outopts.result;
	debug_print(D_MISC, "intype: %s outformat: %s outtype: %s\n",
			out_params.intype, out_params.outformat, out_params.outtype);
	goto end;
fail:
	XFREE(out_params.output_spec_string);
	out_params.err = true;
end:
	return out_params;
}

fmtr_instance_t *find_fmtr_instance(la_list *fmtr_list, fmtr_descriptor_t *fmttd, fmtr_input_type_t intype) {
	if(fmtr_list == NULL) {
		return NULL;
	}
	for(la_list *p = fmtr_list; p != NULL; p = la_list_next(p)) {
		fmtr_instance_t *fmtr = p->data;
		if(fmtr->td == fmttd && fmtr->intype == intype) {
			return fmtr;
		}
	}
	return NULL;
}

void start_all_output_threads(la_list *fmtr_list) {
	la_list_foreach(fmtr_list, start_all_output_threads_for_fmtr, NULL);
}

void start_all_output_threads_for_fmtr(void *p, void *ctx) {
	UNUSED(ctx);
	ASSERT(p != NULL);
	fmtr_instance_t *fmtr = p;
	la_list_foreach(fmtr->outputs, start_output_thread, NULL);
}

void start_output_thread(void *p, void *ctx) {
	UNUSED(ctx);
	ASSERT(p != NULL);
	output_instance_t *output = p;
	debug_print(D_OUTPUT, "starting thread for output %s\n", output->td->name);
	start_thread(output->output_thread, output_thread, output);
}

