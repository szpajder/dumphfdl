/* K=7 r=1/2 Viterbi decoder
 * Copyright Feb 2004, Phil Karn, KA9Q
 */
#include <stddef.h>
#include "fec.h"

/* Create a new instance of a Viterbi decoder */
void *create_viterbi27(int len){
	return create_viterbi27_port(len);
}

void set_viterbi27_polynomial(int polys[2]){
	set_viterbi27_polynomial_port(polys);
}

/* Initialize Viterbi decoder for start of new frame */
int init_viterbi27(void *p,int starting_state){
	return init_viterbi27_port(p,starting_state);
}

/* Viterbi chainback */
int chainback_viterbi27(
		void *p,
		unsigned char *data, /* Decoded output data */
		unsigned int nbits, /* Number of data bits */
		unsigned int endstate){ /* Terminal encoder state */

	return chainback_viterbi27_port(p,data,nbits,endstate);
}

/* Delete instance of a Viterbi decoder */
void delete_viterbi27(void *p){
	delete_viterbi27_port(p);
}

/* Update decoder with a block of demodulated symbols
 * Note that nbits is the number of decoded data bits, not the number
 * of symbols!
 */
int update_viterbi27_blk(void *p,unsigned char syms[],int nbits){
	if(p == NULL)
		return -1;

	update_viterbi27_blk_port(p,syms,nbits);
	return 0;
}
