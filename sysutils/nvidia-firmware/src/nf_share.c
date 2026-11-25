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
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nf_share.h"

static int
nf_share_image_cmp(const void *a, const void *b)
{
	const struct nf_share_image *ia = a;
	const struct nf_share_image *ib = b;

	return (strcmp(ia->name, ib->name));
}

static void
nf_share_sort(struct nf_share_version *v)
{
	if (v->nimages > 1)
		qsort(v->images, v->nimages, sizeof(*v->images),
		    nf_share_image_cmp);
}

/* The end of the word that starts at p. */
static char *
nf_word_end(char *p)
{
	while (*p != '\0' && *p != ' ' && *p != '\t')
		p++;
	return (p);
}

/*
 * One line of the file of hashes: a hash, two spaces and the name of the
 * image.  Returns 1 when the line holds an entry, 0 when it holds
 * nothing and -1 when there is no memory for the entry.
 */
static int
nf_share_sum_line(struct nf_share_version *v, char *line)
{
	char *p = line;
	char *hash;
	char *name;
	struct nf_share_expected *ex;
	size_t i;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '#' || *p == '\0')
		return (0);
	hash = p;
	p = nf_word_end(p);
	if (*p == '\0')
		return (0);
	*p++ = '\0';
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '*')
		p++;
	name = p;
	p = nf_word_end(p);
	*p = '\0';
	/*
	 * A name that is not one path component is never used in a path;
	 * it is counted and reported.
	 */
	if (!nf_valid_image_name(name)) {
		v->nbadnames++;
		return (1);
	}
	ex = realloc(v->expected, (v->nexpected + 1) * sizeof(*ex));
	if (ex == NULL)
		return (-1);
	v->expected = ex;
	ex = &v->expected[v->nexpected++];
	memset(ex, 0, sizeof(*ex));
	snprintf(ex->name, sizeof(ex->name), "%s", name);
	snprintf(ex->sha, sizeof(ex->sha), "%s", hash);
	for (i = 0; i < v->nimages; i++) {
		if (strcmp(v->images[i].name, name) != 0)
			continue;
		snprintf(v->images[i].sha, sizeof(v->images[i].sha),
		    "%s", hash);
		v->images[i].hashed = 1;
		break;
	}
	return (1);
}

/*
 * Read the SHA256SUMS file of a version directory.  The file is in the
 * format of sha256(1): a hash, two spaces and the name of the image.
 *
 * The names are read on their own, so that an image that the file names
 * but that is not in the directory is still known: the verifier needs
 * that image to report it as missing.  A name that is not one path
 * component is never used in a path; it is counted and reported.
 */
static void
nf_share_read_sums(struct nf_share_version *v, const char *path)
{
	char line[1024];
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return;
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		if (nf_share_sum_line(v, line) < 0) {
			fclose(fp);
			return;
		}
	}
	fclose(fp);
}

/*
 * Does the name look like a driver version?  A version is a number with
 * dots in it, such as 580.178.04.  A directory with another name holds
 * no firmware of ours and is not a version.
 */
static int
nf_share_is_version(const char *name)
{
	size_t i;
	int digits = 0;

	if (name == NULL || *name == '\0')
		return (0);
	for (i = 0; name[i] != '\0'; i++) {
		if (isdigit((unsigned char)name[i])) {
			digits++;
			continue;
		}
		if (name[i] == '.')
			continue;
		return (0);
	}
	return (digits > 0);
}

static int
nf_share_add_version(struct nf_share *share, const char *version)
{
	struct nf_share_version *v;
	char path[PATH_MAX];
	struct dirent *de;
	DIR *dir;

	v = realloc(share->versions,
	    (share->nversions + 1) * sizeof(*share->versions));
	if (v == NULL)
		return (-1);
	share->versions = v;
	v = &share->versions[share->nversions++];
	memset(v, 0, sizeof(*v));
	snprintf(v->version, sizeof(v->version), "%s", version);
	snprintf(v->dir, sizeof(v->dir), "%s/%s", share->root, version);

	dir = opendir(v->dir);
	if (dir == NULL)
		return (0);
	while ((de = readdir(dir)) != NULL) {
		struct nf_share_image *im;
		size_t len = strlen(de->d_name);

		if (len < 5 || strcmp(de->d_name + len - 4, ".bin") != 0)
			continue;
		im = realloc(v->images, (v->nimages + 1) * sizeof(*im));
		if (im == NULL)
			continue;
		v->images = im;
		im = &v->images[v->nimages++];
		memset(im, 0, sizeof(*im));
		snprintf(im->name, sizeof(im->name), "%s", de->d_name);
		snprintf(path, sizeof(path), "%s/%s", v->dir, de->d_name);
		im->size = nf_file_size(path);
	}
	closedir(dir);
	nf_share_sort(v);
	snprintf(path, sizeof(path), "%s/%s", v->dir, NF_SUM_FILE);
	nf_share_read_sums(v, path);
	return (0);
}

int
nf_share_load(const char *dir, struct nf_share *share)
{
	struct dirent *de;
	DIR *d;

	memset(share, 0, sizeof(*share));
	snprintf(share->root, sizeof(share->root), "%s", dir);
	d = opendir(dir);
	if (d == NULL)
		return (-1);
	while ((de = readdir(d)) != NULL) {
		char path[PATH_MAX];
		struct stat st;

		if (de->d_name[0] == '.')
			continue;
		/*
		 * A directory is a driver version when its name is one and
		 * when it holds the file of hashes of its images.  A
		 * directory with another name, or one without that file,
		 * holds no firmware of ours.
		 */
		if (!nf_share_is_version(de->d_name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;
		snprintf(path, sizeof(path), "%s/%s/%s", dir, de->d_name,
		    NF_SUM_FILE);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (nf_share_add_version(share, de->d_name) != 0) {
			closedir(d);
			nf_share_free(share);
			return (-1);
		}
	}
	closedir(d);
	return (0);
}

void
nf_share_free(struct nf_share *share)
{
	size_t i;

	for (i = 0; i < share->nversions; i++) {
		free(share->versions[i].images);
		free(share->versions[i].expected);
	}
	free(share->versions);
	memset(share, 0, sizeof(*share));
}

const struct nf_share_version *
nf_share_version(const struct nf_share *share, const char *version)
{
	size_t i;

	if (version == NULL)
		return (NULL);
	for (i = 0; i < share->nversions; i++) {
		if (strcmp(share->versions[i].version, version) == 0)
			return (&share->versions[i]);
	}
	return (NULL);
}

const struct nf_share_image *
nf_share_image(const struct nf_share_version *v, const char *name)
{
	size_t i;

	if (v == NULL)
		return (NULL);
	for (i = 0; i < v->nimages; i++) {
		if (strcmp(v->images[i].name, name) == 0)
			return (&v->images[i]);
	}
	return (NULL);
}

/*
 * The line of the SHA256SUMS file that names an image.  The line is
 * there whether the image is in the directory or not.
 */
const struct nf_share_expected *
nf_share_expected(const struct nf_share_version *v, const char *name)
{
	size_t i;

	if (v == NULL)
		return (NULL);
	for (i = 0; i < v->nexpected; i++) {
		if (strcmp(v->expected[i].name, name) == 0)
			return (&v->expected[i]);
	}
	return (NULL);
}

const char *
nf_share_newest(const struct nf_share *share)
{
	const char *best = NULL;
	size_t i;

	for (i = 0; i < share->nversions; i++) {
		if (best == NULL ||
		    nf_version_cmp(share->versions[i].version, best) > 0)
			best = share->versions[i].version;
	}
	return (best);
}

/*
 * Is there a driver version with images in the share directory?  A share
 * directory that holds no images holds no firmware.
 */
int
nf_share_installed(const struct nf_share *share)
{
	size_t i;

	for (i = 0; i < share->nversions; i++) {
		if (share->versions[i].nimages > 0)
			return (1);
	}
	return (0);
}

/*
 * The number of objects and lists that are open at the place p, counted
 * from the beginning of the object.  A member of the object itself is at
 * the depth zero.
 */
static int
nf_json_depth(const char *obj, const char *p)
{
	const char *q;
	int depth = 0;

	for (q = obj + 1; q < p; q++) {
		if (*q == '{' || *q == '[')
			depth++;
		else if (*q == '}' || *q == ']')
			depth--;
	}
	return (depth);
}

/*
 * The value of a member: the text between the quotes that follow the
 * colon.  The space around the colon is allowed, and an escape is not
 * read.
 */
static int
nf_json_value(const char *p, const char *end, char *out, size_t size)
{
	const char *value = p;
	const char *start;
	size_t len;

	while (value < end && (*value == ' ' || *value == '\t'))
		value++;
	if (value >= end || *value != ':')
		return (-1);
	value++;
	while (value < end && (*value == ' ' || *value == '\t'))
		value++;
	if (value >= end || *value != '"')
		return (-1);
	value++;
	start = value;
	while (value < end && *value != '"') {
		if (*value == '\\')
			return (-1);
		value++;
	}
	if (value >= end)
		return (-1);
	len = (size_t)(value - start);
	if (len >= size)
		len = size - 1;
	memcpy(out, start, len);
	out[len] = '\0';
	return (0);
}

/*
 * The member of an object.  The members are found in the object itself:
 * the order in which they are written says nothing, and a member of
 * another object is never taken for one of this object.
 */
static int
nf_json_member(const char *obj, size_t len, const char *key, char *out,
    size_t size)
{
	char pattern[64];
	const char *end = obj + len;
	const char *p;
	size_t plen;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	plen = strlen(pattern);
	for (p = obj + 1; p + plen <= end; p++) {
		if (*p == '{' || *p == '[' || *p == '}' || *p == ']')
			continue;
		if (memcmp(p, pattern, plen) != 0)
			continue;
		if (nf_json_depth(obj, p) != 0)
			continue;
		if (nf_json_value(p + plen, end, out, size) == 0)
			return (0);
	}
	return (-1);
}

const char *
nf_share_gpu_name(const struct nf_share_version *v, uint32_t devid, char *buf,
    size_t size)
{
	struct nf_gpu *gpus;
	size_t ngpus, i;
	const char *ret = NULL;

	if (nf_share_gpus(v, &gpus, &ngpus) != 0)
		return (NULL);
	for (i = 0; i < ngpus; i++) {
		if (gpus[i].devid != devid)
			continue;
		snprintf(buf, size, "%s", gpus[i].name);
		ret = buf;
		break;
	}
	free(gpus);
	return (ret);
}

/*
 * supported-gpus.json is a flat list of chip objects.  A full JSON parser
 * is not needed to read the device id and the name of every chip, so
 * every object of the file is looked at: the members of an object are
 * found in the object, in whatever order they are written.
 */
int
nf_share_gpus(const struct nf_share_version *v, struct nf_gpu **gpus,
    size_t *ngpus)
{
	char path[PATH_MAX];
	char *json;
	char *p;
	FILE *fp;
	long size_l;
	size_t n = 0;
	size_t cap = 0;

	*gpus = NULL;
	*ngpus = 0;
	if (v == NULL)
		return (-1);
	snprintf(path, sizeof(path), "%s/%s", v->dir, NF_GPUS_JSON);
	fp = fopen(path, "rb");
	if (fp == NULL)
		return (-1);
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return (-1);
	}
	size_l = ftell(fp);
	rewind(fp);
	if (size_l <= 0 || size_l > 64 * 1024 * 1024) {
		fclose(fp);
		return (-1);
	}
	json = malloc((size_t)size_l + 1);
	if (json == NULL) {
		fclose(fp);
		return (-1);
	}
	if (fread(json, 1, (size_t)size_l, fp) != (size_t)size_l) {
		free(json);
		fclose(fp);
		return (-1);
	}
	json[size_l] = '\0';
	fclose(fp);

	p = json;
	while ((p = strchr(p, '{')) != NULL) {
		char devid[32];
		char name[NF_CARD_NAME_LEN];
		const char *q = p;
		struct nf_gpu *g;
		uint32_t id;
		int depth = 0;

		/* The end of the object that starts here. */
		while (*q != '\0') {
			if (*q == '{')
				depth++;
			else if (*q == '}') {
				depth--;
				if (depth == 0)
					break;
			}
			q++;
		}
		if (depth != 0)
			break;	/* the file is not well formed */
		if (nf_json_member(p, (size_t)(q - p) + 1, "devid", devid,
		    sizeof(devid)) != 0)
			goto next;
		if (nf_json_member(p, (size_t)(q - p) + 1, "name", name,
		    sizeof(name)) != 0)
			goto next;
		if (nf_parse_devid(devid, &id) != 0)
			goto next;
		if (n == cap) {
			size_t newcap = cap ? cap * 2 : 64;
			struct nf_gpu *newgpus;

			newgpus = realloc(*gpus, newcap * sizeof(*newgpus));
			if (newgpus == NULL)
				break;
			*gpus = newgpus;
			cap = newcap;
		}
		g = &(*gpus)[n++];
		g->devid = id;
		snprintf(g->name, sizeof(g->name), "%s", name);
next:
		p++;
	}
	free(json);
	*ngpus = n;
	return (0);
}
