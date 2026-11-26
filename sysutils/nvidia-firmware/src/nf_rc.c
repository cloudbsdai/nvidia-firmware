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

#include <errno.h>
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
 * One line of the configuration.  The line is read when it names the
 * setting and when it has a value: the value is told to the caller.
 */
static void
nf_rc_line(char *line, const char *name, int *found)
{
	char *p = line;
	char *value;
	char *end;
	size_t namelen = strlen(name);
	int on;

	while (*p == ' ' || *p == '\t')
		p++;
	if (strncmp(p, name, namelen) != 0)
		return;
	p += namelen;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != '=')
		return;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	value = p;
	if (*value == '"') {
		value++;
		end = strchr(value, '"');
		if (end == NULL)
			return;
		*end = '\0';
	} else {
		end = value;
		while (*end != '\0' && *end != ' ' && *end != '\t' &&
		    *end != '#')
			end++;
		*end = '\0';
	}
	on = nf_rc_value(value);
	if (on < 0)
		return;
	*found = on;
}

/*
 * Read one file of the configuration.  Every setting that is in the file
 * is read, and the last one wins: that is what rc.conf does with a name
 * that is given twice.
 *
 * A file that is not there is not an error: the setting is simply not
 * in it.  A file that is there and cannot be read is an error, and so is
 * a file whose reading fails halfway.  The caller cannot tell that the
 * setting is off when it cannot read the file, and it has to keep the
 * images then.
 */
static int
nf_rc_scan(const char *path, const char *name, int *found)
{
	char line[1024];
	FILE *fp;

	if (nf_test_denied(path)) {
		errno = EACCES;
		return (-1);
	}
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			return (0);
		return (-1);
	}
	while (nf_read_line(fp, line, sizeof(line)) == 0)
		nf_rc_line(line, name, found);
	if (ferror(fp)) {
		fclose(fp);
		return (-1);
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
nf_rc_no_delete(const char *rcconf, const char *rcconf_d, int *no_delete)
{
	int found = 0;

	*no_delete = 0;
	if (rcconf != NULL && nf_rc_scan(rcconf, NF_RC_NO_DELETE, &found) != 0)
		return (-1);
	if (rcconf_d != NULL &&
	    nf_rc_scan(rcconf_d, NF_RC_NO_DELETE, &found) != 0)
		return (-1);
	*no_delete = found;
	return (0);
}
