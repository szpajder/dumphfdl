/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <math.h>                       // round
#include <time.h>                       // strftime, gmtime, localtime
#include <sys/time.h>                   // struct timeval
#include <libacars/libacars.h>          // la_proto_node
#include <libacars/vstring.h>           // la_vstring
#include "fmtr-text.h"
#include "output-common.h"              // fmtr_descriptor_t
#include "util.h"                       // struct octet_string, Config, EOL

static bool fmtr_text_supports_data_type(fmtr_input_type_t type) {
	return(type == FMTR_INTYPE_DECODED_FRAME);
}

static la_vstring *format_timestamp(struct timeval tv) {
	int millis = 0;
	if(Config.milliseconds == true) {
		millis = round(tv.tv_usec / 1000.0);
		if(millis > 999) {
		    millis -= 1000;
		    tv.tv_sec++;
		}
	}
	struct tm *tmstruct = (Config.utc == true ? gmtime(&tv.tv_sec) : localtime(&tv.tv_sec));

	char tbuf[30], tzbuf[8];
	strftime(tbuf, sizeof(tbuf), "%F %T", tmstruct);
	strftime(tzbuf, sizeof(tzbuf), "%Z", tmstruct);

	la_vstring *vstr = la_vstring_new();
	la_vstring_append_sprintf(vstr, "%s", tbuf);
	if(Config.milliseconds == true) {
		la_vstring_append_sprintf(vstr, ".%03d", millis);
	}
	la_vstring_append_sprintf(vstr, " %s", tzbuf);
	return vstr;
}

static struct octet_string *fmtr_text_format_decoded_msg(struct hfdl_msg_metadata *metadata, la_proto_node *root) {
	ASSERT(metadata != NULL);
	ASSERT(root != NULL);

	//la_vstring *timestamp = format_timestamp(metadata->burst_timestamp);
	la_vstring *vstr = la_vstring_new();

	la_vstring_append_sprintf(vstr, "[%d kHz]", metadata->freq / 1000);
	//la_vstring_destroy(timestamp, true);

	EOL(vstr);

	vstr = la_proto_tree_format_text(vstr, root);
	struct octet_string *ret = octet_string_new(vstr->str, vstr->len);
	la_vstring_destroy(vstr, false);
	return ret;
}

fmtr_descriptor_t fmtr_DEF_text = {
	.name = "text",
	.description = "Human readable text",
	.format_decoded_msg = fmtr_text_format_decoded_msg,
	.format_raw_msg = NULL,
	.supports_data_type = fmtr_text_supports_data_type,
	.output_format = OFMT_TEXT,
};