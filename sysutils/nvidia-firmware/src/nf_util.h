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
 * Paths used by the tool.  The data directory holds the pin table and the
 * table of the families; the boot directory is where the firmware that the
 * present GPUs need is placed.
 */
#ifndef NF_DATA_DIR
#define	NF_DATA_DIR	"/usr/local/share/nvidia-firmware"
#endif
#ifndef NF_BOOT_DIR
#define	NF_BOOT_DIR	"/boot/firmware/nvidia"
#endif

#define	NF_FAMILIES_FILE	"nvidia-firmware.families"
#define	NF_SUM_FILE		"SHA256SUMS"
#define	NF_PIN_FILE		"versions.list"

/*
 * The version of the program.  It is the version of the package, and the
 * package is ours: the version is the date of the day the package was
 * made.  The versions of the NVIDIA driver are data in the pin table and
 * are never the version of the program.
 *
 * The port gives its own DISTVERSION to the build, so that the program
 * and the package always carry the same version.
 */
#ifndef NF_VERSION
#define	NF_VERSION "0.0.0"
#endif

/*
 * The version of the schema of the JSON output.  It is bumped whenever a
 * field of the output is added, removed or changed.
 */
#define	NF_SCHEMA_VERSION "2.1"

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

/*
 * One image file: the file itself, the driver version it belongs to,
 * its hash and its size.  The inventory of the boot directory and the
 * pin table both hold image files, so both hold them in this shape and
 * both add them with the one helper below.
 */
struct nf_image {
	char version[NF_VERSION_LEN];
	char name[NF_IMAGE_LEN];
	char sha[NF_SHA256_LEN];
	off_t size;
	int forced;			/* the inventory marks its images */
};

/*
 * Add an image to a list of images.  The list grows as it goes and the
 * new image is a copy of the one that is given.  Returns 0 when the
 * image was added and -1 when the memory for it is not there.
 */
int		nf_image_add(struct nf_image **, size_t *, const char *,
		    const char *, const char *, off_t, int);

/*
 * Split a line of a table into its words.  The words are cut out of the
 * line itself, so the line is changed and the words point into it.
 *
 * A line that holds nothing has no words: that is an empty line and a
 * line that begins with a hash.  The number of words that were found is
 * returned, and it is less than max when the line holds fewer words.
 */
size_t		nf_split_fields(char *, char **, size_t);

/* String helpers. */
char		*nf_strdup(const char *);
int		nf_parse_u32(const char *, uint32_t *);
int		nf_parse_devid(const char *, uint32_t *);

/* Path helpers. */
int		nf_mkdir_p(const char *, mode_t);
int		nf_file_exists(const char *);

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
 *
 * The copy stops when more than maxbytes would be written: the limit is
 * enforced while the file is written and not afterwards, so that a file
 * that is bigger than the limit cannot fill the disk of the machine.
 */
int		nf_stage_copy(const char *, const char *, const char *, off_t,
		    char *, size_t);

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
 *
 * The copy stops when more than maxbytes would be written: the limit is
 * enforced while the file is written.
 */
int		nf_copy_verify(const char *, const char *, const char *, off_t);

/*
 * Write a file in a directory and put it in place atomically.  The
 * temporary file is created with mkstemp(3) in the directory of the file
 * and is renamed over it; the name of the file that is written is never
 * a predictable one.
 */
int		nf_write_atomic(const char *, const char *, const char *, size_t);

/* Compare two dotted version numbers numerically; <0, 0 or >0. */
int		nf_version_cmp(const char *, const char *);

/*
 * Read a version of ours.  The version is the date of the day the
 * package was made: the year, the month and the day, written as three
 * numbers with dots in between.  A release of the same day carries a
 * suffix, such as 2026.11.26.1, and the suffix is not part of the three
 * numbers.
 *
 * The one parser of the program: the text of the version and the three
 * numbers of the version always come from the same place.
 *
 * Returns 0 when the version was read and -1 when it was not.
 */
int		nf_version_parse(const char *, int *, int *, int *);

/* Read one line (without the newline) from an open file. */
int		nf_read_line(FILE *, char *, size_t);

/*
 * Is the path one that the test asks the program not to read?  The paths
 * are in the variable NVIDIA_FIRMWARE_TEST_DENY, separated by colons.
 * The program that is built for the test suite refuses them with EACCES;
 * the installed program never refuses anything.
 */
int		nf_test_denied(const char *);

#endif /* NF_UTIL_H */
