/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_type_descriptor, la_proto_node
#include <libacars/dict.h>          // la_dict
#include "hfdl.h"                   // struct hfdl_pdu_hdr_data
#include "util.h"                   // ASSERT, NEW, XCALLOC, XFREE

// LPDU types
#define UNNUMBERED_DATA             0x0D
#define UNNUMBERED_ACK_DATA         0x1D
#define LOGON_DENIED                0x2F
#define LOGOFF_REQUEST              0x3F
#define LOGON_RESUME_CONFIRM        0x5F
#define LOGON_RESUME                0x4F
#define LOGON_REQUEST_NORMAL        0x8F
#define LOGON_CONFIRM               0x9F
#define LOGON_REQUEST_DLS           0xBF

struct hfdl_lpdu {
	struct octet_string *pdu;
	struct hfdl_pdu_hdr_data mpdu_header;
	int32_t type;
};

la_dict const lpdu_type_descriptions[] = {
	{ .id = UNNUMBERED_DATA,            .val = "Unnumbered data" },
	{ .id = UNNUMBERED_ACK_DATA,        .val = "Unnumbered acknowledged data" },
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

la_proto_node *lpdu_parse(uint8_t *buf, uint32_t len, struct hfdl_pdu_hdr_data mpdu_header) {
	ASSERT(buf);
	ASSERT(len > 0);

	NEW(struct hfdl_lpdu, lpdu);
	lpdu->pdu = octet_string_new(buf, len);
	lpdu->mpdu_header = mpdu_header;
	la_proto_node *node = la_proto_node_new();
	node->td = &proto_DEF_hfdl_lpdu;
	node->data = lpdu;

	lpdu->type = buf[0];
	switch(lpdu->type) {
		case UNNUMBERED_DATA:
			break;
		case UNNUMBERED_ACK_DATA:
			break;
		case LOGON_DENIED:
			break;
		case LOGOFF_REQUEST:
			break;
		case LOGON_RESUME_CONFIRM:
			break;
		case LOGON_RESUME:
			break;
		case LOGON_REQUEST_NORMAL:
			break;
		case LOGON_CONFIRM:
			break;
		case LOGON_REQUEST_DLS:
			break;
		default:
			node->next = unknown_proto_pdu_new(buf, len);
			break;
	}
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
	char const *lpdu_type = la_dict_search(lpdu_type_descriptions, lpdu->type);
	if(lpdu_type != NULL) {
		LA_ISPRINTF(vstr, indent, "%s:\n", lpdu_type);
	} else {
		LA_ISPRINTF(vstr, indent, "Unknown LPDU type (0x%02x):\n", lpdu->type);
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
