/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>                  // perror
#include <stdlib.h>                 // calloc
#include <pthread.h>                // pthread_*
#include <errno.h>                  // errno
#include <string.h>                 // strerror
#include <unistd.h>                 // _exit
#include <libacars/vstring.h>       // la_vstring
#include "util.h"                   // octet_string

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

struct octet_string *octet_string_copy(struct octet_string const *ostring) {
	ASSERT(ostring != NULL);
	NEW(struct octet_string, copy);
	copy->len = ostring->len;
	if(ostring->buf != NULL && ostring->len > 0) {
		copy->buf = XCALLOC(copy->len, sizeof(uint8_t));
		memcpy(copy->buf, ostring->buf, ostring->len);
	}
	return copy;
}

void octet_string_destroy(struct octet_string *ostring) {
	if(ostring == NULL) {
		return;
	}
	XFREE(ostring->buf);
	XFREE(ostring);
}

char *hexdump(uint8_t *data, size_t len) {
	static const char hex[] = "0123456789abcdef";
	if(data == NULL) return strdup("<undef>");
	if(len == 0) return strdup("<none>");

	size_t rows = len / 16;
	if((len & 0xf) != 0) {
		rows++;
	}
	size_t rowlen = 16 * 2 + 16;            // 32 hex digits + 16 spaces per row
	rowlen += 16;                           // ASCII characters per row
	rowlen += 10;                           // extra space for separators
	size_t alloc_size = rows * rowlen + 1;  // terminating NULL
	char *buf = XCALLOC(alloc_size, sizeof(char));
	char *ptr = buf;
	size_t i = 0, j = 0;
	while(i < len) {
		for(j = i; j < i + 16; j++) {
			if(j < len) {
				*ptr++ = hex[((data[j] >> 4) & 0xf)];
				*ptr++ = hex[data[j] & 0xf];
			} else {
				*ptr++ = ' ';
				*ptr++ = ' ';
			}
			*ptr++ = ' ';
			if(j == i + 7) {
				*ptr++ = ' ';
			}
		}
		*ptr++ = ' ';
		*ptr++ = '|';
		for(j = i; j < i + 16; j++) {
			if(j < len) {
				if(data[j] < 32 || data[j] > 126) {
					*ptr++ = '.';
				} else {
					*ptr++ = data[j];
				}
			} else {
				*ptr++ = ' ';
			}
			if(j == i + 7) {
				*ptr++ = ' ';
			}
		}
		*ptr++ = '|';
		*ptr++ = '\n';
		i += 16;
	}
	return buf;
}


void append_hexdump_with_indent(la_vstring *vstr, uint8_t *data, size_t len, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(indent >= 0);
	char *h = hexdump(data, len);
	la_isprintf_multiline_text(vstr, indent, h);
	XFREE(h);
}
