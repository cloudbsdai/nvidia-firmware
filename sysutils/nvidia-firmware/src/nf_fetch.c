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
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nf_fetch.h"

/*
 * The bounds of a release.  The release is never unpacked as a whole:
 * the members are read from it and only the member that is wanted is
 * taken out of it, and what is written is bounded by the size the pin
 * table gives.  The bounds below are the last line of defence: a
 * release that is too big or that holds too many members is refused
 * before anything of it is read.
 */
#define	NF_RELEASE_MAX_BYTES	(1024LL * 1024LL * 1024LL)
#define	NF_RELEASE_MAX_MEMBERS	256
#define	NF_MEMBER_MAX_LEN	1024

void
nf_fetch_init(struct nf_fetch *fetch)
{
	fetch->items = NULL;
	fetch->n = 0;
	fetch->cap = 0;
}

void
nf_fetch_free(struct nf_fetch *fetch)
{
	size_t i;

	for (i = 0; i < fetch->n; i++)
		nf_discard_staged(fetch->items[i].tmp);
	free(fetch->items);
	nf_fetch_init(fetch);
}

/* Run a program.  The arguments are given as words and no shell is
 * started, so that a name that holds a space or a semicolon stays data. */
static void nf_fetch_rmtree(const char *);

/*
 * A directory of its own for the files that are fetched but not placed.
 * The caller says what the directory is for when it cannot be made: the
 * template of the path is written down in one place only.
 */
int
nf_fetch_tmpdir(char *out, size_t size)
{
	int n;

	n = snprintf(out, size, "/var/tmp/nvidia-firmware.XXXXXX");
	if (n < 0 || (size_t)n >= size) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (mkdtemp(out) == NULL)
		return (-1);
	return (0);
}

/* Take a directory of the program away. */
void
nf_fetch_rmdir(const char *dir)
{
	if (dir == NULL || dir[0] == '\0')
		return;
	nf_fetch_rmtree(dir);
}

/*
 * Run a program and write what it writes on its standard output to a
 * file.  The number of bytes that is written is bounded: a program that
 * writes more than the bound is stopped and the call fails.  A program
 * that writes without end cannot fill the disk of the machine.
 */
static int
nf_fetch_capture(char *const argv[], const char *out, off_t maxbytes)
{
	char buf[8192];
	int fds[2];
	pid_t pid;
	int fd;
	ssize_t n = 0;
	off_t total = 0;
	int status;
	int error = 0;

	if (pipe(fds) != 0)
		return (-1);
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return (-1);
	}
	if (pid == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(fds[1]);
		execv(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		close(fds[0]);
		(void)waitpid(pid, &status, 0);
		return (-1);
	}
	while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
		ssize_t off = 0;

		if (total + (off_t)n > maxbytes) {
			fprintf(stderr, "nvidia-firmware: %s is bigger than "
			    "%lld bytes; it is refused\n", out,
			    (long long)maxbytes);
			error = 1;
			break;
		}
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
		total += (off_t)n;
	}
	if (n < 0)
		error = 1;
	close(fds[0]);
	close(fd);
	if (error != 0) {
		(void)kill(pid, SIGTERM);
		(void)waitpid(pid, &status, 0);
		return (-1);
	}
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return (-1);
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return (-1);
	return (0);
}

/*
 * Where a program of the base system is.  The PATH of whoever runs the
 * tool is not trusted: the paths of the base system are tried and the
 * program is run with the path it was given, so that a program that
 * somebody else put in the PATH is never run.
 */
static int
nf_fetch_program(const char *name, char *out, size_t size)
{
	static const char *dirs[] = {
		"/usr/bin",
		"/bin",
		"/usr/sbin",
		"/sbin",
		NULL
	};
	size_t i;
	int n;

	for (i = 0; dirs[i] != NULL; i++) {
		n = snprintf(out, size, "%s/%s", dirs[i], name);
		if (n < 0 || (size_t)n >= size)
			continue;
		if (access(out, X_OK) == 0)
			return (0);
	}
	errno = ENOENT;
	return (-1);
}

/*
 * Fetch a file into a file of its own.  The file that is written is the
 * file that is used afterwards.  The file is written through a pipe, so
 * that the number of bytes that is written is bounded: a release that is
 * bigger than the limit is stopped while it is written and cannot fill
 * the disk of the machine.
 */
static int
nf_fetch_download(const char *url, const char *out)
{
	char fetch[PATH_MAX];
	char *argv[4];

	if (nf_fetch_program("fetch", fetch, sizeof(fetch)) != 0) {
		fprintf(stderr, "nvidia-firmware: there is no fetch program "
		    "on this machine; the firmware cannot be fetched; use "
		    "--from with a directory that holds it\n");
		return (-1);
	}
	argv[0] = fetch;
	argv[1] = (char *)"-q";
	argv[2] = (char *)url;
	argv[3] = NULL;
	if (nf_fetch_capture(argv, out, NF_RELEASE_MAX_BYTES) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot fetch %s\n", url);
		return (-1);
	}
	return (0);
}

/* The name of the release of a driver version. */
static void
nf_fetch_release_name(const char *version, char *out, size_t size)
{
	snprintf(out, size, "%s-%s.tar.xz", NF_RELEASE_NAME, version);
}

/* The name of the directory of the release of a driver version. */
static void
nf_fetch_release_dir(const char *version, char *out, size_t size)
{
	snprintf(out, size, "%s-%s", NF_RELEASE_NAME, version);
}

/* Take a directory and everything in it away. */
static void
nf_fetch_rmtree(const char *path)
{
	struct dirent *de;
	DIR *dir;

	dir = opendir(path);
	if (dir == NULL) {
		unlink(path);
		return;
	}
	while ((de = readdir(dir)) != NULL) {
		char sub[PATH_MAX];
		struct stat st;

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", path, de->d_name);
		if (lstat(sub, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			nf_fetch_rmtree(sub);
		else
			unlink(sub);
	}
	closedir(dir);
	rmdir(path);
}

/*
 * The state of one run: the release that was fetched and the members it
 * holds.
 */
struct nf_fetch_ctx {
	char tmpdir[PATH_MAX];
	char tarball[PATH_MAX];
	struct nf_strlist members;
	int have_release;
};

static void
nf_fetch_ctx_free(struct nf_fetch_ctx *ctx)
{
	if (ctx->tmpdir[0] != '\0')
		nf_fetch_rmtree(ctx->tmpdir);
	nf_strlist_free(&ctx->members);
	memset(ctx, 0, sizeof(*ctx));
}

/*
 * May the member be a member of the release?  Every member has to be
 * under the directory of the release, and no part of it may go up with
 * "..": a member that is not one of the release is refused before
 * anything of the archive is read.
 */
static int
nf_fetch_member_ok(const char *release, const char *member)
{
	const char *p;
	size_t len = strlen(release);

	if (strncmp(member, release, len) != 0)
		return (0);
	if (member[len] != '/')
		return (0);
	for (p = member; *p != '\0'; ) {
		const char *end = strchr(p, '/');
		size_t n = end != NULL ? (size_t)(end - p) : strlen(p);

		if (n == 2 && strncmp(p, "..", 2) == 0)
			return (0);
		if (end == NULL)
			break;
		p = end + 1;
	}
	return (1);
}

/*
 * Read the members of the release and hold them.  The list is read from
 * the archive itself: the archive is never unpacked as a whole.
 */
static int
nf_fetch_members(struct nf_fetch_ctx *ctx, const char *version)
{
	char list[PATH_MAX];
	char release[PATH_MAX];
	char tar[PATH_MAX];
	char *argv[4];
	char line[NF_MEMBER_MAX_LEN];
	FILE *fp;

	nf_fetch_release_dir(version, release, sizeof(release));
	if (nf_fetch_program("tar", tar, sizeof(tar)) != 0) {
		fprintf(stderr, "nvidia-firmware: there is no tar program on "
		    "this machine; the release cannot be unpacked\n");
		return (-1);
	}
	snprintf(list, sizeof(list), "%s/members", ctx->tmpdir);
	argv[0] = tar;
	argv[1] = (char *)"-tf";
	argv[2] = ctx->tarball;
	argv[3] = NULL;
	if (nf_fetch_capture(argv, list, NF_RELEASE_MAX_BYTES) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot read the members of "
		    "the release of the driver %s\n", version);
		return (-1);
	}
	fp = fopen(list, "r");
	if (fp == NULL)
		return (-1);
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		if (line[0] == '\0')
			continue;
		if (!nf_fetch_member_ok(release, line)) {
			fprintf(stderr, "nvidia-firmware: the release of the "
			    "driver %s holds a member that is not one of the "
			    "release: %s\n", version, line);
			fclose(fp);
			return (-1);
		}
		if (ctx->members.n >= NF_RELEASE_MAX_MEMBERS) {
			fprintf(stderr, "nvidia-firmware: the release of the "
			    "driver %s holds too many members\n", version);
			fclose(fp);
			return (-1);
		}
		if (nf_strlist_add(&ctx->members, line) != 0) {
			fclose(fp);
			return (-1);
		}
	}
	fclose(fp);
	return (0);
}

/*
 * The member of the release that holds an image.  The image is the last
 * part of the name of the member: the release puts every image in the
 * directory of the firmware.
 */
static const char *
nf_fetch_member_of(const struct nf_fetch_ctx *ctx, const char *name)
{
	size_t i;

	for (i = 0; i < ctx->members.n; i++) {
		const char *member = ctx->members.items[i];
		const char *base = strrchr(member, '/');

		if (base == NULL)
			continue;
		if (strcmp(base + 1, name) != 0)
			continue;
		return (member);
	}
	return (NULL);
}

/*
 * Take one member out of the release and write it to a file.  The number
 * of bytes that is written is bounded by the size the pin table gives:
 * a member that is bigger is refused while it is written, so that it
 * cannot fill the disk of the machine.
 */
static int
nf_fetch_member_file(struct nf_fetch_ctx *ctx, const char *member,
    const char *out, off_t maxsize)
{
	char tar[PATH_MAX];
	char *argv[7];

	if (nf_fetch_program("tar", tar, sizeof(tar)) != 0)
		return (-1);
	argv[0] = tar;
	argv[1] = (char *)"-xJf";
	argv[2] = ctx->tarball;
	argv[3] = (char *)"-O";
	argv[4] = (char *)"--";
	argv[5] = (char *)member;
	argv[6] = NULL;
	if (nf_fetch_capture(argv, out, maxsize) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot take %s out of the "
		    "release; it is bigger than the size the pin table gives\n",
		    member);
		unlink(out);
		return (-1);
	}
	return (0);
}

/*
 * Get the release of a version.  The release is taken from the mirror
 * when there is one and fetched from NVIDIA when there is not.  It is
 * never unpacked as a whole: the members are read from it and only the
 * member that is wanted is taken out of it.
 */
static int
nf_fetch_release(struct nf_fetch_ctx *ctx, const char *mirror,
    const char *version)
{
	char url[PATH_MAX];
	char release[PATH_MAX];
	struct stat st;

	if (ctx->have_release)
		return (0);
	nf_fetch_release_name(version, release, sizeof(release));
	if (nf_fetch_tmpdir(ctx->tmpdir, sizeof(ctx->tmpdir)) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot make a directory for "
		    "the release: %s\n", strerror(errno));
		return (-1);
	}
	snprintf(ctx->tarball, sizeof(ctx->tarball), "%s/%s", ctx->tmpdir,
	    release);
	if (mirror != NULL) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s", mirror, release);
		if (!nf_regular_file(path)) {
			nf_fetch_ctx_free(ctx);
			return (-1);
		}
		if (nf_copy_verify(path, ctx->tarball, NULL,
		    NF_RELEASE_MAX_BYTES) != 0) {
			fprintf(stderr, "nvidia-firmware: cannot copy %s\n",
			    path);
			nf_fetch_ctx_free(ctx);
			return (-1);
		}
	} else {
		snprintf(url, sizeof(url), "%s/%s/%s", NF_MIRROR_BASE, version,
		    release);
		if (nf_fetch_download(url, ctx->tarball) != 0) {
			nf_fetch_ctx_free(ctx);
			return (-1);
		}
	}
	if (stat(ctx->tarball, &st) != 0) {
		nf_fetch_ctx_free(ctx);
		return (-1);
	}
	if (st.st_size > NF_RELEASE_MAX_BYTES) {
		fprintf(stderr, "nvidia-firmware: the release of the driver %s "
		    "is bigger than %lld bytes; it is refused\n", version,
		    (long long)NF_RELEASE_MAX_BYTES);
		nf_fetch_ctx_free(ctx);
		return (-1);
	}
	if (nf_fetch_members(ctx, version) != 0) {
		nf_fetch_ctx_free(ctx);
		return (-1);
	}
	ctx->have_release = 1;
	return (0);
}

/*
 * The file of an image in a mirror.  The mirror holds the images
 * themselves, either in a directory per version or flat.
 */
static int
nf_fetch_mirror_file(const char *mirror, const char *version, const char *name,
    char *out, size_t size)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s/%s", mirror, version, name);
	if (nf_regular_file(path)) {
		snprintf(out, size, "%s", path);
		return (0);
	}
	snprintf(path, sizeof(path), "%s/%s", mirror, name);
	if (nf_regular_file(path)) {
		snprintf(out, size, "%s", path);
		return (0);
	}
	return (-1);
}

/*
 * The file of an image, from the mirror or from the release.
 */
static int
nf_fetch_source(struct nf_fetch_ctx *ctx, const char *mirror,
    const char *version, const char *name, off_t maxsize, char *out,
    size_t size)
{
	const char *member;
	char path[PATH_MAX];

	if (mirror != NULL &&
	    nf_fetch_mirror_file(mirror, version, name, out, size) == 0)
		return (0);
	if (nf_fetch_release(ctx, mirror, version) != 0) {
		if (mirror != NULL) {
			fprintf(stderr, "nvidia-firmware: %s of the driver %s "
			    "is not in %s\n", name, version, mirror);
		} else {
			fprintf(stderr, "nvidia-firmware: %s of the driver %s "
			    "could not be fetched; use --from with a directory "
			    "that holds it\n", name, version);
		}
		return (-1);
	}
	member = nf_fetch_member_of(ctx, name);
	if (member == NULL) {
		fprintf(stderr, "nvidia-firmware: the release of the driver "
		    "%s does not hold %s\n", version, name);
		return (-1);
	}
	snprintf(path, sizeof(path), "%s/%s", ctx->tmpdir, name);
	if (nf_fetch_member_file(ctx, member, path, maxsize) != 0)
		return (-1);
	snprintf(out, size, "%s", path);
	return (0);
}

/*
 * May the files be put in this directory?  A directory that others can
 * write in is refused: the file that was checked could be replaced
 * between the check and the rename that puts it in its place.  A
 * directory that is not ours is refused as well: its owner can replace
 * the file that was checked, whatever the mode of the directory is.
 */
static int
nf_fetch_private_dir(const char *dir)
{
	struct stat st;
	mode_t write;

	if (stat(dir, &st) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot read %s: %s\n", dir,
		    strerror(errno));
		return (-1);
	}
	if (!S_ISDIR(st.st_mode)) {
		fprintf(stderr, "nvidia-firmware: %s is not a directory\n", dir);
		return (-1);
	}
	if (st.st_uid != geteuid()) {
		fprintf(stderr, "nvidia-firmware: %s is not ours; the file that "
		    "was checked could be replaced\n", dir);
		return (-1);
	}
	write = st.st_mode & (S_IWGRP | S_IWOTH);
	if (write != 0) {
		fprintf(stderr, "nvidia-firmware: %s can be written in by "
		    "others; the file that was checked could be replaced\n",
		    dir);
		return (-1);
	}
	return (0);
}

/*
 * The directories on the way to the directory: every one of them has to
 * be ours or the one of root, and nobody but us may write in it.  A
 * directory that has the sticky bit set may be written in by others: a
 * file of ours in it cannot be taken away by them.
 */
static int
nf_fetch_private_path(const char *dir)
{
	char path[PATH_MAX];
	char prefix[PATH_MAX];
	struct stat st;
	const char *p;
	size_t len;

	if (snprintf(path, sizeof(path), "%s", dir) >= (int)sizeof(path))
		return (-1);
	p = path;
	for (;;) {
		while (*p == '/')
			p++;
		if (*p == '\0')
			break;
		len = (size_t)(p - path);
		if (len == 0 || len >= sizeof(prefix))
			return (-1);
		memcpy(prefix, path, len);
		prefix[len] = '\0';
		if (stat(prefix, &st) != 0)
			return (-1);
		if (!S_ISDIR(st.st_mode))
			return (-1);
		if (st.st_uid != 0 && st.st_uid != geteuid())
			return (-1);
		if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0 &&
		    (st.st_mode & S_ISVTX) == 0)
			return (-1);
		while (*p != '\0' && *p != '/')
			p++;
	}
	return (0);
}

/*
 * Make a copy of an image in the boot directory and check it.  The copy
 * is in a temporary file whose name cannot be guessed; the size and the
 * hash of the copy have to be the pinned ones, and the copy is thrown
 * away when they are not.
 */
static int
nf_fetch_stage(struct nf_fetched *item, const char *src, const char *dir,
    const struct nf_image *pin)
{
	char got[NF_SHA256_LEN];
	struct stat st;

	if (nf_mkdir_p(dir, 0755) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot make %s: %s\n", dir,
		    strerror(errno));
		return (-1);
	}
	if (nf_fetch_private_dir(dir) != 0)
		return (-1);
	if (nf_fetch_private_path(dir) != 0) {
		fprintf(stderr, "nvidia-firmware: the way to %s is not safe; "
		    "the file that was checked could be replaced\n", dir);
		return (-1);
	}
	/*
	 * The copy is made without a hash of its own: the hash below is
	 * the one check of the file, and a file of thirty or seventy
	 * megabytes is not read twice.  The copy stops one byte after the
	 * size of the pin: the limit is enforced while the file is
	 * written, so that a file that is bigger than the pin cannot fill
	 * the disk of the machine.
	 */
	if (nf_stage_copy(src, dir, NULL, pin->size + 1, item->tmp,
	    sizeof(item->tmp)) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot copy %s\n", src);
		return (-1);
	}
	if (stat(item->tmp, &st) != 0) {
		fprintf(stderr, "nvidia-firmware: cannot read %s: %s\n",
		    item->tmp, strerror(errno));
		nf_discard_staged(item->tmp);
		return (-1);
	}
	if (st.st_size != pin->size) {
		fprintf(stderr, "nvidia-firmware: %s of the driver %s has the "
		    "size %lld, the pin table says %lld\n", pin->name,
		    pin->version, (long long)st.st_size, (long long)pin->size);
		nf_discard_staged(item->tmp);
		return (-1);
	}
	if (nf_sha256_regular(item->tmp, got) != 0 ||
	    strcmp(got, pin->sha) != 0) {
		fprintf(stderr, "nvidia-firmware: %s of the driver %s is not "
		    "the file that is pinned\n", pin->name, pin->version);
		nf_discard_staged(item->tmp);
		return (-1);
	}
	return (0);
}

static int
nf_fetch_add(struct nf_fetch *fetch, const struct nf_fetched *item)
{
	struct nf_fetched *items;

	if (fetch->n == fetch->cap) {
		size_t cap = fetch->cap ? fetch->cap * 2 : 8;

		items = realloc(fetch->items, cap * sizeof(*items));
		if (items == NULL)
			return (-1);
		fetch->items = items;
		fetch->cap = cap;
	}
	fetch->items[fetch->n++] = *item;
	return (0);
}

int
nf_fetch_get(struct nf_fetch *fetch, const char *mirror, const char *version,
    const struct nf_pin *pin, const struct nf_strlist *images, const char *dir)
{
	struct nf_fetch_ctx ctx;
	size_t i;
	int error = 0;

	memset(&ctx, 0, sizeof(ctx));
	nf_strlist_init(&ctx.members);
	for (i = 0; i < images->n; i++) {
		const struct nf_image *pi;
		struct nf_fetched item;
		char src[PATH_MAX];

		pi = nf_pin_image(pin, version, images->items[i]);
		if (pi == NULL) {
			fprintf(stderr, "nvidia-firmware: %s of the driver %s "
			    "is not in the pin table\n", images->items[i],
			    version);
			error = -1;
			break;
		}
		if (nf_fetch_source(&ctx, mirror, version, pi->name, pi->size,
		    src, sizeof(src)) != 0) {
			error = -1;
			break;
		}
		memset(&item, 0, sizeof(item));
		snprintf(item.name, sizeof(item.name), "%s", pi->name);
		snprintf(item.version, sizeof(item.version), "%s", pi->version);
		snprintf(item.sha, sizeof(item.sha), "%s", pi->sha);
		item.size = pi->size;
		if (nf_fetch_stage(&item, src, dir, pi) != 0) {
			error = -1;
			break;
		}
		if (nf_fetch_add(fetch, &item) != 0) {
			nf_discard_staged(item.tmp);
			fprintf(stderr, "nvidia-firmware: out of memory\n");
			error = -1;
			break;
		}
	}
	nf_fetch_ctx_free(&ctx);
	if (error != 0) {
		/*
		 * Nothing is left behind: every copy that was made is
		 * thrown away and the boot directory is as it was.
		 */
		nf_fetch_free(fetch);
		return (-1);
	}
	return (0);
}
