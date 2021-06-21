/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <libacars/libacars.h>          // la_proto_node

la_proto_node *hfnpdu_parse(uint8_t *buf, uint32_t len);
