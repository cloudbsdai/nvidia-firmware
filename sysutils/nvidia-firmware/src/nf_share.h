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

#ifndef NF_SHARE_H
#define NF_SHARE_H

#include <sys/types.h>

#include <limits.h>

#include "nf_util.h"

struct nf_share_image {
	char name[NF_IMAGE_LEN];
	char sha[NF_SHA256_LEN];
	off_t size;
	int hashed;			/* the hash is known */
};

/* One line of the SHA256SUMS file of a version. */
struct nf_share_expected {
	char name[NF_IMAGE_LEN];
	char sha[NF_SHA256_LEN];
};

struct nf_share_version {
	char version[NF_VERSION_LEN];
	char dir[PATH_MAX];
	struct nf_share_image *images;		/* the files that are there */
	size_t nimages;
	struct nf_share_expected *expected;	/* what SHA256SUMS names */
	size_t nexpected;
	size_t nbadnames;			/* names that are not paths */
};

struct nf_share {
	char root[PATH_MAX];
	struct nf_share_version *versions;
	size_t nversions;
};

/* One chip of the supported-gpus.json file of a version. */
struct nf_gpu {
	uint32_t devid;
	char name[NF_CARD_NAME_LEN];
};

/* Scan a share directory; one subdirectory per driver version. */
int		nf_share_load(const char *, struct nf_share *);
void		nf_share_free(struct nf_share *);

const struct nf_share_version	*nf_share_version(const struct nf_share *,
		    const char *);
const struct nf_share_image	*nf_share_image(const struct nf_share_version *,
		    const char *);

/* The line of the SHA256SUMS file that names an image, or NULL. */
const struct nf_share_expected	*nf_share_expected(const struct nf_share_version *,
		    const char *);

/* The newest installed version, or NULL when nothing is installed. */
const char	*nf_share_newest(const struct nf_share *);

/* 1 when the share directory holds a version with images, 0 when not. */
int		nf_share_installed(const struct nf_share *);

/*
 * The name NVIDIA gives a device id in the supported-gpus.json file of a
 * version, or NULL when the file or the device is not listed.
 */
const char	*nf_share_gpu_name(const struct nf_share_version *, uint32_t,
		    char *, size_t);

/* Every chip of the supported-gpus.json file of a version. */
int		nf_share_gpus(const struct nf_share_version *,
		    struct nf_gpu **, size_t *);

#endif /* NF_SHARE_H */
