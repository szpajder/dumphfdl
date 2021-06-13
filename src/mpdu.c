/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // struct hfdl_pdu_qentry
#include "mpdu.h"                   // mpdu_direction
#include "util.h"                   // NEW, ASSERT, struct octet_string
#include "crc.h"                    // crc16_ccitt

struct hfdl_mpdu {
	struct octet_string *pdu;
};

// Forward declaration
la_type_descriptor const proto_DEF_hfdl_mpdu;

la_list *mpdu_parse(struct hfdl_pdu_qentry *q, la_reasm_ctx *reasm_ctx) {
	ASSERT(q);

	la_list *lpdu_list = NULL;
	// If raw frame output has been requested by the user,
	// then append a hfdl_mpdu node as a first entry on the list,
	// so that mpdu_format_text() could print it as hex.
	// If --raw-frames is disabled, then this node is omitted.
	if(Config.output_raw_frames == true) {
		NEW(struct hfdl_mpdu, mpdu);
		mpdu->pdu = q->pdu;
		la_proto_node *node = la_proto_node_new();
		node->data = mpdu;
		node->td = &proto_DEF_hfdl_mpdu;
		lpdu_list = la_list_append(lpdu_list, node);
	}
	return lpdu_list;
}

static void mpdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_mpdu const *mpdu = data;
	if(Config.output_raw_frames == true && mpdu->pdu->len > 0) {
		append_hexdump_with_indent(vstr, mpdu->pdu->buf, mpdu->pdu->len, indent+1);
	}
}

la_type_descriptor const proto_DEF_hfdl_mpdu = {
	.format_text = mpdu_format_text,
	.destroy = NULL
};
