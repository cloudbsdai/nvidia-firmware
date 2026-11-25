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

#ifndef NF_UTIL_H
#define NF_UTIL_H

#include <sys/types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define	NF_SHA256_LEN	65		/* 64 hex digits and the NUL */
#define	NF_IMAGE_LEN	64		/* the name of a firmware image */
#define	NF_VERSION_LEN	32		/* a driver version */
#define	NF_CARD_NAME_LEN	192		/* the name of a chip */

/*
 * Paths used by the tool.  The share directory holds one directory per
 * driver version; the boot directory is where the firmware that the
 * present GPUs need is placed.
 */
#ifndef NF_SHARE_DIR
#define	NF_SHARE_DIR	"/usr/local/share/nvidia-firmware"
#endif
#ifndef NF_BOOT_DIR
#define	NF_BOOT_DIR	"/boot/firmware/nvidia"
#endif

#define	NF_FAMILIES_FILE	"nvidia-firmware.families"
#define	NF_SUM_FILE		"SHA256SUMS"
#define	NF_GPUS_JSON		"supported-gpus.json"

/* Growable string list. */
struct nf_strlist {
	char **items;
	size_t n;
	size_t cap;
};

void		nf_strlist_init(struct nf_strlist *);
int		nf_strlist_add(struct nf_strlist *, const char *);
void		nf_strlist_free(struct nf_strlist *);
int		nf_strlist_has(const struct nf_strlist *, const char *);

/* String helpers. */
char		*nf_strdup(const char *);
int		nf_parse_u32(const char *, uint32_t *);
int		nf_parse_devid(const char *, uint32_t *);

/* Path helpers. */
int		nf_mkdir_p(const char *, mode_t);
int		nf_file_exists(const char *);
off_t		nf_file_size(const char *);

/*
 * Is the path a regular file?  A symbolic link is not: the link itself
 * is not the file of an image.
 */
int		nf_regular_file(const char *);

/*
 * Is the name one path component that may be used to build a path?  A
 * name that holds a slash, that is ".." or that begins with a dot is
 * refused: every name that comes from an inventory or from a file of
 * hashes is checked with this before it is used in a path.
 */
int		nf_valid_image_name(const char *);

/* The directory of a path, without the last component. */
int		nf_path_dir(const char *, char *, size_t);

/* Hashing. */
int		nf_sha256_file(const char *, char *);
int		nf_sha256_fd(int, char *);
void		nf_sha256_hex(const unsigned char *, char *);

/*
 * The hash of a file that has to be a regular file.  The file is opened
 * without following a symbolic link: a symbolic link is never taken for
 * the file of an image.  errno says why the hash was not computed:
 * ENOENT when the file is not there, ELOOP when the name is a symbolic
 * link and EINVAL when the name is not a regular file.
 */
int		nf_sha256_regular(const char *, char *);

/*
 * Copy a file into a temporary file in a directory and check the hash of
 * the copy.  The temporary file is created with mkstemp(3), so that its
 * name is not predictable and no symbolic link can be followed.  The copy
 * is not put in place: the caller renames it when every copy of the run
 * has been made.
 */
int		nf_stage_copy(const char *, const char *, const char *, char *,
		    size_t);

/* Put a staged copy in place; rename(2) never follows a symbolic link. */
int		nf_commit_staged(const char *, const char *);

/* Throw a staged copy away. */
void		nf_discard_staged(const char *);

/*
 * Copy a file to dst through a temporary file.  The temporary file is
 * created in the directory of dst with mkstemp(3), so that its name is
 * not predictable and no symbolic link can be followed.  The contents are
 * flushed to stable storage, the hash of the file that was written is
 * checked against want_sha and the temporary file is renamed over dst
 * only when the hash matches.  A symbolic link is never created.
 */
int		nf_copy_verify(const char *, const char *, const char *);

/*
 * Write a file in a directory and put it in place atomically.  The
 * temporary file is created with mkstemp(3) in the directory of the file
 * and is renamed over it; the name of the file that is written is never
 * a predictable one.
 */
int		nf_write_atomic(const char *, const char *, const char *, size_t);

/* Compare two dotted version numbers numerically; <0, 0 or >0. */
int		nf_version_cmp(const char *, const char *);

/* Read one line (without the newline) from an open file. */
int		nf_read_line(FILE *, char *, size_t);

#endif /* NF_UTIL_H */
