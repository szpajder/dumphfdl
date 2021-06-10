/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>              // perror
#include <stdlib.h>             // calloc
#include <pthread.h>            // pthread_*
#include <errno.h>              // errno
#include <string.h>             // strerror
#include <unistd.h>             // _exit
#include "util.h"               // octet_string

void *xcalloc(size_t nmemb, size_t size, char const *file, int line, char const *func) {
	void *ptr = calloc(nmemb, size);
	if(ptr == NULL) {
		fprintf(stderr, "%s:%d: %s(): calloc(%zu, %zu) failed: %s\n",
				file, line, func, nmemb, size, strerror(errno));
		_exit(1);
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size, char const *file, int line, char const *func) {
	ptr = realloc(ptr, size);
	if(ptr == NULL) {
		fprintf(stderr, "%s:%d: %s(): realloc(%zu) failed: %s\n",
				file, line, func, size, strerror(errno));
		_exit(1);
	}
	return ptr;
}

int start_thread(pthread_t *pth, void *(*start_routine)(void *), void *thread_ctx) {
	int ret = 0;
	if((ret = pthread_create(pth, NULL, start_routine, thread_ctx) != 0)) {
		errno = ret;
		perror("pthread_create() failed");
	}
	return ret;
}

void stop_thread(pthread_t pth) {
	int ret = 0;
	if((pthread_join(pth, NULL)) != 0) {
		errno = ret;
		perror("pthread_join failed");
	}
}

int pthread_barrier_create(pthread_barrier_t *barrier, unsigned count) {
	int ret;
	if((ret = pthread_barrier_init(barrier, NULL, count)) != 0) {
		errno = ret;
		perror("pthread_barrier_init() failed");
	}
	return ret;
}

int pthread_cond_initialize(pthread_cond_t *cond) {
	int ret;
	if((ret = pthread_cond_init(cond, NULL)) != 0) {
		errno = ret;
		perror("pthread_cond_init() failed");
	}
	return ret;
}

int pthread_mutex_initialize(pthread_mutex_t *mutex) {
	int ret;
	if((ret = pthread_mutex_init(mutex, NULL)) != 0) {
		errno = ret;
		perror("pthread_mutex_init() failed");
	}
	return ret;
}

struct octet_string *octet_string_new(void *buf, size_t len) {
	NEW(struct octet_string, ostring);
	ostring->buf = buf;
	ostring->len = len;
	return ostring;
}

void octet_string_destroy(struct octet_string *ostring) {
	if(ostring == NULL) {
		return;
	}
	XFREE(ostring->buf);
	XFREE(ostring);
}
