/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <complex.h>
#include <math.h>
#include <liquid/liquid.h>
#include "config.h"                 // *_DEBUG
#include "block.h"                  // block_*
#include "util.h"                   // NEW, XCALLOC
#include "fastddc.h"                // fft_channelizer_create, fastddc_inv_cc
#include "hfdl.h"                   // HFDL_SYMBOL_RATE, SPS

#define SYMBUFFER_SIZE 30
#define A_LEN 127
#define M1_LEN 127
#define M2_LEN 15
#define M_SHIFT_CNT 8
#define T_LEN 15
#define EQ_LEN 15
#define DATA_FRAME_LEN 30
#define CORR_THRESHOLD 0.3f
#define MAX_SEARCH_RETRIES 3
#define HFDL_SSB_CARRIER_OFFSET_HZ 1440

typedef enum {
	SAMPLER_EMIT_BITS = 1,
	SAMPLER_EMIT_SYMBOLS = 2,
	SAMPLER_SKIP = 3
} sampler_state;

typedef enum {
	FRAMER_A1_SEARCH = 1,
	FRAMER_A2_SEARCH = 2,
	FRAMER_M1_SEARCH = 3,
	FRAMER_M2_SKIP = 4,
	FRAMER_EQ_TRAIN = 5,
	FRAMER_DATA_1 = 6,      // first half of data frame
	FRAMER_DATA_2 = 7       // second half
} framer_state;

struct costas_s {
	float alpha, beta, phi, dphi, err;
};
typedef struct costas_s *costas;

// values correspond to numbers of bits per symbol (arity)
typedef enum {
	M_UNKNOWN = 0,
	M_BPSK = 1,
	M_PSK4 = 2,
	M_PSK8 = 3
} mod_arity;
#define MODULATION_CNT 4

void *hfdl_decoder_thread(void *ctx);

struct hfdl_channel {
	struct block block;
	fft_channelizer channelizer;
	msresamp_crcf resampler;
	agc_crcf agc;
	costas loop;
	firfilt_crcf mf;
	eqlms_cccf eq;
	modem m[MODULATION_CNT];
	symsync_crcf ss;
	bsequence bits;
	bsequence user_data[M_SHIFT_CNT];
	cbuffercf symbols;
	uint64_t symbol_cnt, sample_cnt;
	float resamp_rate;
	sampler_state s_state;
	framer_state fr_state;
	mod_arity data_mod_arity;
	mod_arity current_mod_arity;
	int chan_freq;
	int resampler_delay;
	int symbols_wanted;
	int search_retries;
	int eq_train_seq_cnt;
	int data_segment_cnt;
	int train_bits_total;
	int train_bits_bad;
	int T_idx;
	uint32_t bitmask;
};

struct {
	mod_arity scheme;
	int data_segment_cnt;
	int code_rate;
} hfdl_frame_params[M_SHIFT_CNT] = {
	// 300 bps, single slot
	[0] = {
		.scheme = M_BPSK,
		.data_segment_cnt = 72,
		.code_rate = 4
	},
	// 600 bps, single slot
	[1] = {
		.scheme = M_BPSK,
		.data_segment_cnt = 72,
		.code_rate = 2
	},
	// 1200 bps, single slot
	[2] = {
		.scheme = M_PSK4,
		.data_segment_cnt = 72,
		.code_rate = 2
	},
	// 1800 bps, single slot
	[3] = {
		.scheme = M_PSK8,
		.data_segment_cnt = 72,
		.code_rate = 2
	},
	// 300 bps, dual slot
	[4] = {
		.scheme = M_BPSK,
		.data_segment_cnt = 168,
		.code_rate = 4
	},
	// 600 bps, dual slot
	[5] = {
		.scheme = M_BPSK,
		.data_segment_cnt = 168,
		.code_rate = 2
	},
	// 1200 bps, dual slot
	[6] = {
		.scheme = M_PSK4,
		.data_segment_cnt = 168,
		.code_rate = 2
	},
	// 1800 bps, dual slot
	[7] = {
		.scheme = M_PSK8,
		.data_segment_cnt = 168,
		.code_rate = 2
	}
};

#define HFDL_MF_TAPS_CNT 61
static float hfdl_matched_filter[HFDL_MF_TAPS_CNT] = { \
	-0.0082982, -0.0070036, -0.0045802, -0.0013410,
 	 0.0022887, 0.0058192, 0.0087528, 0.0106423,
 	 0.0111454, 0.0100705, 0.0074070, 0.0033386,
	-0.0017635, -0.0073674, -0.0128238, -0.0174242,
	-0.0204671, -0.0213268, -0.0195177, -0.0147487,
	-0.0069617, 0.0036496, 0.0166431, 0.0313540,
 	 0.0469403, 0.0624459, 0.0768746, 0.0892695,
 	 0.0987898, 0.1047803, 0.1068247, 0.1047803,
 	 0.0987898, 0.0892695, 0.0768746, 0.0624459,
 	 0.0469403, 0.0313540, 0.0166431, 0.0036496,
	-0.0069617, -0.0147487, -0.0195177, -0.0213268,
	-0.0204671, -0.0174242, -0.0128238, -0.0073674,
	-0.0017635, 0.0033386, 0.0074070, 0.0100705,
 	 0.0111454, 0.0106423, 0.0087528, 0.0058192,
 	 0.0022887, -0.0013410, -0.0045802, -0.0070036,
	-0.0082982 };
static float hfdl_matched_filter_interp[HFDL_MF_TAPS_CNT];

float complex T_seq[2][T_LEN] = {
	[0] = { 1.f, 1.f, 1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f, 1.f, -1.f, -1.f, -1.f, -1.f },
	[1] = { -1.f, -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f, -1.f, 1.f, 1.f, 1.f, 1.f }
};

struct {
	int A1_found, A2_found, M1_found;
	int train_bits_total, train_bits_bad;
	float A1_corr_total, A2_corr_total, M1_corr_total;
} S;

static uint32_t T = 0x9AF;      // training sequence

bsequence A_bs, M1[M_SHIFT_CNT], M2[M_SHIFT_CNT];

costas costas_cccf_create() {
	NEW(struct costas_s, c);
	c->alpha = 0.10f;
	c->beta = 0.2f * c->alpha * c->alpha;
	return c;
}

void costas_cccf_execute(costas c, float complex in, float complex *out) {
	*out = in * cexpf(-I * c->phi);
}

void costas_cccf_adjust(costas c, float err) {
	c->err = err;
	if(c->err > 1.0f) {
		c->err = 1.0f;
	} else if(c->err < -1.0f) {
		c->err = -1.0f;
	}
	c->phi += c->alpha * c->err;
	c->dphi += c->beta * c->err;
}

void costas_cccf_step(costas c) {
	c->phi += c->dphi;
	if(c->phi > M_PI) {
		c->phi -= 2.0 * M_PI;
	} else if(c->phi < -M_PI) {
		c->phi += 2.0 * M_PI;
	}
}

void hfdl_print_summary() {
	fprintf(stderr, "A1_found:\t\t%d\nA2_found:\t\t%d\nM1_found:\t\t%d\n",
			S.A1_found, S.A2_found, S.M1_found);
	fprintf(stderr, "A1_corr_avg:\t\t%4.3f\n", S.A1_found > 0 ? S.A1_corr_total / (float)S.A1_found : 0);
	fprintf(stderr, "A2_corr_avg:\t\t%4.3f\n", S.A2_found > 0 ? S.A2_corr_total / (float)S.A2_found : 0);
	fprintf(stderr, "M1_corr_avg:\t\t%4.3f\n", S.M1_found > 0 ? S.M1_corr_total / (float)S.M1_found : 0);
	fprintf(stderr, "train_bits_bad/total:\t%d/%d (%f%%)\n", S.train_bits_bad, S.train_bits_total,
			(float)S.train_bits_bad / (float)S.train_bits_total * 100.f);
}

void hfdl_init_globals() {
	uint8_t A_octets[] = {
		0b01011011,
		0b10111100,
		0b01110100,
		0b01010111,
		0b00000011,
		0b11011001,
		0b10001001,
		0b00111001,
		0b11110010,
		0b00001000,
		0b11010101,
		0b00110110,
		0b10010100,
		0b00101100,
		0b00110010,
		0b11111110
	};
	A_bs = bsequence_create(A_LEN);
	bsequence_init(A_bs, A_octets);

	uint32_t M1_bits[M1_LEN] = {
		0,1,1,1,0,1,1,0,1,1,1,1,0,1,0,0,0,1,0,1,1,0,0,
		1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,
		0,0,0,1,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,0,0,1,0,0,1,1,
		0,0,0,0,0,1,0,1,0,1,0,1,1,0,1,0,0,1,0,0,1,0,1,0,0,1,
		1,1,1,0,0,1,0,0,0,1,1,0,1,0,1,0,0,0,0,1,1,1,1,1,1,1
	};

	size_t M_shifts[M_SHIFT_CNT] = { 72, 82, 113, 123, 61, 103, 93, 9 };
	for(int shift = 0; shift < M_SHIFT_CNT; shift++) {
		M1[shift] = bsequence_create(M1_LEN);
		M2[shift] = bsequence_create(M2_LEN);
		for(int j = 0; j < M1_LEN; j++) {
			bsequence_push(M1[shift], M1_bits[(M_shifts[shift]+j) % M1_LEN]);
		}
		for(int j = 0; j < M2_LEN; j++) {
			bsequence_push(M2[shift], M1_bits[(M_shifts[shift]+j) % M1_LEN]);
		}
	}
	// symsync uses interpolator internally, so it needs MF filter taps
	// multipled by SPS
	for(int i = 0; i < HFDL_MF_TAPS_CNT; i++) {
		hfdl_matched_filter_interp[i] = hfdl_matched_filter[i] * SPS;
	}
}

static void sampler_reset(struct hfdl_channel *c) {
	symsync_crcf_reset(c->ss);
	c->s_state = SAMPLER_EMIT_BITS;
	c->bitmask = 0;
}

static void framer_reset(struct hfdl_channel *c) {
	c->fr_state = FRAMER_A1_SEARCH;
	c->symbols_wanted = 1;
	c->search_retries = 0;
	c->current_mod_arity = M_BPSK;
	c->train_bits_total = c->train_bits_bad = 0;
	c->T_idx = 0;
	agc_crcf_unlock(c->agc);
	eqlms_cccf_reset(c->eq);
	for(int i = 0; i < M_SHIFT_CNT; i++) {
		bsequence_reset(c->user_data[i]);
	}
	sampler_reset(c);
}

struct block *hfdl_channel_create(int sample_rate, int pre_decimation_rate,
		float transition_bw, int centerfreq, int frequency) {
	NEW(struct hfdl_channel, c);
	c->resamp_rate = (float)(HFDL_SYMBOL_RATE * SPS) / ((float)sample_rate / (float)pre_decimation_rate);
	c->resampler = msresamp_crcf_create(c->resamp_rate, 60.0f);
	c->resampler_delay = (int)ceilf(msresamp_crcf_get_delay(c->resampler));

	c->chan_freq = frequency;
	float freq_shift = (float)(centerfreq - (frequency + HFDL_SSB_CARRIER_OFFSET_HZ)) / (float)sample_rate;
	debug_print(D_DEMOD, "create: centerfreq=%d frequency=%d freq_shift=%f\n",
			centerfreq, frequency, freq_shift);

	c->channelizer = fft_channelizer_create(pre_decimation_rate, transition_bw, freq_shift);
	if(c->channelizer == NULL) {
		goto fail;
	}

	c->agc = agc_crcf_create();
	agc_crcf_set_bandwidth(c->agc, 0.01f);

	c->loop = costas_cccf_create();

	c->mf = firfilt_crcf_create(hfdl_matched_filter, HFDL_MF_TAPS_CNT);
	c->eq = eqlms_cccf_create(NULL, EQ_LEN);
	eqlms_cccf_set_bw(c->eq, 0.1f);

	c->m[M_BPSK] = modem_create(LIQUID_MODEM_BPSK);
	c->m[M_PSK4] = modem_create(LIQUID_MODEM_PSK4);
	c->m[M_PSK8] = modem_create(LIQUID_MODEM_PSK8);

	c->ss = symsync_crcf_create(SPS, 1, hfdl_matched_filter_interp, HFDL_MF_TAPS_CNT);

	c->bits = bsequence_create(M1_LEN);
	c->symbols = cbuffercf_create(SYMBUFFER_SIZE);

	for(int i = 0; i < M_SHIFT_CNT; i++) {
		c->user_data[i] = bsequence_create(hfdl_frame_params[i].data_segment_cnt * DATA_FRAME_LEN * hfdl_frame_params[i].scheme);
	}

	framer_reset(c);

	struct producer producer = { .type = PRODUCER_NONE };
	struct consumer consumer = { .type = CONSUMER_MULTI, .min_ru = 0 };
	c->block.producer = producer;
	c->block.consumer = consumer;
	c->block.thread_routine = hfdl_decoder_thread;

	return &c->block;
fail:
	XFREE(c);
	return NULL;
}

int match_sequence(bsequence *templates, size_t template_cnt, bsequence bits, float *result_corr) {
	float max_corr = 0.f;
	int max_idx = -1;
	int seq_len = bsequence_get_length(bits);
	for(size_t idx = 0; idx < template_cnt; idx++) {
		float corr = fabsf(2.0f * (float)bsequence_correlate(templates[idx], bits) / (float)seq_len - 1.0f);
		if(corr > max_corr) {
			max_corr = corr;
			max_idx = idx;
		}
	}
	*result_corr = max_corr;
	return max_idx;
}

void compute_train_bit_error_cnt(struct hfdl_channel *c) {
	uint32_t T_seq = 0, bit = 0;
	float complex s;
	for(int i = 0; i < T_LEN; i++) {
		cbuffercf_pop(c->symbols, &s);
		modem_demodulate(c->m[M_BPSK], s, &bit);
		bit ^= (c->bitmask & 1);
		T_seq = (T_seq << 1) | bit;
	}
	int error_cnt = count_bit_errors(T, T_seq);
	//debug_print(D_DEMOD, "T[%d]: val: %x err_cnt: %d\n", c->eq_train_seq_cnt, T_seq, error_cnt);
	S.train_bits_total += T_LEN;
	S.train_bits_bad += error_cnt;
	c->train_bits_total += T_LEN;
	c->train_bits_bad += error_cnt;
}

void demodulate_user_data(struct hfdl_channel *c, int M1) {
	bsequence bs = c->user_data[M1];
	uint32_t bits = 0;
	float complex s;
	for(int i = 0; i < DATA_FRAME_LEN; i++) {
		cbuffercf_pop(c->symbols, &s);
		modem_demodulate(c->m[c->data_mod_arity], s, &bits);
		bits ^= c->bitmask;
		for(int j = c->data_mod_arity - 1; j >= 0; j--) {
			bsequence_push(bs, bits >> j);
		}
	}
}

#define chan_debug(fmt, ...) { \
	debug_print(D_DEMOD, "%d: " fmt, c->chan_freq / 1000, ##__VA_ARGS__); \
}
#define fopen_datfile(file, suffix) \
	FILE *(file) = fopen(#file "." suffix, "w"); \
	assert(file)
#define fopen_cf32(file) fopen_datfile(file, "cf32")
#define fopen_rf32(file) fopen_datfile(file, "rf32")

#define write_block(file, ptr, cnt, type) fwrite(ptr, sizeof(type), cnt, (file))
#define write_value(file, val, type) write_block(file, &(val), 1, type)

#define write_cf32(file, val) write_value(file, val, float complex)
#define write_rf32(file, val) write_value(file, val, float)
#define write_nan_rf32(file) write_value(file, NaN, float)
#define write_nan_cf32(file) write_value(file, cNaN, float complex)
#define write_block_cf32(file, ptr, cnt) write_block(file, ptr, cnt, float complex)

float NaN = NAN;
float complex cNaN = NAN;
float zero = 0.f;

void *hfdl_decoder_thread(void *ctx) {
	ASSERT(ctx != NULL);
	struct block *block = ctx;
	struct hfdl_channel *c = container_of(block, struct hfdl_channel, block);

	// FIXME: post_input_size / post_decimation_rate ?
	float complex *channelizer_output = XCALLOC(c->channelizer->ddc->post_input_size, sizeof(float complex));
	size_t resampled_size = (c->channelizer->ddc->post_input_size + c->resampler_delay + 10) * c->resamp_rate;
	float complex *resampled = XCALLOC(resampled_size, sizeof(float complex));
	uint32_t resampled_cnt = 0;
	float complex r, s, t;
	float complex symbols[3];
	uint32_t symbols_produced = 0;
	uint32_t bits = 0;
	int M1_match = -1;
	float corr_A1 = 0.f;
	float corr_A2 = 0.f;
	float corr_M1 = 0.f;
	c->s_state = SAMPLER_EMIT_BITS;
	c->fr_state = FRAMER_A1_SEARCH;
#ifdef COSTAS_DEBUG
	fopen_rf32(f_costas_dphi);
	fopen_rf32(f_costas_err);
	fopen_cf32(f_costas_out);
#endif
#ifdef SYMSYNC_DEBUG
	fopen_cf32(f_symsync_in);
	fopen_cf32(f_symsync_out);
#endif
#ifdef CHAN_DEBUG
	fopen_cf32(f_chan_out);
#endif
#ifdef MF_DEBUG
	fopen_cf32(f_mf_in);
	fopen_cf32(f_mf_out);
#endif
#ifdef AGC_DEBUG
	fopen_rf32(f_agc_gain);
	fopen_rf32(f_agc_rssi);
	float gain, rssi;
#endif
#ifdef EQ_DEBUG
	fopen_cf32(f_eq_out);
#endif
#ifdef CORR_DEBUG
	fopen_rf32(f_corr_A1);
	fopen_rf32(f_corr_A2);
	bool first = true;
#endif
#ifdef DUMP_CONST
	uint64_t frame_id;
	FILE *consts = fopen("const.m", "w");
	assert(consts);
#endif
#ifdef DUMP_FFT
	fopen_cf32(f_fft_out);
#endif
	float evm_hat = 0.03f;
	float complex d_prime;
	struct shared_buffer *input = &block->consumer.in->shared_buffer;

	while(true) {
		pthread_barrier_wait(input->consumers_ready);
		pthread_barrier_wait(input->data_ready);
		if(block_connection_is_shutdown_signaled(block->consumer.in)) {
			debug_print(D_MISC, "channel %d: Exiting (ordered shutdown)\n", c->chan_freq);
			break;
		}
#ifdef DUMP_FFT
		write_block_cf32(f_fft_out, input->buf, c->channelizer->ddc->fft_size);
#endif
		// FIXME: pass c->channelizer pointer to this function
		c->channelizer->shift_status = fastddc_inv_cc(input->buf, channelizer_output, c->channelizer->ddc,
				c->channelizer->inv_plan, c->channelizer->filtertaps_fft, c->channelizer->shift_status);
#ifdef CHAN_DEBUG
		write_block_cf32(f_chan_out, channelizer_output, c->channelizer->shift_status.output_size);
#endif
		msresamp_crcf_execute(c->resampler, channelizer_output, c->channelizer->shift_status.output_size,
				resampled, &resampled_cnt);
		if(resampled_cnt < 1) {
			debug_print(D_DEMOD, "ERROR: resampled_cnt is 0\n");
			continue;
		}
		for(size_t k = 0; k < resampled_cnt; k++, c->sample_cnt++) {
			agc_crcf_execute(c->agc, resampled[k], &r);
			firfilt_crcf_push(c->mf, r);
			firfilt_crcf_execute(c->mf, &s);
#ifdef MF_DEBUG
			write_cf32(f_mf_in, resampled[k]);
			write_cf32(f_mf_out, s);
#endif
			t = s;
#ifdef AGC_DEBUG
			gain = agc_crcf_get_gain(c->agc);
			rssi = agc_crcf_get_rssi(c->agc);
			write_rf32(f_agc_gain, gain);
			write_rf32(f_agc_rssi, rssi);
#endif
#ifdef SYMSYNC_DEBUG
			write_cf32(f_symsync_in, t);
#endif
			symsync_crcf_execute(c->ss, &t, 1, symbols, &symbols_produced);
			if(symbols_produced == 0) {
#ifdef SYMSYNC_DEBUG
				write_nan_cf32(f_symsync_out);
#endif
#ifdef COSTAS_DEBUG
				write_nan_rf32(f_costas_dphi);
				write_nan_rf32(f_costas_err);
				write_nan_cf32(f_costas_out);
				write_nan_cf32(f_eq_out);
#endif
#ifdef CORR_DEBUG
				write_rf32(f_corr_A1, zero);
				write_rf32(f_corr_A2, zero);
#endif
			}
			for(size_t i = 0; i < symbols_produced; i++) {
#ifdef SYMSYNC_DEBUG
				write_cf32(f_symsync_out, symbols[i]);
#endif
				costas_cccf_execute(c->loop, t, &symbols[i]);
				// Back-propagate framer state to compensate delay introduced by eqlms
				// fr_state == DATA_1 -> Costas is now in DATA_2 (use current_mod_arity)
				// fr_state == TRAIN && eq_train_seq_cnt == 1  -> Costas is now in DATA_1 (use current_mod_arity)
				// fr_state == other -> Costas is now in A1_SEARCH, A2_SEARCH or M1_SEARCH or TRAIN (use BPSK)
				if((c->fr_state == FRAMER_EQ_TRAIN && c->eq_train_seq_cnt == 1) || c->fr_state == FRAMER_DATA_1) {
					//debug_print(D_DEMOD, "costas adjust: M=%d\n", c->data_mod_arity);
					modem_demodulate(c->m[c->data_mod_arity], symbols[i], &bits);
					costas_cccf_adjust(c->loop, modem_get_demodulator_phase_error(c->m[c->data_mod_arity]));
				} else {
					//debug_print(D_DEMOD, "costas adjust: M=1\n");
					modem_demodulate(c->m[M_BPSK], symbols[i], &bits);
					costas_cccf_adjust(c->loop, modem_get_demodulator_phase_error(c->m[M_BPSK]));
				}
				costas_cccf_step(c->loop);

#ifdef COSTAS_DEBUG
				write_rf32(f_costas_dphi, c->loop->dphi);
				write_rf32(f_costas_err, c->loop->err);
				write_cf32(f_costas_out, symbols[i]);
#endif
				eqlms_cccf_push(c->eq, symbols[i]);
				eqlms_cccf_execute(c->eq, &symbols[i]);
				if(c->fr_state == FRAMER_EQ_TRAIN) {
					eqlms_cccf_step(c->eq, T_seq[c->bitmask & 1][c->T_idx], symbols[i]);
					//debug_print(D_DEMOD, "train: T[%d]: %f output: %f+%f*i\n",
					//		c->T_idx, crealf(T_seq[c->bitmask & 1][c->T_idx]), crealf(symbols[i]), cimagf(symbols[i]));
					c->T_idx++;
				}
#ifdef EQ_DEBUG
				write_cf32(f_eq_out, symbols[i]);
#endif

				modem_demodulate(c->m[c->current_mod_arity], symbols[i], &bits);
				if(c->fr_state >= FRAMER_EQ_TRAIN) {
					modem_get_demodulator_sample(c->m[c->current_mod_arity], &d_prime);
					float evm = crealf((d_prime - symbols[i]) * conjf(d_prime - symbols[i]));
					evm_hat = 0.98f * evm_hat + 0.02f * evm;
					//debug_print(D_DEMOD, "d_prime = %f+%f*i symbol=%f+%f*i rms error = %12.8f dB arity = %d\n",
					//		crealf(d_prime), cimagf(d_prime), crealf(symbols[i]), cimagf(symbols[i]),
					//		10*log10(evm_hat), c->current_mod_arity);
				}

#ifdef DUMP_CONST
				if(c->fr_state >= FRAMER_EQ_TRAIN) {
					fprintf(consts, "frame%lu(end+1,1)=%f+%f*i;\n", frame_id,
							crealf(symbols[i]), cimagf(symbols[i]));
				}
#endif
#ifdef CORR_DEBUG
// this writes corr values calculated in the previous loop iteration, so the values on first loop
// iteration are meaningless and need to be skipped. Also reset the values afterwards, so that
// zeros get logged in states other than FRAMER_A1_SEARCH and FRAMER_A2_SEARCH.
				if(first) {
					first = false;
				} else {
					write_rf32(f_corr_A1, corr_A1);
					write_rf32(f_corr_A2, corr_A2);
					corr_A1 = 0.f; corr_A2 = 0.f;
				}
#endif

				c->symbol_cnt++;
				if(c->s_state == SAMPLER_EMIT_BITS) {
					bits ^= c->bitmask;
					for(uint32_t b = 0; b < c->current_mod_arity; b++, bits >>= 1) {
						bsequence_push(c->bits, bits);
					}
				} else if(c->s_state == SAMPLER_EMIT_SYMBOLS) {
					ASSERT(cbuffercf_space_available(c->symbols) != 0);
					cbuffercf_push(c->symbols, symbols[i]);
				} else {    // SKIP
							// NOOP
				}
				if(c->symbols_wanted > 1) {
					c->symbols_wanted--;
					continue;
				}

				switch(c->fr_state) {
				case FRAMER_A1_SEARCH:
					corr_A1 = 2.0f * (float)bsequence_correlate(A_bs, c->bits) / (float)A_LEN - 1.0f;
					if(fabsf(corr_A1) > CORR_THRESHOLD) {
						S.A1_found++;
						S.A1_corr_total += fabsf(corr_A1);
						c->bitmask = corr_A1 > 0.f ? 0 : ~0;
						agc_crcf_lock(c->agc);
						c->symbols_wanted = A_LEN;
						c->search_retries = 0;
						c->fr_state++;
#ifdef DUMP_CONST
						frame_id = c->sample_cnt;
#endif
					}
					break;
				case FRAMER_A2_SEARCH:
					corr_A2 = 2.0f * (float)bsequence_correlate(A_bs, c->bits) / (float)A_LEN - 1.0f;
					if(fabsf(corr_A2) > CORR_THRESHOLD) {
						chan_debug("A2 sequence found at sample %" PRIu64 " (corr=%f retry=%d costas_dphi=%f)\n",
								c->sample_cnt, corr_A2, c->search_retries, c->loop->dphi);
						S.A2_found++;
						S.A2_corr_total += fabsf(corr_A2);
						c->symbols_wanted = M1_LEN;
						c->search_retries = 0;
						c->fr_state = FRAMER_M1_SEARCH;
					} else if(++c->search_retries >= MAX_SEARCH_RETRIES) {
						framer_reset(c);
					}
					break;
				case FRAMER_M1_SEARCH:
					M1_match = match_sequence(M1, M_SHIFT_CNT, c->bits, &corr_M1);
					if(fabsf(corr_M1) > CORR_THRESHOLD) {
						chan_debug("M1 match at sample %" PRIu64 ": %d (corr=%f, costas_dphi=%f)\n", c->sample_cnt, M1_match, corr_M1, c->loop->dphi);
						S.M1_found++;
						S.M1_corr_total += fabsf(corr_M1);
						c->data_segment_cnt = hfdl_frame_params[M1_match].data_segment_cnt;
						c->data_mod_arity = hfdl_frame_params[M1_match].scheme;
						c->symbols_wanted = M2_LEN;
						c->search_retries = 0;
						c->fr_state = FRAMER_M2_SKIP;
						c->s_state = SAMPLER_SKIP;
					} else {
						chan_debug("M1 sequence unreliable (val=%d corr=%f)\n", M1_match, corr_M1);
						framer_reset(c);
					}
					break;
				case FRAMER_M2_SKIP:
					cbuffercf_reset(c->symbols);
					c->symbols_wanted = T_LEN;
					c->eq_train_seq_cnt = 9;
					c->fr_state = FRAMER_EQ_TRAIN;
					c->s_state = SAMPLER_EMIT_SYMBOLS;
#ifdef DUMP_CONST
					fprintf(consts, "frame%lu = [];\n", frame_id);
#endif
					break;
				case FRAMER_EQ_TRAIN:
					ASSERT(cbuffercf_size(c->symbols) == T_LEN);
					compute_train_bit_error_cnt(c);
					cbuffercf_reset(c->symbols);
					if(c->eq_train_seq_cnt > 1) {               // next frame is training sequence
						c->eq_train_seq_cnt--;
						c->symbols_wanted = T_LEN;
						c->T_idx = 0;
					} else if(c->data_segment_cnt > 0) {        // next frame is data frame
						c->symbols_wanted = DATA_FRAME_LEN / 2;
						c->fr_state = FRAMER_DATA_1;
						c->current_mod_arity = c->data_mod_arity;
					} else {                                    // end of frame
						chan_debug("train_bits_bad: %d/%d (%f%%)\n",
								c->train_bits_bad, c->train_bits_total,
								(float)c->train_bits_bad / (float)c->train_bits_total * 100.f);
						framer_reset(c);
					}
					break;
				case FRAMER_DATA_1:
					c->symbols_wanted = DATA_FRAME_LEN / 2;
					c->fr_state = FRAMER_DATA_2;
					break;
				case FRAMER_DATA_2:
					demodulate_user_data(c, M1_match);
					c->data_segment_cnt--;
					c->current_mod_arity = M_BPSK;
					c->fr_state = FRAMER_EQ_TRAIN;
					c->eq_train_seq_cnt = 1;
					c->symbols_wanted = T_LEN;
					c->T_idx = 0;
					cbuffercf_reset(c->symbols);
					break;
				}
			}
		}
	}
	block->running = false;
	return NULL;
}

