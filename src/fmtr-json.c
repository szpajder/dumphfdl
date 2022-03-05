/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <libacars/libacars.h>          // la_proto_node
#include <libacars/vstring.h>           // la_vstring
#include <libacars/json.h>
#include "fmtr-json.h"
#include "output-common.h"              // fmtr_descriptor_t
#include "util.h"                       // struct octet_string, Config, EOL
#include "pdu.h"                        // struct hfdl_pdu_metadata

// forward declarations
la_type_descriptor const td_DEF_hfdl_message;

static void hfdl_format_json(la_vstring *vstr, void const *data) {
	ASSERT(vstr);
	ASSERT(data);

	struct hfdl_pdu_metadata const *m = data;
	la_json_object_start(vstr, "app");
	la_json_append_string(vstr, "name", "dumphfdl");
	la_json_append_string(vstr, "ver", DUMPHFDL_VERSION);
	la_json_object_end(vstr);
	if(Config.station_id != NULL) {
		la_json_append_string(vstr, "station", Config.station_id);
	}

	la_json_object_start(vstr, "t");
	la_json_append_int64(vstr, "sec", m->metadata.rx_timestamp.tv_sec);
	la_json_append_int64(vstr, "usec", m->metadata.rx_timestamp.tv_usec);
	la_json_object_end(vstr);

	la_json_append_int64(vstr, "freq", m->freq);
	la_json_append_int64(vstr, "bit_rate", m->bit_rate);
	la_json_append_double(vstr, "sig_level", m->rssi);
	la_json_append_double(vstr, "noise_level", m->noise_floor);
	la_json_append_double(vstr, "freq_skew", m->freq_err_hz);
	la_json_append_char(vstr, "slot", m->slot);
}

static bool fmtr_json_supports_data_type(fmtr_input_type_t type) {
	return(type == FMTR_INTYPE_DECODED_FRAME);
}

static struct octet_string *fmtr_json_format_decoded_msg(struct metadata *metadata, la_proto_node *root) {
	ASSERT(metadata != NULL);
	ASSERT(root != NULL);

	// prepend the metadata node the the tree (and destroy it afterwards)
	la_proto_node *hfdl_pdu = la_proto_node_new();
	hfdl_pdu->td = &td_DEF_hfdl_message;
	hfdl_pdu->data = metadata;
	hfdl_pdu->next = root;

	la_vstring *vstr = la_proto_tree_format_json(NULL, hfdl_pdu);
	EOL(vstr);
	struct octet_string *ret = octet_string_new(vstr->str, vstr->len);
	la_vstring_destroy(vstr, false);
	XFREE(hfdl_pdu);
	return ret;
}

la_type_descriptor const td_DEF_hfdl_message = {
	.format_text = NULL,
	.format_json = hfdl_format_json,
	.json_key = "hfdl",
	.destroy = NULL
};

fmtr_descriptor_t fmtr_DEF_json = {
	.name = "json",
	.description = "Javascript object notation",
	.format_decoded_msg = fmtr_json_format_decoded_msg,
	.format_raw_msg = NULL,
	.supports_data_type = fmtr_json_supports_data_type,
	.output_format = OFMT_JSON
};
