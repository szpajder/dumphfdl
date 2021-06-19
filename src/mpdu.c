/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/reassembly.h>    // la_reasm_ctx
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // hfdl_*
#include "util.h"                   // NEW, ASSERT, struct octet_string

struct hfdl_mpdu {
	struct octet_string *pdu;
	la_list *dst_aircraft;                  // List of destination aircraft (for multi-LPDU uplinks)
	struct hfdl_pdu_hdr_data header;
};

struct mpdu_dst {
	uint8_t dst_id;
	uint8_t lpdu_cnt;
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
	NEW(struct hfdl_mpdu, mpdu);
	mpdu->pdu = pdu;
	la_proto_node *node = la_proto_node_new();
	node->data = mpdu;
	node->td = &proto_DEF_hfdl_mpdu;
	lpdu_list = la_list_append(lpdu_list, node);

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
			NEW(struct mpdu_dst, dst_ac);
			dst_ac->dst_id = *hdrptr++;
			dst_ac->lpdu_cnt = (*hdrptr++ >> 4) & 0xF;
			mpdu->dst_aircraft = la_list_append(mpdu->dst_aircraft, dst_ac);
			if((consumed_octets = parse_lpdu_list(hdrptr, dataptr, buf + len, dst_ac->lpdu_cnt, lpdu_list)) < 0) {
				goto end;
			}
		}
	}

end:
	mpdu->header = mpdu_header;
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
	if(Config.output_raw_frames == true) {
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
			int32_t i = 1;
			for(la_list *ac = mpdu->dst_aircraft; ac != NULL; ac = la_list_next(ac)) {
				struct mpdu_dst *dst = ac->data;
				LA_ISPRINTF(vstr, indent, "Dst AC #%d: %hhu\n", i, dst->dst_id);
				LA_ISPRINTF(vstr, indent+1, "LPDU count: %hhu\n", dst->lpdu_cnt);
			}
	} else {
		LA_ISPRINTF(vstr, indent, "Downlink MPDU:\n");
			indent++;
			LA_ISPRINTF(vstr, indent, "Src AC: %hhu\n", mpdu->header.src_id);
			LA_ISPRINTF(vstr, indent, "Dst GS: %hhu\n", mpdu->header.dst_id);
	}
}

static void mpdu_destroy(void *data) {
	if(data == NULL) {
		return;
	}
	struct hfdl_mpdu *mpdu = data;
	la_list_free(mpdu->dst_aircraft);
	XFREE(mpdu);
}

la_type_descriptor const proto_DEF_hfdl_mpdu = {
	.format_text = mpdu_format_text,
	.destroy = mpdu_destroy
};
