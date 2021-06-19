/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // hfdl_*
#include "util.h"                   // NEW, ASSERT, struct octet_string

struct hfdl_mpdu {
	struct octet_string *pdu;
	struct hfdl_pdu_hdr_data header;
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_mpdu;
static int32_t parse_lpdu_list(uint8_t *lpdu_len_ptr, uint8_t *data_ptr,
		uint8_t *endptr, uint32_t lpdu_cnt, la_list *lpdu_list);

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
	uint32_t aircraft_cnt = 0;
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
		aircraft_cnt = ((buf[0] & 0x70) >> 4) + 1;
		debug_print(D_PROTO, "aircraft_cnt: %u\n", aircraft_cnt);
		hdr_len = 2;                        // P/NAC/T + UTC/GS ID
		for(uint32_t i = 0; i < aircraft_cnt; i++) {
			if(len < hdr_len + 2) {
				debug_print(D_PROTO, "uplink: too short: %u < %u\n", len, hdr_len + 2);
				goto end;
			}
			lpdu_cnt = (buf[hdr_len+1] >> 4) & 0xF;
			hdr_len += 2 + lpdu_cnt;        // aircraft_id + NLP/DDR/P + LPDU size octets (one per LPDU)
			debug_print(D_PROTO, "uplink: ac %u lpdu_cnt: %u hdr_len: %u\n", i, lpdu_cnt, hdr_len);
		}
	}
	debug_print(D_PROTO, "hdr_len: %u\n", hdr_len);
	if(len < hdr_len + 2) {
		debug_print(D_PROTO, "Too short: %u < %u\n", len, hdr_len + lpdu_cnt + 2);
		goto end;
	}

	if(hfdl_pdu_fcs_check(buf, hdr_len)) {
		mpdu_header.crc_ok = true;
	} else {
		goto end;
	}

	uint8_t *dataptr = buf + hdr_len + 2;       // First data octet of the first LPDU
	if(mpdu_header.direction == DOWNLINK_PDU) {
		mpdu_header.src_id = buf[2];
		mpdu_header.dst_id = buf[1] & 0x7f;
		uint8_t *hdrptr = buf + 6;              // First LPDU size octet
		if(parse_lpdu_list(hdrptr, dataptr, buf + len, lpdu_cnt, lpdu_list) < 0) {
			goto end;
		}
	} else {                                    // UPLINK_PDU
		mpdu_header.src_id = buf[1] & 0x7f;
		mpdu_header.dst_id = 0;                 // See comment in mpdu_format_text()
		uint8_t *hdrptr = buf + 2;              // First AC ID octet
		int32_t consumed_octets = 0;
		for(uint32_t i = 0; i < aircraft_cnt; i++, hdrptr++, dataptr += consumed_octets) {
			mpdu_header.dst_id = *hdrptr++;
			lpdu_cnt = (*hdrptr++ >> 4) & 0xF;
			if((consumed_octets = parse_lpdu_list(hdrptr, dataptr, buf + len, lpdu_cnt, lpdu_list)) < 0) {
				goto end;
			}
		}
	}

end:
	if(mpdu != NULL) {
		mpdu->header = mpdu_header;
	}
	return lpdu_list;
}

static int32_t parse_lpdu_list(uint8_t *lpdu_len_ptr, uint8_t *data_ptr,
		uint8_t *endptr, uint32_t lpdu_cnt, la_list *lpdu_list) {
	int32_t consumed_octets = 0;
	for(uint32_t j = 0; j < lpdu_cnt; j++) {
		uint32_t lpdu_len = *lpdu_len_ptr + 1;
		if(data_ptr + lpdu_len <= endptr) {
			debug_print(D_PROTO, "lpdu %u/%u: lpdu_len=%u\n", j + 1, lpdu_cnt, lpdu_len);
			//lpdu_list = lpdu_parse(data_ptr, lpdu_len, mpdu_header, lpdu_list);
			data_ptr += lpdu_len;              // Move to the next LPDU
			consumed_octets += lpdu_len;
			lpdu_len_ptr++;
		} else {
			debug_print(D_PROTO, "lpdu %u/%u truncated: end is %td octets past buffer\n",
					j + 1, lpdu_cnt, data_ptr + lpdu_len - endptr);
			return -1;
		}
	}
	return consumed_octets;
}

static void mpdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_mpdu const *mpdu = data;
	if(Config.output_raw_frames == true && mpdu->pdu->len > 0) {
		append_hexdump_with_indent(vstr, mpdu->pdu->buf, mpdu->pdu->len, indent+1);
	}
	if(!mpdu->header.crc_ok) {
		LA_ISPRINTF(vstr, indent, "-- CRC check failed\n");
		return;
	}
	if(mpdu->header.direction == UPLINK_PDU) {
		LA_ISPRINTF(vstr, indent, "Uplink MPDU:\n");
			indent++;
			LA_ISPRINTF(vstr, indent, "Src GS: %hhu\n", mpdu->header.src_id);
			// Dst AC is meaningless here, because MPDU may contain several
			// LPDUs and each one may have a different destination.
			// Dst AC ID will therefore be printed in the header of each LPDU.
	} else {
		LA_ISPRINTF(vstr, indent, "Downlink MPDU:\n");
			indent++;
			LA_ISPRINTF(vstr, indent, "Src AC: %hhu\n", mpdu->header.src_id);
			LA_ISPRINTF(vstr, indent, "Dst GS: %hhu\n", mpdu->header.dst_id);
	}
}

la_type_descriptor const proto_DEF_hfdl_mpdu = {
	.format_text = mpdu_format_text,
	.destroy = NULL
};
