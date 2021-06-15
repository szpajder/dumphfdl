/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // struct hfdl_pdu_metadata, struct hfdl_pdu_hdr_data, enum hfdl_pdu_direction
#include "util.h"                   // NEW, ASSERT, struct octet_string
#include "crc.h"                    // crc16_ccitt

struct hfdl_mpdu {
	struct octet_string *pdu;
	struct hfdl_pdu_hdr_data header;
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_mpdu;
static bool mpdu_fcs_check(uint8_t *buf, uint32_t hdr_len);

la_list *mpdu_parse(struct octet_string *pdu, la_reasm_ctx *reasm_ctx) {
	ASSERT(pdu);
	ASSERT(pdu->buf);
	ASSERT(pdu->len > 0);

	la_list *lpdu_list = NULL;
	// If raw frame output has been requested by the user,
	// then append a hfdl_mpdu node as a first entry on the list,
	// so that mpdu_format_text() could print it as hex.
	// If --raw-frames is disabled, then this node is omitted.
	struct hfdl_mpdu *mpdu = NULL;
	if(Config.output_raw_frames == true) {
		mpdu = XCALLOC(1, sizeof(struct hfdl_mpdu));
		mpdu->pdu = pdu;
		la_proto_node *node = la_proto_node_new();
		node->data = mpdu;
		node->td = &proto_DEF_hfdl_mpdu;
		lpdu_list = la_list_append(lpdu_list, node);
	}
	struct hfdl_pdu_hdr_data mpdu_header = {0};
	uint32_t lpdu_cnt = 0;
	uint32_t hdr_len = 0;
	uint8_t *buf = pdu->buf;
	uint32_t len = pdu->len;
	if(pdu->buf[0] & 0x2) {
		mpdu_header.direction = DOWNLINK_PDU;
		lpdu_cnt = (buf[0] >> 2) & 0xF;
		hdr_len = 6 + lpdu_cnt;             // header length, not incl. FCS
	} else {
		mpdu_header.direction = UPLINK_PDU;
		if(len < 4) {
			goto end;
		}
		lpdu_cnt = (buf[3] >> 4) & 0xF;
		hdr_len = 4 + lpdu_cnt;             // header length, not incl. FCS
	}
	if(len < hdr_len + lpdu_cnt + 2) {
		debug_print(D_PROTO, "Too short: %u < %u\n", len, hdr_len + lpdu_cnt + 2);
		goto end;
	}

	if(mpdu_fcs_check(buf, hdr_len)) {
		mpdu_header.crc_ok = true;
	} else {
		goto end;
	}

	mpdu_header.gs_id = buf[1] & 0x7f;
	mpdu_header.aircraft_id = buf[2];
end:
	if(mpdu != NULL) {
		mpdu->header = mpdu_header;
	}
	debug_print(D_PROTO, "crc: %d ac_id: %hhu gs_id: %hhu\n",
			mpdu->header.crc_ok, mpdu->header.aircraft_id, mpdu_header.gs_id);
	return lpdu_list;
}

static bool mpdu_fcs_check(uint8_t *buf, uint32_t hdr_len) {
	uint16_t fcs_check = buf[hdr_len] | (buf[hdr_len + 1] << 8);
	uint16_t fcs_computed = crc16_ccitt(buf, hdr_len, 0xFFFFu) ^ 0xFFFFu;
	debug_print(D_PROTO, "FCS: computed: 0x%04x check: 0x%04x\n",
			fcs_computed, fcs_check);
	if(fcs_check != fcs_computed) {
		debug_print(D_PROTO, "FCS check failed\n");
		return false;
	}
	debug_print(D_PROTO, "FCS check OK\n");
	return true;
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
