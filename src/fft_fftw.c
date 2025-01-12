/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <complex.h>
#include <fftw3.h>
#include "fft.h"
#include "util.h"           // NEW
#include "config.h"         // WITH_FFTW3F_THREADS

void csdr_fft_init(int32_t thread_cnt) {
#ifdef WITH_FFTW3F_THREADS
	fftwf_init_threads();
	fftwf_plan_with_nthreads(thread_cnt);
	fprintf(stderr, "Initialized %d FFT threads\n", thread_cnt);
#endif
}

void csdr_fft_destroy() {
#ifdef WITH_FFTW3F_THREADS
	fftwf_cleanup_threads();
#endif
}

FFT_PLAN_T* csdr_make_fft_c2c(int32_t size, float complex* input, float complex* output, int32_t forward, int32_t benchmark) {
	NEW(FFT_PLAN_T, plan);
	// fftwf_complex is binary compatible with float complex
	plan->plan = fftwf_plan_dft_1d(size, (fftwf_complex *)input, (fftwf_complex *)output, forward ? FFTW_FORWARD : FFTW_BACKWARD, benchmark ? FFTW_MEASURE : FFTW_ESTIMATE);
	plan->size = size;
	plan->input = input;
	plan->output = output;
	return plan;
}

void csdr_destroy_fft_c2c(FFT_PLAN_T *plan) {
	if(plan) {
		fftwf_destroy_plan(plan->plan);
		XFREE(plan);
	}
}

void csdr_fft_execute(FFT_PLAN_T* plan) {
	fftwf_execute(plan->plan);
}
