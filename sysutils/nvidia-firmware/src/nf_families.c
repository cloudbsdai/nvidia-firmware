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
#include <stdlib.h>
#include <string.h>

#include "nf_families.h"
#include "nf_families_data.h"

/*
 * The state of the parser while it reads the table.
 */
struct nf_families_parse {
	struct nf_families *fams;
	struct nf_family *cur;
};

/*
 * A test hook: the number of the call that fails.  The list is not
 * grown then, and the caller has to keep the list it had.  The hook is
 * only there in a binary that was built for the test suite.
 */
static int
nf_families_alloc_fail(void)
{
#ifdef NVIDIA_FIRMWARE_TESTING
	static int calls = 0;
	const char *test;

	test = getenv("NVIDIA_FIRMWARE_TEST_FAMILIES_ALLOC_FAIL");
	if (test == NULL || *test == '\0')
		return (0);
	calls++;
	return (calls == atoi(test) ? 1 : 0);
#else
	return (0);
#endif
}

/*
 * One more entry in a list of structs.  The list is made bigger, the
 * new entry is cleared and it is given back.
 *
 * The list that is there is kept when it cannot be made bigger: the
 * caller frees it, and a list that is not there cannot be freed.
 */
static void *
nf_list_add(void *listp, size_t *n, size_t size)
{
	void **list = listp;
	void *newlist;
	void *item;

	if (nf_families_alloc_fail() != 0)
		return (NULL);
	newlist = realloc(*list, (*n + 1) * size);
	if (newlist == NULL)
		return (NULL);
	*list = newlist;
	item = (char *)*list + (*n) * size;
	memset(item, 0, size);
	(*n)++;
	return (item);
}

static struct nf_family *
nf_family_get(struct nf_families *fams, const char *name)
{
	struct nf_family *fam;
	size_t i;

	for (i = 0; i < fams->nfamilies; i++) {
		if (strcmp(fams->families[i].name, name) == 0)
			return (&fams->families[i]);
	}
	fam = nf_list_add(&fams->families, &fams->nfamilies, sizeof(*fam));
	if (fam == NULL)
		return (NULL);
	snprintf(fam->name, sizeof(fam->name), "%s", name);
	return (fam);
}

static int
nf_family_add_range(struct nf_family *fam, uint32_t lo, uint32_t hi)
{
	struct nf_range *r;

	r = realloc(fam->ranges, (fam->nranges + 1) * sizeof(*r));
	if (r == NULL)
		return (-1);
	fam->ranges = r;
	fam->ranges[fam->nranges].lo = lo;
	fam->ranges[fam->nranges].hi = hi;
	fam->nranges++;
	return (0);
}

static int
nf_family_add_exact(struct nf_family *fam, uint32_t devid)
{
	uint32_t *e;

	e = realloc(fam->exact, (fam->nexact + 1) * sizeof(*e));
	if (e == NULL)
		return (-1);
	fam->exact = e;
	fam->exact[fam->nexact++] = devid;
	return (0);
}

/* "family <NAME>" opens a family. */
static int
nf_keyword_family(struct nf_families_parse *p, const char *name)
{
	if (name == NULL)
		return (-1);
	p->cur = nf_family_get(p->fams, name);
	return (p->cur != NULL ? 0 : -1);
}

/* "attaches yes|no" says whether the driver reaches the family today. */
static int
nf_keyword_attaches(struct nf_families_parse *p, const char *yesno)
{
	if (p->cur == NULL || yesno == NULL)
		return (-1);
	if (strcmp(yesno, "yes") == 0) {
		p->cur->attaches = 1;
		return (0);
	}
	if (strcmp(yesno, "no") == 0) {
		p->cur->attaches = 0;
		return (0);
	}
	return (-1);
}

/* "range <lo> <hi>" gives the device ids of a family. */
static int
nf_keyword_range(struct nf_families_parse *p, const char *los, const char *his)
{
	uint32_t lo, hi;

	if (p->cur == NULL || los == NULL || his == NULL)
		return (-1);
	if (nf_parse_u32(los, &lo) != 0 || nf_parse_u32(his, &hi) != 0)
		return (-1);
	if (lo > hi)
		return (-1);
	return (nf_family_add_range(p->cur, lo, hi));
}

/* "exact <devid>" names one device of a family. */
static int
nf_keyword_exact(struct nf_families_parse *p, const char *devid)
{
	uint32_t id;

	if (p->cur == NULL || devid == NULL)
		return (-1);
	if (nf_parse_devid(devid, &id) != 0)
		return (-1);
	return (nf_family_add_exact(p->cur, id));
}

/*
 * "images <version> <family> <image> [<image> ...]" names the images a
 * version needs for a family.  The names are the rest of the line, so the
 * tokenizer of the line is handed over to this function.
 */
static int
nf_keyword_images(struct nf_families_parse *p, const char *version,
    const char *family, char *first, char **save)
{
	struct nf_images *im;
	char *tok;

	if (version == NULL || family == NULL || first == NULL)
		return (-1);
	im = nf_list_add(&p->fams->images, &p->fams->nimages, sizeof(*im));
	if (im == NULL)
		return (-1);
	snprintf(im->version, sizeof(im->version), "%s", version);
	snprintf(im->family, sizeof(im->family), "%s", family);
	nf_strlist_init(&im->images);
	for (tok = first; tok != NULL; tok = strtok_r(NULL, " \t", save)) {
		/*
		 * The name of an image is one path component: the names
		 * of the table are used to build paths.
		 */
		if (!nf_valid_image_name(tok))
			return (-1);
		if (nf_strlist_add(&im->images, tok) != 0)
			return (-1);
	}
	return (0);
}

/*
 * The next word of a line.  A word between double quotes is the text of
 * the quotes; the quotes are taken off and the word is written to the
 * buffer.  The words of the rules of the table hold spaces, so they are
 * written between quotes.
 */
static char *
nf_next_word(char **p, char *out, size_t size)
{
	char *s = *p;
	size_t len = 0;
	int quoted = 0;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '"') {
		quoted = 1;
		s++;
	}
	while (*s != '\0' && (quoted || (*s != ' ' && *s != '\t'))) {
		if (quoted && *s == '"')
			break;
		if (len + 1 >= size)
			break;
		out[len++] = *s++;
	}
	out[len] = '\0';
	if (quoted && *s == '"')
		s++;
	*p = s;
	return (out);
}

/*
 * "product <name> <series> <family>" names a product line.  The name of
 * the product is the name NVIDIA gives it in the name of the chip.
 */
static int
nf_keyword_product(struct nf_families_parse *p, char *rest)
{
	struct nf_product *pr;
	char pattern[NF_PATTERN_LEN];
	char series[NF_SERIES_LEN];
	char family[NF_FAMILY_LEN];

	nf_next_word(&rest, pattern, sizeof(pattern));
	nf_next_word(&rest, series, sizeof(series));
	nf_next_word(&rest, family, sizeof(family));
	if (pattern[0] == '\0' || series[0] == '\0' || family[0] == '\0')
		return (-1);
	pr = nf_list_add(&p->fams->products, &p->fams->nproducts, sizeof(*pr));
	if (pr == NULL)
		return (-1);
	snprintf(pr->pattern, sizeof(pr->pattern), "%s", pattern);
	snprintf(pr->series, sizeof(pr->series), "%s", series);
	snprintf(pr->family, sizeof(pr->family), "%s", family);
	return (0);
}

/*
 * One line of the table.  Returns 0 when the line is good and -1 when it
 * is not.
 */
static int
nf_families_line(struct nf_families_parse *p, char *line)
{
	char *kw;
	char *a1;
	char *a2;
	char *a3;
	char *save;

	while (*line == ' ' || *line == '\t')
		line++;
	if (*line == '#' || *line == '\0')
		return (0);
	kw = strtok_r(line, " \t", &save);
	if (kw == NULL)
		return (0);
	/*
	 * The rule of a product takes the rest of the line, so it is
	 * read before the line is cut into words.
	 */
	if (strcmp(kw, "product") == 0) {
		char *rest = save;

		return (nf_keyword_product(p, rest));
	}
	a1 = strtok_r(NULL, " \t", &save);
	a2 = strtok_r(NULL, " \t", &save);
	a3 = strtok_r(NULL, " \t", &save);

	if (strcmp(kw, "family") == 0)
		return (nf_keyword_family(p, a1));
	if (strcmp(kw, "attaches") == 0)
		return (nf_keyword_attaches(p, a1));
	if (strcmp(kw, "range") == 0)
		return (nf_keyword_range(p, a1, a2));
	if (strcmp(kw, "exact") == 0)
		return (nf_keyword_exact(p, a1));
	if (strcmp(kw, "images") == 0)
		return (nf_keyword_images(p, a1, a2, a3, &save));
	return (-1);
}

/*
 * Read the table from an open file.  The file is closed here, also when
 * the table turns out to be bad.
 */
static int
nf_families_parse(FILE *fp, struct nf_families *fams)
{
	char line[1024];
	struct nf_families_parse p;

	p.fams = fams;
	p.cur = NULL;
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		if (nf_families_line(&p, line) != 0) {
			fclose(fp);
			nf_families_free(fams);
			return (-1);
		}
	}
	fclose(fp);
	/*
	 * A table that names no family, or no image, is not a table: the
	 * tool would place no firmware and would take every image away.
	 * Such a table is refused.
	 */
	if (fams->nfamilies == 0 || fams->nimages == 0) {
		nf_families_free(fams);
		errno = EINVAL;
		return (-1);
	}
	return (0);
}

int
nf_families_load(const char *path, struct nf_families *fams)
{
	FILE *fp;

	memset(fams, 0, sizeof(*fams));
	fp = fopen(path, "r");
	if (fp == NULL)
		return (-1);
	return (nf_families_parse(fp, fams));
}

/*
 * Read the table from a text that is already in memory.
 */
int
nf_families_load_text(const char *text, struct nf_families *fams)
{
	FILE *fp;

	memset(fams, 0, sizeof(*fams));
	fp = fmemopen((void *)(uintptr_t)text, strlen(text), "r");
	if (fp == NULL)
		return (-1);
	return (nf_families_parse(fp, fams));
}

/*
 * Read the table that is compiled into the program.  It is the same table
 * the share directory holds, so that the tool can name the family of a
 * card when the firmware is not installed.
 */
int
nf_families_load_builtin(struct nf_families *fams)
{
	return (nf_families_load_text(nf_families_text, fams));
}

void
nf_families_free(struct nf_families *fams)
{
	size_t i;

	for (i = 0; i < fams->nfamilies; i++) {
		free(fams->families[i].ranges);
		free(fams->families[i].exact);
	}
	free(fams->families);
	for (i = 0; i < fams->nimages; i++)
		nf_strlist_free(&fams->images[i].images);
	free(fams->images);
	free(fams->products);
	memset(fams, 0, sizeof(*fams));
}

const char *
nf_families_family(const struct nf_families *fams, uint32_t devid)
{
	size_t i, j;

	for (i = 0; i < fams->nfamilies; i++) {
		for (j = 0; j < fams->families[i].nexact; j++) {
			if (fams->families[i].exact[j] == devid)
				return (fams->families[i].name);
		}
	}
	for (i = 0; i < fams->nfamilies; i++) {
		for (j = 0; j < fams->families[i].nranges; j++) {
			if (devid >= fams->families[i].ranges[j].lo &&
			    devid <= fams->families[i].ranges[j].hi)
				return (fams->families[i].name);
		}
	}
	return (NULL);
}

int
nf_families_images(const struct nf_families *fams, const char *version,
    const char *family, struct nf_strlist *out)
{
	size_t i;

	nf_strlist_init(out);
	for (i = 0; i < fams->nimages; i++) {
		if (strcmp(fams->images[i].version, version) != 0)
			continue;
		if (strcmp(fams->images[i].family, family) != 0)
			continue;
		for (size_t j = 0; j < fams->images[i].images.n; j++) {
			if (nf_strlist_add(out,
			    fams->images[i].images.items[j]) != 0)
				return (-1);
		}
	}
	return (0);
}

int
nf_families_attaches(const struct nf_families *fams, const char *family)
{
	size_t i;

	for (i = 0; i < fams->nfamilies; i++) {
		if (strcmp(fams->families[i].name, family) == 0)
			return (fams->families[i].attaches);
	}
	return (0);
}

/*
 * The series of products a chip belongs to.  The rules are looked at in
 * the order they are written: the first rule whose name is in the name of
 * the chip is the one that counts.
 */
/*
 * The lines of products a family is in.  The rules of the table say which
 * line of products a chip belongs to and which family it is in, so the
 * lines of a family are the lines of the rules that name it.
 */
int
nf_families_series_of(const struct nf_families *fams, const char *family,
    struct nf_strlist *out)
{
	size_t i;

	nf_strlist_init(out);
	for (i = 0; i < fams->nproducts; i++) {
		if (strcmp(fams->products[i].family, family) != 0)
			continue;
		if (nf_strlist_has(out, fams->products[i].series))
			continue;
		if (nf_strlist_add(out, fams->products[i].series) != 0) {
			nf_strlist_free(out);
			return (-1);
		}
	}
	return (0);
}
