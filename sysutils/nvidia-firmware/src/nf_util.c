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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * The SHA-256 of libmd.  The header that declares the interface is
 * sha256.h: FreeBSD installs it from sys/crypto/sha2, and the file is
 * named sha2.h on the systems that carry the older libmd.  Both name the
 * same interface (SHA256_CTX, SHA256_Init(), SHA256_Update() and
 * SHA256_Final()), so the header that is there is the one that is used.
 */
#if defined(__has_include)
#if __has_include(<sha256.h>)
#include <sha256.h>
#elif __has_include(<sha2.h>)
#include <sha2.h>
#else
#error "no header of the SHA-256 interface of libmd"
#endif
#else
#include <sha256.h>
#endif

#include "nf_util.h"

void
nf_strlist_init(struct nf_strlist *sl)
{
	sl->items = NULL;
	sl->n = 0;
	sl->cap = 0;
}

int
nf_strlist_add(struct nf_strlist *sl, const char *s)
{
	char **p;

	if (sl->n == sl->cap) {
		size_t newcap = sl->cap ? sl->cap * 2 : 8;
		char **newitems;

		newitems = realloc(sl->items, newcap * sizeof(*newitems));
		if (newitems == NULL)
			return (-1);
		sl->items = newitems;
		sl->cap = newcap;
	}
	p = &sl->items[sl->n];
	*p = nf_strdup(s);
	if (*p == NULL)
		return (-1);
	sl->n++;
	return (0);
}

void
nf_strlist_free(struct nf_strlist *sl)
{
	size_t i;

	for (i = 0; i < sl->n; i++)
		free(sl->items[i]);
	free(sl->items);
	nf_strlist_init(sl);
}

int
nf_strlist_has(const struct nf_strlist *sl, const char *s)
{
	size_t i;

	for (i = 0; i < sl->n; i++) {
		if (strcmp(sl->items[i], s) == 0)
			return (1);
	}
	return (0);
}

char *
nf_strdup(const char *s)
{
	size_t len;
	char *p;

	if (s == NULL)
		return (NULL);
	len = strlen(s) + 1;
	p = malloc(len);
	if (p != NULL)
		memcpy(p, s, len);
	return (p);
}

int
nf_parse_u32(const char *s, uint32_t *out)
{
	uint64_t v;
	char *end;

	if (s == NULL || *s == '\0')
		return (-1);
	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0')
		return (-1);
	if (v > UINT32_MAX)
		return (-1);
	*out = (uint32_t)v;
	return (0);
}

/*
 * A PCI device id is written as 0x2204 in NVIDIA's tables and as 2204 in
 * the test fixtures; both are accepted.
 */
int
nf_parse_devid(const char *s, uint32_t *out)
{
	const char *p = s;
	uint64_t v;
	char *end;

	if (p == NULL || *p == '\0')
		return (-1);
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	errno = 0;
	v = strtoull(p, &end, 16);
	if (errno != 0 || end == p || *end != '\0')
		return (-1);
	if (v > UINT32_MAX)
		return (-1);
	*out = (uint32_t)v;
	return (0);
}

int
nf_mkdir_p(const char *path, mode_t mode)
{
	char buf[PATH_MAX];
	char *p;

	if (strlen(path) >= sizeof(buf))
		return (-1);
	strcpy(buf, path);
	for (p = buf + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(buf, mode) != 0 && errno != EEXIST)
			return (-1);
		*p = '/';
	}
	if (mkdir(buf, mode) != 0 && errno != EEXIST)
		return (-1);
	return (0);
}

int
nf_file_exists(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0);
}

off_t
nf_file_size(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return (-1);
	return (st.st_size);
}

void
nf_sha256_hex(const unsigned char *digest, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		out[i * 2] = hex[(digest[i] >> 4) & 0xf];
		out[i * 2 + 1] = hex[digest[i] & 0xf];
	}
	out[SHA256_DIGEST_LENGTH * 2] = '\0';
}

int
nf_sha256_file(const char *path, char *out)
{
	int fd;
	int error;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (-1);
	error = nf_sha256_fd(fd, out);
	close(fd);
	return (error);
}

/*
 * The hash of the file that is open on the descriptor.  The descriptor is
 * read from its beginning, so that the hash is of the whole file and not
 * of what is left of it.
 */
int
nf_sha256_fd(int fd, char *out)
{
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256_CTX ctx;
	char buf[65536];
	ssize_t n;

	if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
		return (-1);
	SHA256_Init(&ctx);
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		SHA256_Update(&ctx, buf, (size_t)n);
	if (n < 0)
		return (-1);
	SHA256_Final(digest, &ctx);
	nf_sha256_hex(digest, out);
	return (0);
}

/*
 * The hash of a file that has to be a regular file.  The file is opened
 * without following a symbolic link and the descriptor is checked: a
 * symbolic link is never taken for the file of an image.
 *
 * Returns 0 when the hash was computed and -1 when it was not.  errno
 * says why: ENOENT when the file is not there, ELOOP when the name is a
 * symbolic link and EINVAL when the name is not a regular file.
 */
int
nf_sha256_regular(const char *path, char *out)
{
	struct stat st;
	int fd;
	int error;

	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return (-1);
	if (fstat(fd, &st) != 0) {
		close(fd);
		return (-1);
	}
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		errno = EINVAL;
		return (-1);
	}
	error = nf_sha256_fd(fd, out);
	close(fd);
	return (error);
}

/*
 * Is the path a regular file?  A symbolic link is not a regular file:
 * the link itself is not the file of an image.
 */
int
nf_regular_file(const char *path)
{
	struct stat st;

	if (lstat(path, &st) != 0)
		return (0);
	return (S_ISREG(st.st_mode));
}

/*
 * Is the name one path component?  A name with a slash in it, the name
 * ".." and a name that begins with a dot are refused.  Every name that
 * comes from an inventory or from a file of hashes is checked with this
 * before it is used to build a path.
 */
int
nf_valid_image_name(const char *name)
{
	size_t i;
	size_t len;

	if (name == NULL)
		return (0);
	len = strlen(name);
	if (len == 0 || len >= NF_IMAGE_LEN)
		return (0);
	if (name[0] == '.')
		return (0);
	for (i = 0; i < len; i++) {
		if (name[i] == '/')
			return (0);
	}
	return (1);
}

/* The directory of a path: everything before its last component. */
int
nf_path_dir(const char *path, char *out, size_t size)
{
	const char *slash;
	size_t len;

	if (path == NULL || out == NULL || size == 0)
		return (-1);
	slash = strrchr(path, '/');
	if (slash == NULL) {
		if (size < 2)
			return (-1);
		strcpy(out, ".");
		return (0);
	}
	len = (size_t)(slash - path);
	if (len == 0) {
		if (size < 2)
			return (-1);
		strcpy(out, "/");
		return (0);
	}
	if (len >= size)
		return (-1);
	memcpy(out, path, len);
	out[len] = '\0';
	return (0);
}

/*
 * Copy a file into a temporary file in a directory and check the hash of
 * the copy.
 *
 * The temporary file is created with mkstemp(3): its name cannot be
 * guessed, it is created with O_EXCL and it cannot be a symbolic link
 * that points at some other file.  The file that is written is the file
 * that is hashed, so that the check and the action use the same file.
 *
 * The copy is not put in place: the caller renames it when every copy of
 * the run has been made, so that a failure in a later copy leaves the
 * directory as it was.  The name of the temporary file is written to tmp.
 */
int
nf_stage_copy(const char *src, const char *dir, const char *want_sha,
    char *tmp, size_t size)
{
	unsigned char buf[65536];
	char got[NF_SHA256_LEN];
	ssize_t n;
	int in;
	int fd;
	int error;

	if (snprintf(tmp, size, "%s/.nvidia-firmware.XXXXXX", dir) >=
	    (int)size)
		return (-1);
	fd = mkstemp(tmp);
	if (fd < 0)
		return (-1);
	if (fchmod(fd, 0644) != 0) {
		close(fd);
		nf_discard_staged(tmp);
		return (-1);
	}
	in = open(src, O_RDONLY | O_NOFOLLOW);
	if (in < 0) {
		close(fd);
		nf_discard_staged(tmp);
		return (-1);
	}
	error = 0;
	n = 0;
	while ((n = read(in, buf, sizeof(buf))) > 0) {
		ssize_t off = 0;

		while (off < n) {
			ssize_t w = write(fd, buf + off, (size_t)(n - off));

			if (w <= 0) {
				error = 1;
				break;
			}
			off += w;
		}
		if (error != 0)
			break;
	}
	if (n < 0)
		error = 1;
	close(in);
	if (error == 0 && fsync(fd) != 0)
		error = 1;
	if (error == 0 && nf_sha256_fd(fd, got) != 0)
		error = 1;
	close(fd);
	if (error != 0) {
		nf_discard_staged(tmp);
		return (-1);
	}
	if (want_sha != NULL && strcmp(got, want_sha) != 0) {
		nf_discard_staged(tmp);
		errno = EIO;
		return (-1);
	}
	return (0);
}

/*
 * Put a staged copy in place.  rename(2) replaces the name itself and
 * never follows a symbolic link, so a link that is in the way is
 * replaced and not written through.
 */
int
nf_commit_staged(const char *tmp, const char *dst)
{
	return (rename(tmp, dst));
}

/* Throw a staged copy away. */
void
nf_discard_staged(const char *tmp)
{
	if (tmp == NULL || tmp[0] == '\0')
		return;
	unlink(tmp);
}

/*
 * Copy a file to dst through a temporary file in the directory of dst.
 *
 * The name of the temporary file comes from mkstemp(3): it cannot be
 * guessed, it is created with O_EXCL and it cannot be a symbolic link
 * that points at some other file.  The file that is written is the file
 * that is hashed and the file that is renamed over dst, so the check and
 * the action use the same file.  rename(2) replaces the name itself and
 * never follows a symbolic link, so a link that is in the way is
 * replaced and not written through.
 */
int
nf_copy_verify(const char *src, const char *dst, const char *want_sha)
{
	char dir[PATH_MAX];
	char tmp[PATH_MAX];

	if (nf_path_dir(dst, dir, sizeof(dir)) != 0)
		return (-1);
	if (nf_stage_copy(src, dir, want_sha, tmp, sizeof(tmp)) != 0)
		return (-1);
	if (nf_commit_staged(tmp, dst) != 0) {
		nf_discard_staged(tmp);
		return (-1);
	}
	return (0);
}

/*
 * Write a file in a directory and put it in place.  The temporary file
 * is created with mkstemp(3) in the directory of the file and is renamed
 * over it, so the name of the file that is written is never a
 * predictable one and a symbolic link in the way is replaced instead of
 * written through.
 */
int
nf_write_atomic(const char *dir, const char *name, const char *data, size_t len)
{
	char tmp[PATH_MAX];
	char path[PATH_MAX];
	size_t off = 0;
	int fd;

	if (snprintf(tmp, sizeof(tmp), "%s/.nvidia-firmware.XXXXXX", dir) >=
	    (int)sizeof(tmp))
		return (-1);
	fd = mkstemp(tmp);
	if (fd < 0)
		return (-1);
	if (fchmod(fd, 0644) != 0) {
		close(fd);
		unlink(tmp);
		return (-1);
	}
	while (off < len) {
		ssize_t w = write(fd, data + off, len - off);

		if (w <= 0) {
			close(fd);
			unlink(tmp);
			return (-1);
		}
		off += (size_t)w;
	}
	if (fsync(fd) != 0) {
		close(fd);
		unlink(tmp);
		return (-1);
	}
	if (close(fd) != 0) {
		unlink(tmp);
		return (-1);
	}
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >=
	    (int)sizeof(path)) {
		unlink(tmp);
		return (-1);
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return (-1);
	}
	return (0);
}

int
nf_version_cmp(const char *a, const char *b)
{
	const char *pa = a;
	const char *pb = b;

	if (a == NULL || b == NULL) {
		if (a == b)
			return (0);
		return (a == NULL ? -1 : 1);
	}
	for (;;) {
		long va, vb;
		char *end;

		while (*pa == '.' || *pa == '_')
			pa++;
		while (*pb == '.' || *pb == '_')
			pb++;
		if (*pa == '\0' && *pb == '\0')
			return (0);
		if (!isdigit((unsigned char)*pa)) {
			va = (unsigned char)*pa;
			pa++;
		} else {
			va = strtol(pa, &end, 10);
			pa = end;
		}
		if (!isdigit((unsigned char)*pb)) {
			vb = (unsigned char)*pb;
			pb++;
		} else {
			vb = strtol(pb, &end, 10);
			pb = end;
		}
		if (va != vb)
			return (va < vb ? -1 : 1);
	}
}

int
nf_read_line(FILE *fp, char *buf, size_t size)
{
	size_t len;

	if (fgets(buf, (int)size, fp) == NULL)
		return (-1);
	len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	return (0);
}
