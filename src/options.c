/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>          // fprintf()
#include <string.h>         // strlen
#include "options.h"        // USAGE_OPT_NAME_COLWIDTH, USAGE_INDENT_STEP

void describe_option(char const *name, char const *description, int indent) {
	int descr_shiftwidth = USAGE_OPT_NAME_COLWIDTH - (int)strlen(name) - indent * USAGE_INDENT_STEP;
	if(descr_shiftwidth < 1) {
		descr_shiftwidth = 1;
	}
	fprintf(stderr, "%*s%s%*s%s\n", IND(indent), "", name, descr_shiftwidth, "", description);
}

