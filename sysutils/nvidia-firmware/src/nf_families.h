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

#ifndef NF_FAMILIES_H
#define NF_FAMILIES_H

#include <stddef.h>
#include <stdint.h>

#include "nf_util.h"

#define	NF_FAMILY_LEN	16
#define	NF_PATTERN_LEN	64		/* the name of a product */
#define	NF_SERIES_LEN	32		/* the line of products a chip is in */

struct nf_range {
	uint32_t lo;
	uint32_t hi;
};

struct nf_family {
	char name[NF_FAMILY_LEN];
	int attaches;			/* 1: the driver attaches today */
	struct nf_range *ranges;
	size_t nranges;
	uint32_t *exact;
	size_t nexact;
};

struct nf_images {
	char version[NF_VERSION_LEN];
	char family[NF_FAMILY_LEN];
	struct nf_strlist images;
};

/*
 * A product line.  The pattern is the name NVIDIA gives the product in
 * the name of the chip; the series is the line of products the chip
 * belongs to and the family is the family the chip is in.
 */
struct nf_product {
	char pattern[NF_PATTERN_LEN];
	char series[NF_SERIES_LEN];
	char family[NF_FAMILY_LEN];
};

struct nf_families {
	struct nf_family *families;
	size_t nfamilies;
	struct nf_images *images;
	size_t nimages;
	struct nf_product *products;
	size_t nproducts;
};

/*
 * Load the family table.  The file format is line oriented:
 *
 *	family <NAME>
 *		attaches yes|no
 *		range <lo> <hi>
 *		exact <devid>
 *	images <version> <family> <image> [<image> ...]
 *	product <name> <series> <family>
 *
 * The words of a rule that hold a space are written between double
 * quotes.  The rules are looked at in the order they are written: the
 * first rule whose name is in the name of the chip is the one that
 * counts.
 */
int		nf_families_load(const char *, struct nf_families *);
void		nf_families_free(struct nf_families *);

/*
 * Read the table from a text that is already in memory, and from the
 * table that is compiled into the program.  The compiled table is the
 * table the share directory holds; it is used when the share directory
 * is not there.
 */
int		nf_families_load_text(const char *, struct nf_families *);
int		nf_families_load_builtin(struct nf_families *);

/*
 * Return the name of the family a device id belongs to, or NULL when the
 * device has no GSP firmware.  Exact entries win over ranges.
 */
const char	*nf_families_family(const struct nf_families *, uint32_t);

/* The images the given version needs for the given family. */
int		nf_families_images(const struct nf_families *, const char *,
		    const char *, struct nf_strlist *);

/* 1 when the driver attaches to the family today, 0 when it does not. */
int		nf_families_attaches(const struct nf_families *, const char *);

/*
 * The series of products a chip belongs to, or NULL when the name of the
 * chip names no product of the table.
 */
const char	*nf_families_series(const struct nf_families *, const char *);

#endif /* NF_FAMILIES_H */
