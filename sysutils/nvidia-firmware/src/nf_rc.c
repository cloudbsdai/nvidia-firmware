/*-
 * Copyright (c) 2026 REVYTECH, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>

#include "nf_rc.h"
#include "nf_util.h"

/*
 * The value of a setting: 1 when it is on, 0 when it is off.  A value
 * that is neither is not a setting at all.
 */
static int
nf_rc_value(const char *value)
{
	if (value == NULL)
		return (-1);
	if (strcasecmp(value, "YES") == 0 || strcasecmp(value, "TRUE") == 0 ||
	    strcmp(value, "1") == 0)
		return (1);
	if (strcasecmp(value, "NO") == 0 || strcasecmp(value, "FALSE") == 0 ||
	    strcmp(value, "0") == 0)
		return (0);
	return (-1);
}

/*
 * Read one file of the configuration.  Every setting that is in the file
 * is read, and the last one wins: that is what rc.conf does with a name
 * that is given twice.
 */
static int
nf_rc_scan(const char *path, const char *name, int *found)
{
	char line[1024];
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (0);
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		char *p = line;
		char *value;
		size_t namelen = strlen(name);
		int on;

		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, name, namelen) != 0)
			continue;
		p += namelen;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '=')
			continue;
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		value = p;
		if (*value == '"') {
			char *end;

			value++;
			end = strchr(value, '"');
			if (end == NULL)
				continue;
			*end = '\0';
		} else {
			char *end;

			end = value;
			while (*end != '\0' && *end != ' ' && *end != '\t' &&
			    *end != '#')
				end++;
			*end = '\0';
		}
		on = nf_rc_value(value);
		if (on < 0)
			continue;
		*found = on;
	}
	fclose(fp);
	return (0);
}

/*
 * The setting of the configuration.  The file of rc.conf.d is read
 * after rc.conf, so that it has the last word: that is the order in
 * which the system reads them.
 */
int
nf_rc_no_delete(const char *rcconf, const char *rcconf_d)
{
	int found = 0;

	if (rcconf != NULL)
		nf_rc_scan(rcconf, NF_RC_NO_DELETE, &found);
	if (rcconf_d != NULL)
		nf_rc_scan(rcconf_d, NF_RC_NO_DELETE, &found);
	return (found);
}
