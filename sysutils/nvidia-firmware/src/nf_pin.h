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

#ifndef NF_PIN_H
#define NF_PIN_H

#include <sys/types.h>

#include "nf_util.h"

/*
 * The pin table.  It says which file belongs to which driver version and
 * how big it is and what its hash is.  The table is the only place where
 * the pins are written down, and the firmware is never shipped with the
 * package: the tool fetches it and checks it against this table before it
 * places anything.
 *
 * One line of the table is
 *
 *	<version> <image> <size> <sha256>
 *
 * A line that is not one of those is an error: a table that is read half
 * way is a table the tool cannot trust.
 */
struct nf_pin {
	struct nf_image *images;
	size_t nimages;
	char **versions;			/* the versions, the newest last */
	size_t nversions;
};

/* Read the table.  Returns 0 when the table was read and -1 when it was
 * not; errno says why. */
int		nf_pin_load(const char *, struct nf_pin *);
void		nf_pin_free(struct nf_pin *);

/* The image of a version, or NULL when the table does not name it. */
const struct nf_image	*nf_pin_image(const struct nf_pin *, const char *,
		    const char *);

/* 1 when the table names the version, 0 when it does not. */
int		nf_pin_has_version(const struct nf_pin *, const char *);

/* The version at the place, or NULL when the place is past the end. */
const char	*nf_pin_version(const struct nf_pin *, size_t);

/* The newest version of the table, or NULL when the table is empty. */
const char	*nf_pin_newest(const struct nf_pin *);

/* The images of a version, in the order of the table. */
int		nf_pin_images(const struct nf_pin *, const char *,
		    struct nf_strlist *);

#endif /* NF_PIN_H */
