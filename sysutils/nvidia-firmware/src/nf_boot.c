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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nf_boot.h"

/*
 * The inventory of the boot directory.  Every line names one image that we
 * placed there:
 *
 *	<sha256>  <image>  <version>  <forced|->  <size>
 */
static int
nf_boot_parse(struct nf_boot *boot, const char *path)
{
	char line[1024];
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (0);
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		struct nf_boot_entry *e;
		char *p = line;
		char *fields[5];
		int i;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\0')
			continue;
		for (i = 0; i < 5; i++) {
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '\0')
				break;
			fields[i] = p;
			while (*p != '\0' && *p != ' ' && *p != '\t')
				p++;
			if (*p != '\0')
				*p++ = '\0';
		}
		if (i < 3)
			continue;
		/*
		 * The name of an image is one path component.  A name that
		 * is not one means a broken inventory: the tool refuses the
		 * whole inventory instead of using the name in a path.
		 */
		if (!nf_valid_image_name(fields[1])) {
			fclose(fp);
			errno = EINVAL;
			return (-1);
		}
		e = realloc(boot->entries,
		    (boot->nentries + 1) * sizeof(*boot->entries));
		if (e == NULL) {
			fclose(fp);
			return (-1);
		}
		boot->entries = e;
		e = &boot->entries[boot->nentries++];
		memset(e, 0, sizeof(*e));
		snprintf(e->sha, sizeof(e->sha), "%s", fields[0]);
		snprintf(e->name, sizeof(e->name), "%s", fields[1]);
		snprintf(e->version, sizeof(e->version), "%s", fields[2]);
		e->forced = (i > 3 && strcmp(fields[3], NF_BOOT_FORCED) == 0);
		if (i > 4)
			e->size = (off_t)strtoll(fields[4], NULL, 10);
	}
	fclose(fp);
	return (0);
}

static void
nf_boot_foreign(struct nf_boot *boot)
{
	struct dirent *de;
	DIR *dir;

	dir = opendir(boot->dir);
	if (dir == NULL)
		return;
	while ((de = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		struct stat st;

		if (de->d_name[0] == '.')
			continue;
		if (strcmp(de->d_name, NF_SUM_FILE) == 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", boot->dir, de->d_name);
		if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (nf_boot_find(boot, de->d_name) != NULL)
			continue;
		nf_strlist_add(&boot->foreign, de->d_name);
	}
	closedir(dir);
}

/*
 * Does the directory hold a file that is not the inventory?  A name that
 * begins with a dot is not one of ours and does not count.
 */
static int
nf_boot_holds_files(const char *dir)
{
	struct dirent *de;
	DIR *d;

	d = opendir(dir);
	if (d == NULL)
		return (0);
	while ((de = readdir(d)) != NULL) {
		char path[PATH_MAX];
		struct stat st;

		if (de->d_name[0] == '.')
			continue;
		if (strcmp(de->d_name, NF_SUM_FILE) == 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		closedir(d);
		return (1);
	}
	closedir(d);
	return (0);
}

int
nf_boot_load(const char *dir, struct nf_boot *boot)
{
	char path[PATH_MAX];
	struct stat st;

	memset(boot, 0, sizeof(*boot));
	snprintf(boot->dir, sizeof(boot->dir), "%s", dir);
	nf_strlist_init(&boot->foreign);
	snprintf(path, sizeof(path), "%s/%s", dir, NF_SUM_FILE);
	if (stat(path, &st) != 0) {
		if (errno != ENOENT)
			return (-1);
		/*
		 * There is no inventory.  That is all right only when the
		 * directory holds nothing either: an inventory that is
		 * missing while files are there is a broken state, and the
		 * tool refuses it instead of taking it for an empty
		 * inventory.
		 */
		if (nf_boot_holds_files(dir) != 0) {
			errno = EINVAL;
			return (-1);
		}
		return (0);
	}
	if (nf_boot_parse(boot, path) != 0)
		return (-1);
	nf_boot_foreign(boot);
	return (0);
}

void
nf_boot_free(struct nf_boot *boot)
{
	free(boot->entries);
	nf_strlist_free(&boot->foreign);
	memset(boot, 0, sizeof(*boot));
}

struct nf_boot_entry *
nf_boot_find(struct nf_boot *boot, const char *name)
{
	size_t i;

	for (i = 0; i < boot->nentries; i++) {
		if (strcmp(boot->entries[i].name, name) == 0)
			return (&boot->entries[i]);
	}
	return (NULL);
}

int
nf_boot_set(struct nf_boot *boot, const char *name, const char *version,
    const char *sha, off_t size, int forced)
{
	struct nf_boot_entry *e;

	e = nf_boot_find(boot, name);
	if (e == NULL) {
		e = realloc(boot->entries,
		    (boot->nentries + 1) * sizeof(*boot->entries));
		if (e == NULL)
			return (-1);
		boot->entries = e;
		e = &boot->entries[boot->nentries++];
		memset(e, 0, sizeof(*e));
		snprintf(e->name, sizeof(e->name), "%s", name);
	}
	snprintf(e->version, sizeof(e->version), "%s", version);
	snprintf(e->sha, sizeof(e->sha), "%s", sha);
	e->size = size;
	e->forced = forced;
	return (0);
}

int
nf_boot_write(struct nf_boot *boot, int dryrun)
{
	char buf[16384];
	size_t len = 0;
	size_t i;
	int n;

	if (dryrun)
		return (0);
	n = snprintf(buf, sizeof(buf),
	    "# nvidia-firmware: the images in %s\n", boot->dir);
	if (n < 0 || (size_t)n >= sizeof(buf))
		return (-1);
	len = (size_t)n;
	n = snprintf(buf + len, sizeof(buf) - len,
	    "# sha256  image  version  forced  size\n");
	if (n < 0 || (size_t)n >= sizeof(buf) - len)
		return (-1);
	len += (size_t)n;
	for (i = 0; i < boot->nentries; i++) {
		struct nf_boot_entry *e = &boot->entries[i];

		n = snprintf(buf + len, sizeof(buf) - len,
		    "%s  %s  %s  %s  %lld\n", e->sha, e->name, e->version,
		    e->forced ? NF_BOOT_FORCED : "-", (long long)e->size);
		if (n < 0 || (size_t)n >= sizeof(buf) - len)
			return (-1);
		len += (size_t)n;
	}
	if (nf_mkdir_p(boot->dir, 0755) != 0)
		return (-1);
	return (nf_write_atomic(boot->dir, NF_SUM_FILE, buf, len));
}

/*
 * Make a copy of an image in the boot directory without putting it in
 * place.  The copy is in a temporary file whose name cannot be guessed;
 * the hash of the copy is checked against the hash that is given, and
 * the temporary file is thrown away when the check fails.
 */
int
nf_boot_stage(struct nf_boot *boot, const char *name, const char *version,
    const char *sha, const char *src, off_t size, int forced,
    struct nf_stage *stage)
{
	if (!nf_valid_image_name(name)) {
		errno = EINVAL;
		return (-1);
	}
	memset(stage, 0, sizeof(*stage));
	snprintf(stage->name, sizeof(stage->name), "%s", name);
	snprintf(stage->version, sizeof(stage->version), "%s", version);
	snprintf(stage->sha, sizeof(stage->sha), "%s", sha);
	stage->size = size;
	stage->forced = forced;
	if (nf_mkdir_p(boot->dir, 0755) != 0)
		return (-1);
	if (nf_stage_copy(src, boot->dir, sha, stage->tmp,
	    sizeof(stage->tmp)) != 0)
		return (-1);
	return (0);
}

/*
 * Put a staged copy in place and record it in the inventory.
 */
int
nf_boot_commit(struct nf_boot *boot, struct nf_stage *stage)
{
	char dst[PATH_MAX];

	snprintf(dst, sizeof(dst), "%s/%s", boot->dir, stage->name);
	if (nf_commit_staged(stage->tmp, dst) != 0)
		return (-1);
	stage->tmp[0] = '\0';
	return (nf_boot_set(boot, stage->name, stage->version, stage->sha,
	    stage->size, stage->forced));
}

/* Throw a staged copy away. */
void
nf_boot_discard(struct nf_stage *stage)
{
	if (stage == NULL)
		return;
	nf_discard_staged(stage->tmp);
	stage->tmp[0] = '\0';
}

/*
 * Is the name one of the files of somebody else in the boot directory?
 * Such a file is never overwritten.
 */
int
nf_boot_foreign_name(const struct nf_boot *boot, const char *name)
{
	return (nf_strlist_has(&boot->foreign, name));
}

/*
 * Is the image of the name in place?  The inventory says that the file
 * is ours; the file itself has to be there and has to be a regular file:
 * a symbolic link is never an installed image.
 */
int
nf_boot_placed(const struct nf_boot *boot, const char *name)
{
	char path[PATH_MAX];

	if (nf_boot_find((struct nf_boot *)boot, name) == NULL)
		return (0);
	snprintf(path, sizeof(path), "%s/%s", boot->dir, name);
	return (nf_regular_file(path));
}

int
nf_boot_place(struct nf_boot *boot, const char *name, const char *version,
    const char *sha, const char *src, off_t size, int forced, int dryrun)
{
	char dst[PATH_MAX];

	if (!nf_valid_image_name(name)) {
		errno = EINVAL;
		return (-1);
	}
	snprintf(dst, sizeof(dst), "%s/%s", boot->dir, name);
	if (!dryrun) {
		if (nf_mkdir_p(boot->dir, 0755) != 0)
			return (-1);
		if (nf_copy_verify(src, dst, sha) != 0)
			return (-1);
	}
	return (nf_boot_set(boot, name, version, sha, size, forced));
}

int
nf_boot_remove(struct nf_boot *boot, const char *name, int dryrun)
{
	char path[PATH_MAX];
	size_t i;

	if (!nf_valid_image_name(name)) {
		errno = EINVAL;
		return (-1);
	}
	snprintf(path, sizeof(path), "%s/%s", boot->dir, name);
	if (!dryrun) {
		if (unlink(path) != 0 && errno != ENOENT)
			return (-1);
	}
	for (i = 0; i < boot->nentries; i++) {
		if (strcmp(boot->entries[i].name, name) != 0)
			continue;
		memmove(&boot->entries[i], &boot->entries[i + 1],
		    (boot->nentries - i - 1) * sizeof(*boot->entries));
		boot->nentries--;
		return (0);
	}
	return (0);
}

/*
 * Take the inventory away when the boot directory holds nothing of ours
 * any more.
 *
 * A file of somebody else that is in the directory keeps it there.  The
 * inventory is then not taken away but written empty: the directory
 * holds files that are not ours, and a directory without an inventory is
 * a state the tool refuses to work on.  An empty inventory says that
 * the directory is ours and that we have nothing in it.
 */
int
nf_boot_cleanup(struct nf_boot *boot, int dryrun)
{
	char path[PATH_MAX];
	struct dirent *de;
	DIR *dir;

	if (boot->nentries > 0)
		return (0);
	if (nf_boot_holds_files(boot->dir) != 0)
		return (nf_boot_write(boot, dryrun));
	snprintf(path, sizeof(path), "%s/%s", boot->dir, NF_SUM_FILE);
	if (!dryrun)
		unlink(path);
	/* The directory goes away only when it holds nothing else. */
	dir = opendir(boot->dir);
	if (dir == NULL)
		return (0);
	while ((de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		closedir(dir);
		return (0);
	}
	closedir(dir);
	if (!dryrun)
		rmdir(boot->dir);
	return (0);
}
