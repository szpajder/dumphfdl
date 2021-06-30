/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stdbool.h>

typedef struct systable systable;

systable *systable_create(char const *savefile);
bool systable_read_from_file(systable *st, char const *file);
char const *systable_error_text(systable const *st);
int32_t systable_get_version(systable const *st);
char const *systable_get_station_name(systable const *st, int32_t id);
double systable_get_station_frequency(systable const *st, int32_t gs_id, int32_t freq_id);
bool systable_is_available(systable const *st);
void systable_destroy(systable *st);
