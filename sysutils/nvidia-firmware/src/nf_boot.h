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

#ifndef NF_BOOT_H
#define NF_BOOT_H

#include <limits.h>
#include <sys/types.h>

#include "nf_util.h"

#define	NF_BOOT_FORCED	"forced"

struct nf_boot {
	char dir[PATH_MAX];
	struct nf_image *entries;
	size_t nentries;
	struct nf_strlist foreign;	/* files we did not place */
};

/*
 * A copy that is made but not yet put in place.  The copy is in a
 * temporary file in the boot directory; it is renamed over its place
 * when every copy of the run has been made, so that a failure in a later
 * copy leaves the boot directory as it was.
 */
struct nf_stage {
	char name[NF_IMAGE_LEN];
	char tmp[PATH_MAX];
	char version[NF_VERSION_LEN];
	char sha[NF_SHA256_LEN];
	off_t size;
	int forced;
};

/* Read the inventory of the boot directory. */
int		nf_boot_load(const char *, struct nf_boot *);
void		nf_boot_free(struct nf_boot *);

struct nf_image		*nf_boot_find(struct nf_boot *, const char *);

/* Add or replace an entry of the inventory. */
int		nf_boot_set(struct nf_boot *, const char *, const char *,
		    const char *, off_t, int);

/* Write the inventory back to the boot directory. */
int		nf_boot_write(struct nf_boot *, int);

/* Copy an image into the boot directory and record it. */

/*
 * Make a copy of an image in the boot directory without putting it in
 * place.  The copy is checked against the hash that is given; the
 * temporary file is thrown away when the check fails.
 */

/*
 * Put a staged copy in place and record it in the inventory.  The
 * temporary file is the one that is renamed, so a symbolic link in the
 * way is replaced and never written through.
 */
int		nf_boot_commit(struct nf_boot *, struct nf_stage *);

/* Throw a staged copy away. */
void		nf_boot_discard(struct nf_stage *);

/*
 * Is the name one of the files of somebody else in the boot directory?
 * Such a file is never overwritten.
 */
int		nf_boot_foreign_name(const struct nf_boot *, const char *);

/*
 * Is the image of the name in place?  The inventory says that the file
 * is ours; the file itself has to be there and has to be a regular file:
 * a symbolic link is never an installed image.
 */
int		nf_boot_placed(const struct nf_boot *, const char *);

/* Remove an image from the boot directory and from the inventory. */
int		nf_boot_remove(struct nf_boot *, const char *, int);

/*
 * Remove the inventory and the directory when nothing is left.  When a
 * file of somebody else is left, the inventory stays and is empty: the
 * commands that come later have to be able to work on the directory.
 */
int		nf_boot_cleanup(struct nf_boot *, int);

#endif /* NF_BOOT_H */
