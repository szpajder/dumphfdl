/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_type_descriptor, la_proto_node, LA_MSG_DIR_*
#include <libacars/acars.h>         // la_acars_parse
#include <libacars/dict.h>          // la_dict
#include "hfdl.h"                   // enum hfdl_pdu_direction
#include "util.h"                   // ASSERT, NEW, XCALLOC, XFREE

// HFNPDU types
#define SYSTEM_TABLE            0xD0
#define PERFORMANCE_DATA        0xD1
#define SYSTEM_TABLE_REQUEST    0xD2
#define FREQUENCY_DATA          0xD5
#define DELAYED_ECHO            0xDE
#define ENVELOPED_DATA          0xFF

struct hfdl_hfnpdu {
	int32_t type;
	bool err;
};

la_dict const hfnpdu_type_descriptions[] = {
	{ .id = SYSTEM_TABLE,         .val = "System table" },
	{ .id = PERFORMANCE_DATA,     .val = "Performance data" },
	{ .id = SYSTEM_TABLE_REQUEST, .val = "System table request" },
	{ .id = FREQUENCY_DATA,       .val = "Frequency data" },
	{ .id = DELAYED_ECHO,         .val = "Delayed echo" },
	{ .id = ENVELOPED_DATA,       .val = "Enveloped data" },
	{ .id = 0,                    .val = NULL }
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_hfnpdu;

la_proto_node *hfnpdu_parse(uint8_t *buf, uint32_t len, enum hfdl_pdu_direction direction) {
	ASSERT(buf);

	if(len == 0) {
		return NULL;
	}
	if(buf[0] != 0xFF) {
		debug_print(D_PROTO, "Not a HFNPDU\n");
		return unknown_proto_pdu_new(buf, len);
	}
	if(len < 2) {
		debug_print(D_PROTO, "Too short: %u < 2\n", len);
		return NULL;
	}

	NEW(struct hfdl_hfnpdu, hfnpdu);
	la_proto_node *node = la_proto_node_new();
	node->td = &proto_DEF_hfdl_hfnpdu;
	node->data = hfnpdu;

	int32_t consumed_len = 0;
	hfnpdu->type = buf[1];
	switch(hfnpdu->type) {
		case SYSTEM_TABLE:
			break;
		case PERFORMANCE_DATA:
			break;
		case SYSTEM_TABLE_REQUEST:
			break;
		case FREQUENCY_DATA:
			break;
		case DELAYED_ECHO:
			break;
		case ENVELOPED_DATA:
			if(len > 2 && buf[2] == 1) {        // ACARS SOH byte
				node->next = la_acars_parse(buf + 3, len - 3,
						direction == UPLINK_PDU ? LA_MSG_DIR_GND2AIR : LA_MSG_DIR_AIR2GND);
			} else {
				node->next = unknown_proto_pdu_new(buf + 3, len - 3);
			}
			break;
		default:
			break;
	}
	if(consumed_len < 0) {
		hfnpdu->err = true;
	}
	return node;
}

static void hfnpdu_destroy(void *data) {
	if(data == NULL) {
		return;
	}
	struct hfdl_hfnpdu *hfnpdu = data;
	XFREE(hfnpdu);
}

static void hfnpdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_hfnpdu const *hfnpdu = data;
	if(hfnpdu->err) {
		LA_ISPRINTF(vstr, indent, "-- Unparseable HFNPDU\n");
		return;
	}
	char const *hfnpdu_type = la_dict_search(hfnpdu_type_descriptions, hfnpdu->type);
	if(hfnpdu_type != NULL) {
		LA_ISPRINTF(vstr, indent, "%s:\n", hfnpdu_type);
	} else {
		LA_ISPRINTF(vstr, indent, "Unknown HFNPDU type (0x%02x):\n", hfnpdu->type);
	}
	indent++;
	switch(hfnpdu->type) {
		case SYSTEM_TABLE:
			break;
		case PERFORMANCE_DATA:
			break;
		case SYSTEM_TABLE_REQUEST:
			break;
		case FREQUENCY_DATA:
			break;
		case DELAYED_ECHO:
			break;
		case ENVELOPED_DATA:
			break;
		default:
			break;
	}
}

la_type_descriptor const proto_DEF_hfdl_hfnpdu = {
	.format_text = hfnpdu_format_text,
	.destroy = hfnpdu_destroy
};
