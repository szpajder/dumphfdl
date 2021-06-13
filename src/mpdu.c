/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // struct hfdl_pdu_metadata
#include "mpdu.h"                   // mpdu_direction
#include "util.h"                   // NEW, ASSERT, struct octet_string
#include "crc.h"                    // crc16_ccitt

struct hfdl_mpdu {
	struct octet_string *pdu;
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_mpdu;
static la_list *mpdu_parse_uplink(struct octet_string *pdu,
		struct hfdl_pdu_metadata *metadata, la_reasm_ctx *reasm_ctx,
		la_list *lpdu_list);
static la_list *mpdu_parse_downlink(struct octet_string *pdu,
		struct hfdl_pdu_metadata *metadata, la_reasm_ctx *reasm_ctx,
		la_list *lpdu_list);
static bool mpdu_fcs_check(uint8_t *buf, uint32_t hdr_len);

la_list *mpdu_parse(struct octet_string *pdu, struct hfdl_pdu_metadata *metadata,
		la_reasm_ctx *reasm_ctx) {
	ASSERT(pdu);
	ASSERT(pdu->buf);
	ASSERT(pdu->len > 0);

	la_list *lpdu_list = NULL;
	// If raw frame output has been requested by the user,
	// then append a hfdl_mpdu node as a first entry on the list,
	// so that mpdu_format_text() could print it as hex.
	// If --raw-frames is disabled, then this node is omitted.
	if(Config.output_raw_frames == true) {
		NEW(struct hfdl_mpdu, mpdu);
		mpdu->pdu = pdu;
		la_proto_node *node = la_proto_node_new();
		node->data = mpdu;
		node->td = &proto_DEF_hfdl_mpdu;
		lpdu_list = la_list_append(lpdu_list, node);
	}
	enum mpdu_direction direction = pdu->buf[0] & 0x2;
	lpdu_list = (direction == UPLINK_MPDU ?
			mpdu_parse_uplink(pdu, metadata, reasm_ctx, lpdu_list) :
			mpdu_parse_downlink(pdu, metadata, reasm_ctx, lpdu_list));
	return lpdu_list;
}

static la_list *mpdu_parse_uplink(struct octet_string *pdu,
		struct hfdl_pdu_metadata *metadata, la_reasm_ctx *reasm_ctx,
		la_list *lpdu_list) {
	uint8_t *buf = pdu->buf;
	uint32_t len = pdu->len;

	if(len < 4) {
		goto end;
	}
	uint32_t lpdu_cnt = (buf[3] >> 4) & 0xF;
	uint32_t hdr_len = 4 + lpdu_cnt;    // header length, without FCS
	if(len < hdr_len + lpdu_cnt + 2) {
		debug_print(D_PROTO, "Too short: %u < %u\n", len, hdr_len + lpdu_cnt + 2);
		goto end;
	}
	if(!mpdu_fcs_check(buf, hdr_len)) {
		goto end;
	}
	metadata->crc_ok = true;

end:
	return lpdu_list;
}

static la_list *mpdu_parse_downlink(struct octet_string *pdu,
		struct hfdl_pdu_metadata *metadata, la_reasm_ctx *reasm_ctx,
		la_list *lpdu_list) {
	uint8_t *buf = pdu->buf;
	uint32_t len = pdu->len;

	uint32_t lpdu_cnt = (buf[0] >> 2) & 0xF;
	uint32_t hdr_len = 6 + lpdu_cnt;     // header length, without FCS
	if(len < hdr_len + lpdu_cnt + 2) {
		debug_print(D_PROTO, "Too short: %u < %u\n", len, hdr_len + lpdu_cnt + 2);
		goto end;
	}
	if(!mpdu_fcs_check(buf, hdr_len)) {
		goto end;
	}
	metadata->crc_ok = true;

end:
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
