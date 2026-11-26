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

#ifndef NF_FETCH_H
#define NF_FETCH_H

#include <limits.h>
#include <sys/types.h>

#include "nf_pin.h"
#include "nf_util.h"

/*
 * Where the firmware comes from when there is no mirror.  The release of
 * a driver version holds the images of that version.
 */
#define	NF_MIRROR_BASE "https://download.nvidia.com/XFree86/FreeBSD-x86_64"

/* The name of the release of a driver version. */
#define	NF_RELEASE_NAME "NVIDIA-FreeBSD-x86_64"

/*
 * One image that was fetched and checked but that is not in place yet.
 * The image is in a temporary file in the boot directory: it is renamed
 * over its place when every image of the run has been fetched, so that a
 * failure in a later image leaves the boot directory as it was.
 */
struct nf_fetched {
	char name[NF_IMAGE_LEN];
	char version[NF_VERSION_LEN];
	char sha[NF_SHA256_LEN];
	off_t size;
	char tmp[PATH_MAX];
};

struct nf_fetch {
	struct nf_fetched *items;
	size_t n;
	size_t cap;
};

void		nf_fetch_init(struct nf_fetch *);
void		nf_fetch_free(struct nf_fetch *);

/*
 * A directory of its own for the files that are fetched but not placed.
 * The dry run uses it: the files are fetched and checked there, and the
 * directory is taken away when the run is over.  Nothing of the boot
 * directory is touched by a dry run.
 */
int		nf_fetch_tmpdir(char *, size_t);
void		nf_fetch_rmdir(const char *);

/*
 * Fetch the images of a version and check them against the pin table.
 *
 * The mirror is the directory the firmware is taken from when it is not
 * fetched from the network; it holds the images themselves or the release
 * of the driver.  When there is no mirror the release is fetched from
 * download.nvidia.com.
 *
 * Every image is checked before it is put in place: the size and the hash
 * have to be the pinned ones.  An image that does not match is thrown away
 * and the call fails, so that nothing of it is left behind.
 *
 * The images are not in place when the call returns: the caller renames
 * them over their place.
 */
int		nf_fetch_get(struct nf_fetch *, const char *mirror,
		    const char *version, const struct nf_pin *,
		    const struct nf_strlist *, const char *);

#endif /* NF_FETCH_H */
