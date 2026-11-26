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

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nf_pin.h"

/* Is this a driver version?  A version is a number with dots in it, such
 * as 580.178.04.  A name with another shape is not a version. */
static int
nf_pin_is_version(const char *name)
{
	size_t i;
	int digits = 0;

	if (name == NULL || *name == '\0')
		return (0);
	for (i = 0; name[i] != '\0'; i++) {
		if (name[i] >= '0' && name[i] <= '9') {
			digits++;
			continue;
		}
		if (name[i] == '.')
			continue;
		return (0);
	}
	return (digits > 0);
}

/* Is this a hash?  Sixty-four hexadecimal digits. */
static int
nf_pin_is_sha(const char *sha)
{
	size_t i;

	if (sha == NULL)
		return (0);
	for (i = 0; i < NF_SHA256_LEN - 1; i++) {
		char c = sha[i];

		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		    (c >= 'A' && c <= 'F'))
			continue;
		return (0);
	}
	return (sha[NF_SHA256_LEN - 1] == '\0');
}

/*
 * One line of the table.  Returns 1 when the line holds an entry, 0 when
 * it holds nothing and -1 when the line is broken.
 */
static int
nf_pin_line(struct nf_pin *pin, char *line, int lineno)
{
	char *fields[5];
	char *end;
	long long size;
	size_t n;

	/*
	 * The line has four fields and no more: a fifth field is a field
	 * that is not read, and the table of the package and the table
	 * that the README is made of have to read the same lines the same
	 * way.
	 */
	n = nf_split_fields(line, fields, 5);
	if (n == 0)
		return (0);
	if (n < 4) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "a line is a version, a file, a size and a hash\n", lineno);
		return (-1);
	}
	if (n > 4) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "a line holds more than four fields\n", lineno);
		return (-1);
	}
	if (!nf_pin_is_version(fields[0])) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "%s is not a driver version\n", lineno, fields[0]);
		return (-1);
	}
	if (!nf_valid_image_name(fields[1])) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "%s is not the name of a file\n", lineno, fields[1]);
		return (-1);
	}
	errno = 0;
	size = strtoll(fields[2], &end, 10);
	if (errno != 0 || end == fields[2] || *end != '\0' || size < 0) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "%s is not a size\n", lineno, fields[2]);
		return (-1);
	}
	if (!nf_pin_is_sha(fields[3])) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "%s is not a hash\n", lineno, fields[3]);
		return (-1);
	}
	if (nf_pin_image(pin, fields[0], fields[1]) != NULL) {
		fprintf(stderr, "nvidia-firmware: the pin table: line %d: "
		    "%s/%s is pinned twice\n", lineno, fields[0], fields[1]);
		return (-1);
	}
	if (nf_image_add(&pin->images, &pin->nimages, fields[0], fields[1],
	    fields[3], (off_t)size, 0) != 0)
		return (-1);
	return (1);
}

/*
 * The versions of the table, the newest last.  The list is made of the
 * names of the versions that the table holds, so that a version that is
 * named twice is named once.
 */
static int
nf_pin_version_cmp(const void *a, const void *b)
{
	const char *const *va = a;
	const char *const *vb = b;

	return (nf_version_cmp(*va, *vb));
}

static int
nf_pin_add_version(struct nf_pin *pin, const char *version)
{
	size_t i;

	for (i = 0; i < pin->nversions; i++) {
		if (strcmp(pin->versions[i], version) == 0)
			return (0);
	}
	{
		char **v = realloc(pin->versions,
		    (pin->nversions + 1) * sizeof(*v));

		if (v == NULL)
			return (-1);
		pin->versions = v;
		pin->versions[pin->nversions] = nf_strdup(version);
		if (pin->versions[pin->nversions] == NULL)
			return (-1);
		pin->nversions++;
	}
	qsort(pin->versions, pin->nversions, sizeof(*pin->versions),
	    nf_pin_version_cmp);
	return (0);
}

int
nf_pin_load(const char *path, struct nf_pin *pin)
{
	char line[1024];
	FILE *fp;
	int lineno = 0;
	size_t i;

	memset(pin, 0, sizeof(*pin));
	fp = fopen(path, "r");
	if (fp == NULL)
		return (-1);
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		lineno++;
		if (nf_pin_line(pin, line, lineno) < 0) {
			fclose(fp);
			nf_pin_free(pin);
			return (-1);
		}
	}
	if (ferror(fp)) {
		fclose(fp);
		nf_pin_free(pin);
		return (-1);
	}
	fclose(fp);
	for (i = 0; i < pin->nimages; i++) {
		if (nf_pin_add_version(pin, pin->images[i].version) != 0) {
			nf_pin_free(pin);
			return (-1);
		}
	}
	return (0);
}

void
nf_pin_free(struct nf_pin *pin)
{
	size_t i;

	for (i = 0; i < pin->nversions; i++)
		free(pin->versions[i]);
	free(pin->versions);
	free(pin->images);
	memset(pin, 0, sizeof(*pin));
}

const struct nf_image *
nf_pin_image(const struct nf_pin *pin, const char *version, const char *name)
{
	size_t i;

	if (version == NULL || name == NULL)
		return (NULL);
	for (i = 0; i < pin->nimages; i++) {
		if (strcmp(pin->images[i].version, version) != 0)
			continue;
		if (strcmp(pin->images[i].name, name) != 0)
			continue;
		return (&pin->images[i]);
	}
	return (NULL);
}

int
nf_pin_has_version(const struct nf_pin *pin, const char *version)
{
	size_t i;

	if (version == NULL)
		return (0);
	for (i = 0; i < pin->nversions; i++) {
		if (strcmp(pin->versions[i], version) == 0)
			return (1);
	}
	return (0);
}

const char *
nf_pin_version(const struct nf_pin *pin, size_t i)
{
	if (i >= pin->nversions)
		return (NULL);
	return (pin->versions[i]);
}

const char *
nf_pin_newest(const struct nf_pin *pin)
{
	if (pin->nversions == 0)
		return (NULL);
	return (pin->versions[pin->nversions - 1]);
}

int
nf_pin_images(const struct nf_pin *pin, const char *version,
    struct nf_strlist *images)
{
	size_t i;

	nf_strlist_init(images);
	for (i = 0; i < pin->nimages; i++) {
		if (strcmp(pin->images[i].version, version) != 0)
			continue;
		if (nf_strlist_add(images, pin->images[i].name) != 0) {
			nf_strlist_free(images);
			return (-1);
		}
	}
	return (0);
}
