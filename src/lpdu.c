/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_type_descriptor, la_proto_node
#include <libacars/dict.h>          // la_dict
#include "hfdl.h"                   // struct hfdl_pdu_hdr_data, hfdl_pdu_fcs_check
#include "util.h"                   // ASSERT, NEW, XCALLOC, XFREE

// LPDU types
#define UNNUMBERED_DATA             0x0D
#define UNNUMBERED_ACKED_DATA       0x1D
#define LOGON_DENIED                0x2F
#define LOGOFF_REQUEST              0x3F
#define LOGON_RESUME_CONFIRM        0x5F
#define LOGON_RESUME                0x4F
#define LOGON_REQUEST_NORMAL        0x8F
#define LOGON_CONFIRM               0x9F
#define LOGON_REQUEST_DLS           0xBF

struct lpdu_logon_confirm {
	uint32_t icao_address;
	uint8_t ac_id;
};

struct hfdl_lpdu {
	struct octet_string *pdu;
	struct hfdl_pdu_hdr_data mpdu_header;
	int32_t type;
	bool crc_ok;
	bool err;
	union {
		struct lpdu_logon_confirm logon_confirm;
	} data;
};

la_dict const lpdu_type_descriptions[] = {
	{ .id = UNNUMBERED_DATA,            .val = "Unnumbered data" },
	{ .id = UNNUMBERED_ACKED_DATA,      .val = "Unnumbered ack'ed data" },
	{ .id = LOGON_DENIED,               .val = "Logon denied" },
	{ .id = LOGOFF_REQUEST,             .val = "Logoff request" },
	{ .id = LOGON_RESUME_CONFIRM,       .val = "Logon resume confirm" },
	{ .id = LOGON_RESUME,               .val = "Logon resume" },
	{ .id = LOGON_REQUEST_NORMAL,       .val = "Logon request (normal)" },
	{ .id = LOGON_CONFIRM,              .val = "Logon confirm" },
	{ .id = LOGON_REQUEST_DLS,          .val = "Logon request (DLS)" },
	{ .id = 0,                          .val = 0 }
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_lpdu;

static int32_t logon_confirm_parse(uint8_t *buf, uint32_t len, struct lpdu_logon_confirm *result) {
#define LOGON_CONFIRM_LPDU_LEN 8
	ASSERT(buf);
	ASSERT(result);

	if(len < 8) {
		return -1;
	}
	result->icao_address = parse_icao_hex(buf + 1);
	result->ac_id = buf[4];
	return LOGON_CONFIRM_LPDU_LEN;
}

la_proto_node *lpdu_parse(uint8_t *buf, uint32_t len, struct hfdl_pdu_hdr_data mpdu_header) {
	ASSERT(buf);

	if(len < 3) {       // Need at least LPDU type + FCS
		debug_print(D_PROTO, "Too short: %u < 3\n", len);
		return NULL;
	}

	NEW(struct hfdl_lpdu, lpdu);
	lpdu->pdu = octet_string_new(buf, len);
	lpdu->mpdu_header = mpdu_header;
	la_proto_node *node = la_proto_node_new();
	node->td = &proto_DEF_hfdl_lpdu;
	node->data = lpdu;

	len -= 2;           // Strip FCS
	lpdu->crc_ok = hfdl_pdu_fcs_check(buf, len);
	if(!lpdu->crc_ok) {
		goto end;
	}

	int32_t consumed_len = 0;
	lpdu->type = buf[0];
	switch(lpdu->type) {
		case UNNUMBERED_DATA:
			break;
		case UNNUMBERED_ACKED_DATA:
			break;
		case LOGON_DENIED:
			break;
		case LOGOFF_REQUEST:
			break;
		case LOGON_CONFIRM:
		case LOGON_RESUME_CONFIRM:
			consumed_len = logon_confirm_parse(buf, len, &lpdu->data.logon_confirm);
			break;
		case LOGON_RESUME:
			break;
		case LOGON_REQUEST_NORMAL:
			break;
		case LOGON_REQUEST_DLS:
			break;
		default:
			node->next = unknown_proto_pdu_new(buf, len);
			break;
	}
	if(consumed_len < 0) {
		lpdu->err = true;
	}
end:
	return node;
}

static void lpdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_lpdu const *lpdu = data;
	if(Config.output_raw_frames == true) {
		append_hexdump_with_indent(vstr, lpdu->pdu->buf, lpdu->pdu->len, indent+1);
	}
	if(!lpdu->crc_ok) {
		LA_ISPRINTF(vstr, indent, "LPDU:\n");
		LA_ISPRINTF(vstr, indent+1, "-- CRC check failed");
		return;
	}
	// TODO: check error flag
	char const *lpdu_type = la_dict_search(lpdu_type_descriptions, lpdu->type);
	if(lpdu_type != NULL) {
		LA_ISPRINTF(vstr, indent, "%s:\n", lpdu_type);
	} else {
		LA_ISPRINTF(vstr, indent, "Unknown LPDU type (0x%02x):\n", lpdu->type);
	}
	indent++;
	switch(lpdu->type) {
		case LOGON_CONFIRM:
		case LOGON_RESUME_CONFIRM:
			LA_ISPRINTF(vstr, indent, "ICAO: %06X\n", lpdu->data.logon_confirm.icao_address);
			LA_ISPRINTF(vstr, indent, "Assigned AC ID: %u\n", lpdu->data.logon_confirm.ac_id);
			break;
		default:
			return;
	}
}

static void lpdu_destroy(void *data) {
	if(data == NULL) {
		return;
	}
	struct hfdl_lpdu *lpdu = data;
	// No octet_string_destroy() here, because it frees the buffer, which is
	// invalid here (pdu->buf is a pointer into an existing MPDU buffer)
	XFREE(lpdu->pdu);
	XFREE(lpdu);
}

la_type_descriptor const proto_DEF_hfdl_lpdu = {
	.format_text = lpdu_format_text,
	.destroy = lpdu_destroy
};
