/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <libacars/libacars.h>      // la_proto_node
#include <libacars/list.h>          // la_list
#include "hfdl.h"                   // hfdl_*
#include "util.h"                   // NEW, ASSERT, struct octet_string
#include "crc.h"                    // crc16_ccitt

#define SPDU_LEN 66
#define GS_STATUS_CNT 3
#define GS_STATUS_FREQ_CNT 20

struct gs_status {
	uint32_t freqs_in_use;
	uint8_t id;
	bool utc_sync;
};

struct hfdl_spdu {
	struct octet_string *pdu;
	struct hfdl_pdu_hdr_data header;
	struct gs_status gs_data[GS_STATUS_CNT];
	uint32_t frame_index;
	uint8_t frame_offset;
	uint8_t version;
	uint8_t change_note;
	uint8_t min_priority;
	uint8_t systable_version;
	bool rls_in_use;
	bool iso8208_supported;
};

// Forward declarations
la_type_descriptor const proto_DEF_hfdl_spdu;
static void gs_status_format_text(la_vstring *vstr, int32_t indent, struct gs_status const *gs);
static void freq_list_format_text(la_vstring *vstr, int32_t indent, char const *label, uint32_t freqs);

la_list *spdu_parse(struct octet_string *pdu) {
	ASSERT(pdu);
	ASSERT(pdu->buf);
	ASSERT(pdu->len > 0);

	la_list *spdu_list = NULL;
	if(pdu->len < SPDU_LEN) {
		debug_print(D_PROTO, "Too short: %zu < %u\n", pdu->len, SPDU_LEN);
		goto end;
	}

	NEW(struct hfdl_spdu, spdu);
	spdu->pdu = pdu;
	la_proto_node *node = la_proto_node_new();
	node->data = spdu;
	node->td = &proto_DEF_hfdl_spdu;
	spdu_list = la_list_append(spdu_list, node);

	if(hfdl_pdu_fcs_check(pdu->buf, 64u)) {
		spdu->header.crc_ok = true;
	} else {
		goto end;
	}
	uint8_t *buf = pdu->buf;
	spdu->header.direction = UPLINK_PDU;
	spdu->header.src_id = buf[1] & 0x7F;

	spdu->rls_in_use = buf[0] & 2;
	spdu->version = (buf[0] >> 2) & 3;
	spdu->iso8208_supported = buf[0] & 0x20;
	spdu->change_note = (buf[0] & 0xC0) >> 6;

	spdu->frame_index = buf[2] | ((buf[3] & 0xF) << 8);
	spdu->frame_offset = (buf[3] >> 4) & 0xF;

	spdu->min_priority = buf[52] & 0xF;
	spdu->systable_version = buf[53] | ((buf[54] & 0xF) << 8);

	spdu->gs_data[0].id = spdu->header.src_id;
	spdu->gs_data[0].utc_sync = buf[1] & 0x80;
	spdu->gs_data[0].freqs_in_use = (buf[54] & 0xF0) >> 4 | buf[55] << 4 | buf[56] << 12;
	debug_print(D_PROTO, "gs_data: id %hhu utc %d freqs_in_use 0x%05x\n",
			spdu->gs_data[0].id, spdu->gs_data[0].utc_sync, spdu->gs_data[0].freqs_in_use);

	spdu->gs_data[1].id = buf[57] & 0x7F;
	spdu->gs_data[1].utc_sync = buf[57] & 0x80;
	spdu->gs_data[1].freqs_in_use = buf[58] | buf[59] << 8 | (buf[60] & 0xF) << 16;
	debug_print(D_PROTO, "gs_data: id %hhu utc %d freqs_in_use 0x%05x\n",
			spdu->gs_data[1].id, spdu->gs_data[1].utc_sync, spdu->gs_data[1].freqs_in_use);

	spdu->gs_data[2].id = (buf[60] & 0xF0) >> 4 | (buf[61] & 0x7) << 4;
	spdu->gs_data[2].utc_sync = buf[61] & 0x8;
	spdu->gs_data[2].freqs_in_use = (buf[61] & 0xF0) >> 4 | buf[62] << 4 | buf[63] << 12;
	debug_print(D_PROTO, "gs_data: id %hhu utc %d freqs_in_use 0x%05x\n",
			spdu->gs_data[2].id, spdu->gs_data[2].utc_sync, spdu->gs_data[2].freqs_in_use);

end:
	return spdu_list;
}

static char const *change_note_descr[] = {
	[0] = "None",
	[1] = "Channel down",
	[2] = "Upcoming frequency change",
	[3] = "Ground station down"
};

static void spdu_format_text(la_vstring *vstr, void const *data, int indent) {
	ASSERT(vstr != NULL);
	ASSERT(data);
	ASSERT(indent >= 0);

	struct hfdl_spdu const *spdu = data;
	if(Config.output_raw_frames == true && spdu->pdu->len > 0) {
		append_hexdump_with_indent(vstr, spdu->pdu->buf, spdu->pdu->len, indent+1);
	}
	if(!spdu->header.crc_ok) {
		LA_ISPRINTF(vstr, indent, "-- CRC check failed\n");
		return;
	}

	LA_ISPRINTF(vstr, indent, "Uplink SPDU:\n");
	indent++;
	LA_ISPRINTF(vstr, indent, "Src GS: %d\n", spdu->header.src_id);
	LA_ISPRINTF(vstr, indent, "Squitter: ver: %hhu rls: %d iso: %d\n",
			spdu->version, spdu->rls_in_use, spdu->iso8208_supported);
	indent++;
	LA_ISPRINTF(vstr, indent, "Change note: %s\n", change_note_descr[spdu->change_note]);
	LA_ISPRINTF(vstr, indent, "TDMA Frame: index: %u offset: %u\n", spdu->frame_index, spdu->frame_offset);
	LA_ISPRINTF(vstr, indent, "Minimum priority: %u\n", spdu->min_priority);
	LA_ISPRINTF(vstr, indent, "System table version: %u\n", spdu->systable_version);
	LA_ISPRINTF(vstr, indent, "Ground station status:\n");
	for(int32_t i = 0; i < GS_STATUS_CNT; i++) {
		gs_status_format_text(vstr, indent, &spdu->gs_data[i]);
	}
	indent--;
}

static void gs_status_format_text(la_vstring *vstr, int32_t indent, struct gs_status const *gs) {
	ASSERT(vstr);
	ASSERT(indent >= 0);
	ASSERT(gs);

	LA_ISPRINTF(vstr, indent, "ID: %hhu\n", gs->id);
	indent++;
	LA_ISPRINTF(vstr, indent, "UTC sync: %d\n", gs->utc_sync);
	freq_list_format_text(vstr, indent, "Frequencies in use", gs->freqs_in_use);
}

static void freq_list_format_text(la_vstring *vstr, int32_t indent, char const *label, uint32_t freqs) {
	ASSERT(vstr);
	ASSERT(indent >= 0);
	ASSERT(label);

	LA_ISPRINTF(vstr, indent, "%s: ", label);
	bool first = true;
	for(int32_t i = 0; i < GS_STATUS_FREQ_CNT; i++) {
		if((freqs >> i) & 1) {
			la_vstring_append_sprintf(vstr, "%s%d", first ? "" : ", ", i);
			first = false;
		}
	}
	EOL(vstr);
}

la_type_descriptor const proto_DEF_hfdl_spdu = {
	.format_text = spdu_format_text,
	.destroy = NULL
};
