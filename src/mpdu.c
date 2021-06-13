/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include "hfdl.h"                   // struct hfdl_pdu_qentry
#include "mpdu.h"                   // mpdu_direction
#include "util.h"                   // NEW, ASSERT

struct hfdl_mpdu {
	struct hfdl_pdu_qentry *q;
};

// Forward declaration
la_type_descriptor const proto_DEF_hfdl_mpdu;

la_proto_node *mpdu_parse(struct hfdl_pdu_qentry *q, la_reasm_ctx *reasm_ctx) {
	la_proto_node *node = la_proto_node_new();
	node->td = &proto_DEF_hfdl_mpdu;
	NEW(struct hfdl_mpdu, mpdu);
	node->data = mpdu;
	node->next = NULL;

	mpdu->q = q;
	return node;
}

static void mpdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_mpdu const *pdu = data;

	if(Config.output_raw_frames == true && pdu->q->pdu->len > 0) {
		append_hexdump_with_indent(vstr, pdu->q->pdu->buf, pdu->q->pdu->len, indent+1);
	}

	if(pdu->q->pdu->len > 0) {
		append_hexdump_with_indent(vstr, pdu->q->pdu->buf, pdu->q->pdu->len, indent+1);
	}
}

la_type_descriptor const proto_DEF_hfdl_mpdu = {
	.format_text = mpdu_format_text,
	.destroy = NULL
};
