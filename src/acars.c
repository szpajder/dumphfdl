/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <libacars/libacars.h>      // la_type_descriptor, la_proto_node, LA_MSG_DIR_*
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/acars.h>         // la_acars_parse
#include <libacars/dict.h>          // la_dict
#include "config.h"                 // WITH_STATSD
#include "hfdl.h"                   // enum hfdl_pdu_direction
#include "statsd.h"                 // statsd_*

#ifdef WITH_STATSD
static void update_statsd_acars_metrics(la_msg_dir msg_dir, la_proto_node *root) {
	static la_dict const reasm_status_counter_names[] = {
		{ .id = LA_REASM_UNKNOWN, .val = "acars.reasm.unknown" },
		{ .id = LA_REASM_COMPLETE, .val = "acars.reasm.complete" },
		// { .id = LA_REASM_IN_PROGRESS, .val = "acars.reasm.in_progress" },    // report final states only
		{ .id = LA_REASM_SKIPPED, .val = "acars.reasm.skipped" },
		{ .id = LA_REASM_DUPLICATE, .val = "acars.reasm.duplicate" },
		{ .id = LA_REASM_FRAG_OUT_OF_SEQUENCE, .val = "acars.reasm.out_of_seq" },
		{ .id = LA_REASM_ARGS_INVALID, .val = "acars.reasm.invalid_args" },
		{ .id = 0, .val = NULL }
	};
	la_proto_node *node = la_proto_tree_find_acars(root);
	if(node == NULL) {
		return;
	}
	la_acars_msg *amsg = node->data;
	if(amsg->err == true) {
		return;
	}
	char const *metric = la_dict_search(reasm_status_counter_names, amsg->reasm_status);
	if(metric == NULL) {
		return;
	}
	// Dropping const on metric is allowed here, because dumphfdl metric names
	// do not contain any characters that statsd-c-client library would need to
	// sanitize (replace with underscores)
	statsd_increment_per_msgdir(msg_dir, (char *)metric);
}
#endif

la_proto_node *acars_parse(uint8_t *buf, uint32_t len, enum hfdl_pdu_direction direction,
		la_reasm_ctx *reasm_ctx, struct timeval rx_timestamp) {
	ASSERT(buf);
	la_proto_node *node = NULL;
	if(len > 0 && buf[0] == 1) {        // ACARS SOH byte
		la_msg_dir msg_dir = (direction == UPLINK_PDU ? LA_MSG_DIR_GND2AIR : LA_MSG_DIR_AIR2GND);
		node = la_acars_parse_and_reassemble(buf + 1, len - 1, msg_dir, reasm_ctx, rx_timestamp);
#ifdef WITH_STATSD
		update_statsd_acars_metrics(msg_dir, node);
#endif
	}
	return node;
}

