/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include <libacars/libacars.h>          // la_proto_node
#include "position.h"
#include "util.h"

struct position_info *position_info_create(void) {
	NEW(struct position_info, pos_info);
	return pos_info;
}

struct position_info *position_info_extract(la_proto_node *tree) {
	return NULL;
}

void position_info_destroy(struct position_info *pos_info) {
	XFREE(pos_info);
}
