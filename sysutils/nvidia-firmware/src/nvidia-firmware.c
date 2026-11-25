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

/*
 * nvidia-firmware places the GSP firmware that the NVIDIA GPUs of the
 * machine need into /boot/firmware/nvidia.  The firmware itself comes from
 * the nvidia-firmware package, which holds one directory per driver
 * version.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <libxo/xo.h>

#include "nf_boot.h"
#include "nf_cards.h"
#include "nf_families.h"
#include "nf_rc.h"
#include "nf_share.h"
#include "nf_util.h"

#define	NF_EXIT_OK	0
#define	NF_EXIT_ERROR	1
#define	NF_EXIT_USAGE	2

#define	NF_VERSION_PIN	"hw.nvidia.version_pin"

struct nf_config {
	char share[PATH_MAX];
	char boot[PATH_MAX];
	char rcconf[PATH_MAX];
	char rcdir[PATH_MAX];
	int dryrun;
	int json;
};

/*
 * The options of the command line.  The names of the options are in the
 * table of main; the values they take are in the fields below.
 */
struct nf_options {
	int no_delete;
	int keep_versions;
	int expect_card;
	int cards;
	int all;
	const char *version;		/* the value of --version */
	const char *image;		/* the value of --force */
	const char *arg;		/* a word of its own */
};

/* ---------------------------------------------------------------- output */

static void nf_usage(void);

static void
nf_text(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xo_emit_hv(NULL, fmt, ap);
	va_end(ap);
}

static void
nf_json_field(const char *name, const char *fmt, ...)
{
	char xofmt[256];
	va_list ap;

	snprintf(xofmt, sizeof(xofmt), "{:%s/%s}", name, fmt);
	va_start(ap, fmt);
	xo_emit_hv(NULL, xofmt, ap);
	va_end(ap);
}

static void
nf_error(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "nvidia-firmware: %s\n", buf);
}

static void
nf_warn(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "nvidia-firmware: warning: %s\n", buf);
}

/* A list of names as one JSON list. */
static void
nf_json_name_list(const char *name, const struct nf_strlist *items)
{
	size_t i;

	xo_open_list_h(NULL, name);
	for (i = 0; i < items->n; i++) {
		xo_open_instance_h(NULL, name);
		nf_json_field("name", "%s", items->items[i]);
		xo_close_instance_h(NULL, name);
	}
	xo_close_list_h(NULL, name);
}

/* ----------------------------------------------------------------- paths */

static void
nf_paths(struct nf_config *cfg)
{
	const char *root = NULL;

#ifdef NVIDIA_FIRMWARE_TESTING
	root = getenv("NVIDIA_FIRMWARE_TEST_ROOT");
#endif
	if (root != NULL && *root != '\0') {
		snprintf(cfg->share, sizeof(cfg->share),
		    "%s/share/nvidia-firmware", root);
		snprintf(cfg->boot, sizeof(cfg->boot),
		    "%s/boot/firmware/nvidia", root);
		snprintf(cfg->rcconf, sizeof(cfg->rcconf), "%s/etc/rc.conf",
		    root);
		snprintf(cfg->rcdir, sizeof(cfg->rcdir),
		    "%s/etc/rc.conf.d/nvidia_firmware", root);
		return;
	}
	snprintf(cfg->share, sizeof(cfg->share), "%s", NF_SHARE_DIR);
	snprintf(cfg->boot, sizeof(cfg->boot), "%s", NF_BOOT_DIR);
	snprintf(cfg->rcconf, sizeof(cfg->rcconf), "/etc/rc.conf");
	snprintf(cfg->rcdir, sizeof(cfg->rcdir),
	    "/etc/rc.conf.d/nvidia_firmware");
}

/*
 * The version the driver is pinned to, or an empty string when nothing is
 * pinned.  The sysctl is read the same way the kernel publishes it.
 */
static const char *
nf_version_pin(void)
{
	static char pin[NF_VERSION_LEN];
	size_t len = sizeof(pin);

#ifdef NVIDIA_FIRMWARE_TESTING
	{
		const char *test;

		test = getenv("NVIDIA_FIRMWARE_TEST_PIN");
		if (test != NULL && *test != '\0') {
			snprintf(pin, sizeof(pin), "%s", test);
			return (pin);
		}
	}
#endif
	if (sysctlbyname(NF_VERSION_PIN, pin, &len, NULL, 0) != 0) {
		pin[0] = '\0';
		return (pin);
	}
	pin[sizeof(pin) - 1] = '\0';
	return (pin);
}

/*
 * The state of the nvidia kernel module.  The third state is not the
 * same as the second: a caller that changes the boot directory has to
 * refuse when it does not know whether the driver uses the firmware.
 */
#define	NF_KLD_UNKNOWN	0
#define	NF_KLD_LOADED	1
#define	NF_KLD_NOT_LOADED 2

/*
 * Where kldstat(8) is.  The tool does not trust the PATH of whoever
 * runs it: the paths of the base system are tried first.
 */
static const char *
nf_kldstat_path(void)
{
	static const char *paths[] = {
		"/usr/sbin/kldstat",
		"/sbin/kldstat",
		NULL
	};
	size_t i;

	for (i = 0; paths[i] != NULL; i++) {
		if (access(paths[i], X_OK) == 0)
			return (paths[i]);
	}
	return (NULL);
}

/*
 * Is the nvidia kernel module loaded?  kldstat(8) answers the question
 * with its exit status: zero when the module is loaded and one when it
 * is not.  Any other answer, and a kldstat(8) that cannot be run at
 * all, is the third state: the state of the module could not be found
 * out.
 */
static int
nf_nvidia_loaded(void)
{
	char cmd[PATH_MAX];
	char line[256];
	const char *kldstat;
	FILE *fp;
	int status;
	int loaded = 0;

#ifdef NVIDIA_FIRMWARE_TESTING
	{
		const char *test;

		test = getenv("NVIDIA_FIRMWARE_TEST_KLD");
		if (test != NULL && *test != '\0') {
			if (strcmp(test, "loaded") == 0)
				return (NF_KLD_LOADED);
			if (strcmp(test, "not") == 0)
				return (NF_KLD_NOT_LOADED);
			return (NF_KLD_UNKNOWN);
		}
	}
#endif
	kldstat = nf_kldstat_path();
	if (kldstat == NULL)
		return (NF_KLD_UNKNOWN);
	snprintf(cmd, sizeof(cmd), "%s -q -m nvidia 2>/dev/null", kldstat);
	fp = popen(cmd, "r");
	if (fp == NULL)
		return (NF_KLD_UNKNOWN);
	if (fgets(line, sizeof(line), fp) != NULL)
		loaded = 1;
	status = pclose(fp);
	if (status == -1 || !WIFEXITED(status))
		return (NF_KLD_UNKNOWN);
	if (WEXITSTATUS(status) == 0)
		return (NF_KLD_LOADED);
	if (WEXITSTATUS(status) == 1)
		return (loaded ? NF_KLD_LOADED : NF_KLD_NOT_LOADED);
	return (NF_KLD_UNKNOWN);
}

/*
 * May the boot directory be changed?  The nvidia module must not be
 * loaded, and the state of the module must be known: a tool that cannot
 * tell whether the driver uses the firmware does not touch it.
 */
static int
nf_module_unloaded(void)
{
	int state;

	state = nf_nvidia_loaded();
	if (state == NF_KLD_LOADED) {
		nf_error("the nvidia module is loaded, unload it first");
		return (0);
	}
	if (state == NF_KLD_UNKNOWN) {
		nf_error("cannot tell whether the nvidia module is loaded");
		return (0);
	}
	return (1);
}

/* -------------------------------------------------------------- versions */

static const char *
nf_pick_version(const struct nf_share *share, const char *wanted)
{
	const char *pin;
	const char *newest;

	if (wanted != NULL && *wanted != '\0') {
		if (nf_share_version(share, wanted) == NULL) {
			nf_error("version %s is not installed", wanted);
			return (NULL);
		}
		return (wanted);
	}
	pin = nf_version_pin();
	if (pin != NULL && *pin != '\0') {
		if (nf_share_version(share, pin) != NULL)
			return (pin);
	}
	newest = nf_share_newest(share);
	if (newest == NULL)
		nf_error("no driver version is installed in %s", share->root);
	return (newest);
}

/* ---------------------------------------------------------------- helpers */

/*
 * The families an image serves in a version.
 */
static void
nf_image_families(const struct nf_families *fams, const char *version,
    const char *image, struct nf_strlist *families)
{
	size_t i, j;

	nf_strlist_init(families);
	for (i = 0; i < fams->nimages; i++) {
		int serves = 0;

		if (strcmp(fams->images[i].version, version) != 0)
			continue;
		for (j = 0; j < fams->images[i].images.n; j++) {
			if (strcmp(fams->images[i].images.items[j], image) == 0)
				serves = 1;
		}
		if (!serves)
			continue;
		nf_strlist_add(families, fams->images[i].family);
	}
}

/*
 * The names of the cards that need an image, for the version that is
 * loaded.
 */
static void
nf_cards_for_image(const struct nf_card *cards, size_t ncards,
    const struct nf_families *fams, const char *version, const char *image,
    struct nf_strlist *out)
{
	size_t i;

	nf_strlist_init(out);
	for (i = 0; i < ncards; i++) {
		struct nf_strlist images;
		size_t k;

		if (!cards[i].has_family)
			continue;
		if (nf_families_images(fams, version, cards[i].family,
		    &images) != 0)
			continue;
		for (k = 0; k < images.n; k++) {
			if (strcmp(images.items[k], image) == 0) {
				nf_strlist_add(out, cards[i].selector);
				break;
			}
		}
		nf_strlist_free(&images);
	}
}

static void
nf_join(struct nf_strlist *sl, char *out, size_t size)
{
	size_t i;
	size_t len = 0;

	out[0] = '\0';
	for (i = 0; i < sl->n; i++) {
		int n;

		n = snprintf(out + len, size - len, "%s%s",
		    i > 0 ? "," : "", sl->items[i]);
		if (n < 0 || (size_t)n >= size - len)
			break;
		len += (size_t)n;
	}
}

/*
 * Does the driver attach to the hardware of one of the families today?
 * The table of the families says which family is reached.
 */
static int
nf_image_attaches(const struct nf_families *fams, const struct nf_strlist *families)
{
	size_t i;

	for (i = 0; i < families->n; i++) {
		if (nf_families_attaches(fams, families->items[i]))
			return (1);
	}
	return (0);
}

/* ------------------------------------------------------------- inventory */

/*
 * Load the share directory: the images of every driver version that is
 * installed.  The commands that need the firmware of a version use this.
 */
static int
nf_load_share(struct nf_config *cfg, struct nf_share *share)
{
	if (nf_share_load(cfg->share, share) != 0) {
		nf_error("cannot read %s", cfg->share);
		return (-1);
	}
	return (0);
}

/*
 * Load the family table of the share directory.  The table has to be
 * there: a package whose table is missing is broken and the tool does
 * not go on with half of the data.
 */
static int
nf_load_families_strict(struct nf_config *cfg, struct nf_families *fams)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cfg->share, NF_FAMILIES_FILE);
	if (nf_families_load(path, fams) != 0) {
		nf_error("cannot read %s", path);
		return (-1);
	}
	return (0);
}

/*
 * Load the family table.  The table of the share directory is used when
 * it is there; the table that is compiled into the program is used when
 * it is not, so that the cards of the machine can be named without the
 * firmware.  A table that is there but broken is an error and does not
 * fall back to the compiled one: the fallback would hide a package that
 * is broken.
 */
static int
nf_load_families(struct nf_config *cfg, struct nf_families *fams)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cfg->share, NF_FAMILIES_FILE);
	if (!nf_file_exists(path))
		return (nf_families_load_builtin(fams));
	return (nf_load_families_strict(cfg, fams));
}

/*
 * The cards of the machine, with the family of every one of them.
 */
static int
nf_load_cards(struct nf_families *fams, struct nf_card **cards, size_t *ncards)
{
	if (nf_cards_scan(cards, ncards) < 0) {
		nf_error("cannot read the PCI bus");
		return (-1);
	}
	nf_cards_families(*cards, *ncards, fams);
	return (0);
}

static void
nf_free_all(struct nf_share *share, struct nf_families *fams,
    struct nf_card *cards)
{
	free(cards);
	nf_families_free(fams);
	nf_share_free(share);
}

/*
 * Load everything that the commands that look at the machine need: the
 * share directory, the family table, the cards of the machine and the
 * inventory of the boot directory.  A boot directory whose inventory
 * cannot be read is an error and not an empty boot directory.
 */
static int
nf_load_all(struct nf_config *cfg, struct nf_share *share,
    struct nf_families *fams, struct nf_card **cards, size_t *ncards,
    struct nf_boot *boot)
{
	if (nf_load_share(cfg, share) != 0)
		return (-1);
	if (nf_load_families_strict(cfg, fams) != 0) {
		nf_share_free(share);
		return (-1);
	}
	if (nf_load_cards(fams, cards, ncards) != 0) {
		nf_free_all(share, fams, NULL);
		return (-1);
	}
	if (nf_boot_load(cfg->boot, boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_free_all(share, fams, *cards);
		*cards = NULL;
		*ncards = 0;
		return (-1);
	}
	return (0);
}

/* ---------------------------------------------------------------- detect */

/* The images the cards of the machine need for a version. */
static void
nf_detect_needed(const struct nf_card *cards, size_t ncards,
    const struct nf_families *fams, const char *version,
    struct nf_strlist *needed)
{
	size_t i;

	nf_strlist_init(needed);
	for (i = 0; i < ncards; i++) {
		struct nf_strlist images;
		size_t j;

		if (!cards[i].has_family)
			continue;
		if (nf_families_images(fams, version, cards[i].family,
		    &images) != 0)
			continue;
		for (j = 0; j < images.n; j++) {
			if (!nf_strlist_has(needed, images.items[j]))
				nf_strlist_add(needed, images.items[j]);
		}
		nf_strlist_free(&images);
	}
}

/* The forced images stay where they are. */
static void
nf_detect_forced(const struct nf_boot *boot, struct nf_strlist *forced)
{
	size_t i;

	nf_strlist_init(forced);
	for (i = 0; i < boot->nentries; i++) {
		if (boot->entries[i].forced &&
		    !nf_strlist_has(forced, boot->entries[i].name))
			nf_strlist_add(forced, boot->entries[i].name);
	}
}

/*
 * The images that stay in the boot directory: the ones the cards of the
 * machine need and the ones that are forced.
 */
static void
nf_detect_wanted(const struct nf_strlist *needed,
    const struct nf_strlist *forced, struct nf_strlist *wanted)
{
	size_t i;

	nf_strlist_init(wanted);
	for (i = 0; i < needed->n; i++)
		nf_strlist_add(wanted, needed->items[i]);
	for (i = 0; i < forced->n; i++) {
		if (!nf_strlist_has(wanted, forced->items[i]))
			nf_strlist_add(wanted, forced->items[i]);
	}
}

/*
 * Every image the cards need must be in the version that is going to be
 * used.  An image that is not there is an error and is named: the tool
 * does not place a firmware that is missing and does not take the old
 * one away either.
 */
static int
nf_detect_missing(const struct nf_share *share, const char *version,
    const struct nf_strlist *needed)
{
	const struct nf_share_version *sv;
	size_t i;

	sv = nf_share_version(share, version);
	if (sv == NULL) {
		nf_error("version %s is not installed", version);
		return (-1);
	}
	for (i = 0; i < needed->n; i++) {
		if (nf_share_image(sv, needed->items[i]) != NULL)
			continue;
		nf_error("the image %s is missing from version %s",
		    needed->items[i], version);
		return (-1);
	}
	return (0);
}

/*
 * Check one source of the share directory: it has to be a regular file
 * and its hash has to be the hash of the version.  The file is opened
 * without following a symbolic link, so a link is never taken for the
 * image of a version.
 */
static int
nf_source_check(const struct nf_config *cfg, const char *version,
    const struct nf_share_image *si)
{
	char src[PATH_MAX];
	char sha[NF_SHA256_LEN];

	if (si->sha[0] == '\0') {
		nf_error("the hash of %s/%s is not known", version, si->name);
		return (-1);
	}
	snprintf(src, sizeof(src), "%s/%s/%s", cfg->share, version, si->name);
	if (nf_sha256_regular(src, sha) != 0) {
		nf_error("%s: %s", src, strerror(errno));
		return (-1);
	}
	if (strcmp(sha, si->sha) != 0) {
		nf_error("%s: the hash is not the one of version %s", src,
		    version);
		return (-1);
	}
	return (0);
}

/*
 * A file of somebody else that has the name of an image is never
 * overwritten: the run stops and the file stays as it is.
 */
static int
nf_foreign_check(const struct nf_config *cfg, const struct nf_boot *boot,
    const char *image)
{
	if (!nf_boot_foreign_name(boot, image))
		return (0);
	nf_error("%s/%s is a file of somebody else; it is not overwritten",
	    cfg->boot, image);
	return (-1);
}

/*
 * Check every source before the boot directory is changed.
 *
 * A source that is not there, that is not a regular file or whose hash
 * is not the one of the version is an error: the boot directory is not
 * touched at all when the check fails.  The check runs in the dry run as
 * well, so that the dry run says the same thing as the run that follows
 * it.
 */
static int
nf_detect_preflight(const struct nf_config *cfg, const struct nf_share *share,
    const char *version, const struct nf_strlist *wanted,
    const struct nf_boot *boot)
{
	const struct nf_share_version *sv;
	size_t i;

	sv = nf_share_version(share, version);
	if (sv == NULL) {
		nf_error("version %s is not installed", version);
		return (-1);
	}
	for (i = 0; i < wanted->n; i++) {
		const char *image = wanted->items[i];
		const struct nf_share_image *si;

		si = nf_share_image(sv, image);
		if (si == NULL)
			continue;	/* a forced image of another version */
		if (nf_foreign_check(cfg, boot, image) != 0)
			return (-1);
		if (nf_source_check(cfg, version, si) != 0)
			return (-1);
	}
	return (0);
}

/*
 * The copies of one run.  Every copy is made before any of them is put
 * in place, so that a failure in a later copy leaves the boot directory
 * as it was.
 */
struct nf_copies {
	struct nf_stage *items;
	size_t n;
	size_t cap;
};

static void
nf_copies_init(struct nf_copies *copies)
{
	copies->items = NULL;
	copies->n = 0;
	copies->cap = 0;
}

static void
nf_copies_free(struct nf_copies *copies)
{
	size_t i;

	for (i = 0; i < copies->n; i++)
		nf_boot_discard(&copies->items[i]);
	free(copies->items);
	nf_copies_init(copies);
}

static int
nf_copies_add(struct nf_copies *copies, const struct nf_stage *stage)
{
	if (copies->n == copies->cap) {
		size_t cap = copies->cap ? copies->cap * 2 : 8;
		struct nf_stage *items;

		items = realloc(copies->items, cap * sizeof(*items));
		if (items == NULL)
			return (-1);
		copies->items = items;
		copies->cap = cap;
	}
	copies->items[copies->n++] = *stage;
	return (0);
}

/*
 * Is the image of the version already in place?  The file that is there
 * is hashed through its own descriptor: a file that was changed is not
 * the file of the version and is replaced.  A symbolic link is never
 * taken for an installed image.
 */
static int
nf_detect_in_place(const struct nf_boot_entry *be, const char *version,
    const char *sha, const char *path)
{
	char got[NF_SHA256_LEN];
	struct stat st;
	int fd;

	if (be == NULL)
		return (0);
	if (strcmp(be->version, version) != 0)
		return (0);
	if (strcmp(be->sha, sha) != 0)
		return (0);
	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return (0);
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return (0);
	}
	if (nf_sha256_fd(fd, got) != 0) {
		close(fd);
		return (0);
	}
	close(fd);
	return (strcmp(got, sha) == 0);
}

/*
 * Make a copy of every image that is not in place yet.  The copies are
 * not put in place here: that happens when every copy has been made.
 */
static int
nf_detect_place(struct nf_config *cfg, const struct nf_share *share,
    struct nf_boot *boot, const char *version, const struct nf_strlist *wanted,
    struct nf_copies *copies, struct nf_strlist *placed)
{
	size_t i;

	for (i = 0; i < wanted->n; i++) {
		const char *image = wanted->items[i];
		const struct nf_share_version *sv;
		const struct nf_share_image *si;
		struct nf_boot_entry *be;
		struct nf_stage stage;
		char src[PATH_MAX];
		char dst[PATH_MAX];

		sv = nf_share_version(share, version);
		si = nf_share_image(sv, image);
		if (si == NULL) {
			/*
			 * A forced image of another version is already in
			 * place; it is kept, not replaced by a file that does
			 * not exist.
			 */
			continue;
		}
		snprintf(src, sizeof(src), "%s/%s/%s", cfg->share, version,
		    image);
		snprintf(dst, sizeof(dst), "%s/%s", cfg->boot, image);
		be = nf_boot_find(boot, image);
		if (nf_detect_in_place(be, version, si->sha, dst))
			continue;	/* already the right file */
		if (cfg->dryrun) {
			/*
			 * The dry run changes nothing.  The sources were
			 * checked before, so the run that follows will
			 * place the image.
			 */
			nf_strlist_add(placed, image);
			continue;
		}
		if (nf_boot_stage(boot, image, version, si->sha, src, si->size,
		    be != NULL ? be->forced : 0, &stage) != 0) {
			nf_error("cannot place %s: %s", image, strerror(errno));
			return (-1);
		}
		if (nf_copies_add(copies, &stage) != 0) {
			nf_boot_discard(&stage);
			nf_error("cannot place %s: out of memory", image);
			return (-1);
		}
		nf_strlist_add(placed, image);
	}
	return (0);
}

/* Put the copies that were made in place and record them. */
static int
nf_detect_commit(struct nf_boot *boot, struct nf_copies *copies)
{
	size_t i;

	for (i = 0; i < copies->n; i++) {
		if (nf_boot_commit(boot, &copies->items[i]) != 0) {
			nf_error("cannot place %s: %s", copies->items[i].name,
			    strerror(errno));
			return (-1);
		}
	}
	return (0);
}

/* Take away the images that are not needed. */
static int
nf_detect_prune(struct nf_config *cfg, struct nf_boot *boot,
    const char *version, const struct nf_strlist *wanted,
    const struct nf_options *opt, struct nf_strlist *removed)
{
	int no_delete;
	size_t i = 0;

	no_delete = opt->no_delete || nf_rc_no_delete(cfg->rcconf, cfg->rcdir);
	while (i < boot->nentries) {
		struct nf_boot_entry *e = &boot->entries[i];
		char name[NF_IMAGE_LEN];

		snprintf(name, sizeof(name), "%s", e->name);
		if (nf_strlist_has(wanted, name) || e->forced || no_delete ||
		    (opt->keep_versions && strcmp(e->version, version) != 0)) {
			i++;
			continue;
		}
		if (nf_boot_remove(boot, name, cfg->dryrun) != 0) {
			nf_error("cannot remove %s: %s", name, strerror(errno));
			return (-1);
		}
		nf_strlist_add(removed, name);
		/*
		 * The entry is gone and the one that took its place is
		 * looked at next: the index stays where it is.
		 */
	}
	return (0);
}

static void
nf_detect_report_json(const char *version, size_t ncards,
    const struct nf_strlist *placed, const struct nf_strlist *removed,
    const struct nf_boot *boot)
{
	xo_open_container_h(NULL, "detect");
	nf_json_name_list("placed", placed);
	nf_json_name_list("removed", removed);
	nf_json_name_list("foreign", &boot->foreign);
	nf_json_field("version", "%s", version);
	nf_json_field("cards", "%d", (int)ncards);
	xo_close_container_h(NULL, "detect");
}

static void
nf_detect_report_text(const char *version, size_t ncards,
    const struct nf_strlist *placed, const struct nf_strlist *removed,
    const struct nf_boot *boot)
{
	size_t i;

	nf_text("version: {:version/%s}\n", version);
	nf_text("cards: {:count/%d}\n", (int)ncards);
	for (i = 0; i < placed->n; i++)
		nf_text("placed: {:name/%s}\n", placed->items[i]);
	for (i = 0; i < removed->n; i++)
		nf_text("removed: {:name/%s}\n", removed->items[i]);
	for (i = 0; i < boot->foreign.n; i++)
		nf_text("foreign: {:name/%s}\n", boot->foreign.items[i]);
}

/*
 * The state of one detect.  The command is split in two: the part that
 * reads everything and checks it, and the part that changes the boot
 * directory.
 */
struct nf_detect {
	struct nf_share share;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards;
	size_t ncards;
	struct nf_strlist needed;
	struct nf_strlist forced;
	struct nf_strlist wanted;
	struct nf_strlist placed;
	struct nf_strlist removed;
	struct nf_copies copies;
	const char *version;
};

static void
nf_detect_free(struct nf_detect *d)
{
	nf_copies_free(&d->copies);
	nf_strlist_free(&d->placed);
	nf_strlist_free(&d->removed);
	nf_strlist_free(&d->wanted);
	nf_strlist_free(&d->forced);
	nf_strlist_free(&d->needed);
	nf_boot_free(&d->boot);
	nf_free_all(&d->share, &d->fams, d->cards);
}

/*
 * Read everything the detect needs and check it.  Nothing in the boot
 * directory is changed here: every source is checked before the first
 * file of the boot directory is written or taken away.
 */
static int
nf_detect_prepare(struct nf_detect *d, struct nf_config *cfg,
    struct nf_options *opt)
{
	if (!nf_module_unloaded())
		return (-1);
	if (nf_load_share(cfg, &d->share) != 0)
		return (-1);
	if (nf_load_families_strict(cfg, &d->fams) != 0) {
		nf_share_free(&d->share);
		return (-1);
	}
	if (d->share.nversions == 0) {
		nf_error("no driver version is installed in %s", cfg->share);
		nf_free_all(&d->share, &d->fams, NULL);
		return (-1);
	}
	if (nf_load_cards(&d->fams, &d->cards, &d->ncards) != 0) {
		nf_free_all(&d->share, &d->fams, NULL);
		return (-1);
	}
	if (d->ncards == 0 && opt->expect_card) {
		nf_error("expected an NVIDIA GPU, found none");
		nf_free_all(&d->share, &d->fams, d->cards);
		d->cards = NULL;
		return (-1);
	}
	d->version = nf_pick_version(&d->share, opt->version);
	if (d->version == NULL) {
		nf_free_all(&d->share, &d->fams, d->cards);
		d->cards = NULL;
		return (-1);
	}
	nf_detect_needed(d->cards, d->ncards, &d->fams, d->version, &d->needed);
	/*
	 * Every image the cards need has to be in the version before
	 * anything in the boot directory is changed.
	 */
	if (nf_detect_missing(&d->share, d->version, &d->needed) != 0)
		return (-1);
	if (nf_boot_load(cfg->boot, &d->boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		return (-1);
	}
	nf_detect_forced(&d->boot, &d->forced);
	nf_detect_wanted(&d->needed, &d->forced, &d->wanted);
	/*
	 * Every source is checked before the first file of the boot
	 * directory is written or taken away.  A check that fails leaves
	 * the boot directory as it was, and the dry run fails in the same
	 * way as the run that follows it.
	 */
	if (nf_detect_preflight(cfg, &d->share, d->version, &d->wanted,
	    &d->boot) != 0)
		return (-1);
	return (0);
}

/*
 * Change the boot directory: place the images that are not in place yet,
 * take away the ones that are not needed and write the inventory.
 */
static int
nf_detect_execute(struct nf_detect *d, struct nf_config *cfg,
    struct nf_options *opt)
{
	if (nf_detect_place(cfg, &d->share, &d->boot, d->version, &d->wanted,
	    &d->copies, &d->placed) != 0)
		return (-1);
	if (nf_detect_commit(&d->boot, &d->copies) != 0) {
		/*
		 * The copies that were made are recorded, so that the
		 * directory stays one that the tool can work on.
		 */
		if (nf_boot_write(&d->boot, cfg->dryrun) != 0)
			nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		return (-1);
	}
	if (nf_detect_prune(cfg, &d->boot, d->version, &d->wanted, opt,
	    &d->removed) != 0)
		return (-1);
	if (nf_boot_write(&d->boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		return (-1);
	}
	return (nf_boot_cleanup(&d->boot, cfg->dryrun));
}

static int
nf_cmd_detect(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_detect d;
	int error;

	memset(&d, 0, sizeof(d));
	nf_copies_init(&d.copies);
	if (nf_detect_prepare(&d, cfg, opt) != 0) {
		nf_detect_free(&d);
		return (NF_EXIT_ERROR);
	}
	error = nf_detect_execute(&d, cfg, opt) != 0;
	if (cfg->json)
		nf_detect_report_json(d.version, d.ncards, &d.placed, &d.removed,
		    &d.boot);
	else
		nf_detect_report_text(d.version, d.ncards, &d.placed, &d.removed,
		    &d.boot);
	nf_detect_free(&d);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
}

/* ------------------------------------------------------------------ list */

/* The images a card needs for a version. */
static int
nf_card_image_list(const struct nf_families *fams, const char *version,
    const struct nf_card *card, struct nf_strlist *images)
{
	nf_strlist_init(images);
	if (!card->has_family)
		return (-1);
	return (nf_families_images(fams, version, card->family, images));
}

/* The name NVIDIA gives the chip in the file of the newest version. */
static void
nf_card_name(const struct nf_share *share, const struct nf_card *card,
    char *out, size_t size)
{
	const struct nf_share_version *sv;

	out[0] = '\0';
	sv = nf_share_version(share, nf_share_newest(share));
	if (sv == NULL)
		return;
	nf_share_gpu_name(sv, card->devid, out, size);
}

static void
nf_list_cards_text(const struct nf_share *share, const struct nf_families *fams,
    const struct nf_card *cards, size_t ncards, int installed)
{
	size_t i, j;

	for (i = 0; i < ncards; i++) {
		char name[NF_CARD_NAME_LEN];

		nf_card_name(share, &cards[i], name, sizeof(name));
		nf_text("{:selector/%s}  devid={:devid/0x%04x}  {:name/%s}  "
		    "family={:family/%s}\n", cards[i].selector, cards[i].devid,
		    name[0] != '\0' ? name : "-",
		    cards[i].has_family ? cards[i].family : "-");
		for (j = 0; j < share->nversions; j++) {
			struct nf_strlist images;
			size_t k;

			if (nf_card_image_list(fams, share->versions[j].version,
			    &cards[i], &images) != 0)
				continue;
			nf_text("  {:version/%-12s}",
			    share->versions[j].version);
			for (k = 0; k < images.n; k++)
				nf_text("{:image/%s} ", images.items[k]);
			nf_text("\n");
			nf_strlist_free(&images);
		}
	}
	nf_text("firmware: {:state/%s}\n",
	    installed ? "installed" : "not installed");
}

static void
nf_list_cards_json(const struct nf_share *share, const struct nf_families *fams,
    const struct nf_card *cards, size_t ncards, int installed)
{
	size_t i, j;

	xo_open_container_h(NULL, "list");
	nf_json_field("firmware", "%s", installed ? "installed" : "not installed");
	xo_open_list_h(NULL, "card");
	for (i = 0; i < ncards; i++) {
		char name[NF_CARD_NAME_LEN];

		nf_card_name(share, &cards[i], name, sizeof(name));
		xo_open_instance_h(NULL, "card");
		nf_json_field("selector", "%s", cards[i].selector);
		nf_json_field("devid", "0x%04x", cards[i].devid);
		nf_json_field("name", "%s", name);
		nf_json_field("family", "%s",
		    cards[i].has_family ? cards[i].family : "");
		xo_open_list_h(NULL, "image");
		for (j = 0; j < share->nversions; j++) {
			struct nf_strlist images;
			char list[512];

			if (nf_card_image_list(fams, share->versions[j].version,
			    &cards[i], &images) != 0)
				continue;
			nf_join(&images, list, sizeof(list));
			xo_open_instance_h(NULL, "image");
			nf_json_field("version", "%s",
			    share->versions[j].version);
			nf_json_field("names", "%s", list);
			xo_close_instance_h(NULL, "image");
			nf_strlist_free(&images);
		}
		xo_close_list_h(NULL, "image");
		xo_close_instance_h(NULL, "card");
	}
	xo_close_list_h(NULL, "card");
	xo_close_container_h(NULL, "list");
}

/*
 * The cards of the machine.  The list works when the firmware is not
 * installed: the family table is compiled into the program.
 */
static int
nf_list_cards(struct nf_config *cfg)
{
	struct nf_share share;
	struct nf_families fams;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	int installed;

	/* The share directory may be missing; the cards are listed anyway. */
	nf_share_load(cfg->share, &share);
	installed = nf_share_installed(&share);
	if (nf_load_families(cfg, &fams) != 0) {
		nf_error("cannot read the family table");
		nf_share_free(&share);
		return (NF_EXIT_ERROR);
	}
	if (nf_load_cards(&fams, &cards, &ncards) != 0) {
		nf_families_free(&fams);
		nf_share_free(&share);
		return (NF_EXIT_ERROR);
	}
	if (cfg->json)
		nf_list_cards_json(&share, &fams, cards, ncards, installed);
	else
		nf_list_cards_text(&share, &fams, cards, ncards, installed);
	free(cards);
	nf_families_free(&fams);
	nf_share_free(&share);
	return (NF_EXIT_OK);
}

/*
 * One row of the image list: the version and the image to show, with the
 * tables the row is rendered from.  The caller fills the row in and the
 * renderer reads it; one struct keeps the argument list of the renderer
 * short.
 */
struct nf_image_row {
	const struct nf_config *cfg;
	const struct nf_families *fams;
	const struct nf_share_version *sv;
	const struct nf_share_image *si;
	const struct nf_gpu *gpus;
	size_t ngpus;
	const struct nf_card *cards;
	size_t ncards;
	struct nf_boot *boot;
};

static void
nf_list_image_row(const struct nf_image_row *row)
{
	struct nf_strlist families;
	struct nf_strlist series;
	struct nf_strlist users;
	size_t nchips = 0;
	char famlist[256];
	char serieslist[256];
	char userlist[256];
	size_t j;
	int placed;

	nf_strlist_init(&families);
	nf_strlist_init(&series);
	nf_image_families(row->fams, row->sv->version, row->si->name, &families);
	for (j = 0; j < row->ngpus; j++) {
		const char *fam;
		const char *s;

		fam = nf_families_family(row->fams, row->gpus[j].devid);
		if (fam == NULL)
			continue;
		if (!nf_strlist_has(&families, fam))
			continue;
		nchips++;
		s = nf_families_series(row->fams, row->gpus[j].name);
		if (s != NULL && !nf_strlist_has(&series, s))
			nf_strlist_add(&series, s);
	}
	nf_join(&families, famlist, sizeof(famlist));
	nf_join(&series, serieslist, sizeof(serieslist));

	nf_cards_for_image(row->cards, row->ncards, row->fams,
	    row->sv->version, row->si->name, &users);
	nf_join(&users, userlist, sizeof(userlist));

	/*
	 * The image is placed when the file is there and when it is a
	 * regular file: the inventory says that the file is ours, the
	 * file itself says that it is there.  A file that is not there
	 * and a symbolic link are not an installed image.
	 */
	placed = nf_boot_placed(row->boot, row->si->name);

	if (row->cfg->json) {
		xo_open_instance_h(NULL, "image");
		nf_json_field("version", "%s", row->sv->version);
		nf_json_field("name", "%s", row->si->name);
		nf_json_field("size", "%lld", (long long)row->si->size);
		nf_json_field("families", "%s", famlist);
		nf_json_field("products", "%s", serieslist);
		nf_json_field("chips", "%d", (int)nchips);
		nf_json_field("attaches", "%s",
		    nf_image_attaches(row->fams, &families) ? "yes" : "not yet");
		nf_json_field("placed", "%s", placed ? "yes" : "no");
		nf_json_field("cards", "%s", userlist);
		xo_close_instance_h(NULL, "image");
	} else {
		nf_text("{:version/%-12s}{:image/%-22s}"
		    "{:size/%12lld}  {:families/%-26s}"
		    "{:products/%-64s}{:attaches/%-8s}"
		    "{:placed/%-4s}  {:cards/%s}\n",
		    row->sv->version, row->si->name, (long long)row->si->size,
		    famlist, serieslist,
		    nf_image_attaches(row->fams, &families) ? "yes" : "not yet",
		    placed ? "yes" : "no", userlist);
	}
	nf_strlist_free(&families);
	nf_strlist_free(&series);
	nf_strlist_free(&users);
}

/* Every installed version and every image of it. */
static int
nf_list_images(struct nf_config *cfg)
{
	struct nf_share share;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	size_t v;

	if (nf_load_all(cfg, &share, &fams, &cards, &ncards, &boot) != 0)
		return (NF_EXIT_ERROR);
	if (cfg->json) {
		xo_open_container_h(NULL, "list");
		xo_open_list_h(NULL, "image");
	}
	for (v = 0; v < share.nversions; v++) {
		struct nf_share_version *sv = &share.versions[v];
		struct nf_gpu *gpus;
		size_t ngpus, i;

		if (nf_share_gpus(sv, &gpus, &ngpus) != 0) {
			ngpus = 0;
			gpus = NULL;
		}
		for (i = 0; i < sv->nimages; i++) {
			struct nf_image_row row;

			row.cfg = cfg;
			row.fams = &fams;
			row.sv = sv;
			row.si = &sv->images[i];
			row.gpus = gpus;
			row.ngpus = ngpus;
			row.cards = cards;
			row.ncards = ncards;
			row.boot = &boot;
			nf_list_image_row(&row);
		}
		free(gpus);
	}
	if (cfg->json) {
		xo_close_list_h(NULL, "image");
		xo_close_container_h(NULL, "list");
	}
	nf_boot_free(&boot);
	nf_free_all(&share, &fams, cards);
	return (NF_EXIT_OK);
}

static int
nf_cmd_list(struct nf_config *cfg, struct nf_options *opt)
{
	if (opt->cards)
		return (nf_list_cards(cfg));
	return (nf_list_images(cfg));
}

/* ---------------------------------------------------------------- status */

static void
nf_status_json(const struct nf_families *fams, const struct nf_card *cards,
    size_t ncards, struct nf_boot *boot, const char *path, int no_delete)
{
	size_t i;

	xo_open_container_h(NULL, "status");
	nf_json_field("path", "%s", path);
	nf_json_field("no_delete", "%s", no_delete ? "on" : "off");
	xo_open_list_h(NULL, "image");
	for (i = 0; i < boot->nentries; i++) {
		struct nf_strlist users;
		char userlist[256];

		nf_cards_for_image(cards, ncards, fams, boot->entries[i].version,
		    boot->entries[i].name, &users);
		nf_join(&users, userlist, sizeof(userlist));
		xo_open_instance_h(NULL, "image");
		nf_json_field("name", "%s", boot->entries[i].name);
		nf_json_field("version", "%s", boot->entries[i].version);
		nf_json_field("size", "%lld",
		    (long long)boot->entries[i].size);
		nf_json_field("forced", "%s",
		    boot->entries[i].forced ? "yes" : "no");
		nf_json_field("cards", "%s", userlist);
		xo_close_instance_h(NULL, "image");
		nf_strlist_free(&users);
	}
	xo_close_list_h(NULL, "image");
	nf_json_name_list("foreign", &boot->foreign);
	xo_close_container_h(NULL, "status");
}

static void
nf_status_text(const struct nf_families *fams, const struct nf_card *cards,
    size_t ncards, struct nf_boot *boot, const char *path, int no_delete)
{
	size_t i;

	nf_text("boot: {:path/%s}\n", path);
	nf_text("no-delete: {:state/%s}\n", no_delete ? "on" : "off");
	for (i = 0; i < boot->nentries; i++) {
		struct nf_strlist users;
		char userlist[256];

		nf_cards_for_image(cards, ncards, fams,
		    boot->entries[i].version, boot->entries[i].name, &users);
		nf_join(&users, userlist, sizeof(userlist));
		nf_text("{:name/%-22s}{:version/%-12s}{:size/%12lld}  "
		    "{:forced/%-8s}  {:cards/%s}\n",
		    boot->entries[i].name, boot->entries[i].version,
		    (long long)boot->entries[i].size,
		    boot->entries[i].forced ? "forced" : "-", userlist);
		nf_strlist_free(&users);
	}
	for (i = 0; i < boot->foreign.n; i++)
		nf_text("foreign: {:name/%s}\n", boot->foreign.items[i]);
}

static int
nf_cmd_status(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_share share;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	int no_delete;

	(void)opt;
	if (nf_load_all(cfg, &share, &fams, &cards, &ncards, &boot) != 0)
		return (NF_EXIT_ERROR);
	no_delete = nf_rc_no_delete(cfg->rcconf, cfg->rcdir);
	if (cfg->json)
		nf_status_json(&fams, cards, ncards, &boot, cfg->boot,
		    no_delete);
	else
		nf_status_text(&fams, cards, ncards, &boot, cfg->boot,
		    no_delete);
	nf_boot_free(&boot);
	nf_free_all(&share, &fams, cards);
	return (NF_EXIT_OK);
}

/* ---------------------------------------------------------------- verify */

/*
 * Why a file is not an image: it is not there, it is a symbolic link or
 * it is not a regular file.
 */
static const char *
nf_verify_reason(int error)
{
	switch (error) {
	case ENOENT:
		return ("missing");
	case ELOOP:
		return ("a symbolic link is not an image");
	case EINVAL:
		return ("not a regular file");
	default:
		return ("cannot read");
	}
}

/*
 * Check one version of the share directory.  Every image that the
 * SHA256SUMS file names has to be there and has to have the hash the
 * file gives it; an image that is there but that the file does not name
 * is an error of its own.
 *
 * A symbolic link is never an image: the file is opened without
 * following a link and the descriptor has to be a regular file.
 */
static int
nf_verify_version(const struct nf_share_version *sv)
{
	char path[PATH_MAX];
	size_t i;
	int errors = 0;

	for (i = 0; i < sv->nexpected; i++) {
		const struct nf_share_expected *ex = &sv->expected[i];
		char sha[NF_SHA256_LEN];

		snprintf(path, sizeof(path), "%s/%s", sv->dir, ex->name);
		if (nf_sha256_regular(path, sha) != 0) {
			nf_error("%s/%s: %s", sv->version, ex->name,
			    nf_verify_reason(errno));
			errors++;
			continue;
		}
		if (strcmp(sha, ex->sha) != 0) {
			nf_error("%s/%s: changed", sv->version, ex->name);
			errors++;
		}
	}
	/* The images that are there but that the file of hashes does not name. */
	for (i = 0; i < sv->nimages; i++) {
		const struct nf_share_image *si = &sv->images[i];

		if (nf_share_expected(sv, si->name) != NULL)
			continue;
		nf_error("%s/%s: extra image", sv->version, si->name);
		errors++;
	}
	if (sv->nbadnames > 0) {
		nf_error("%s/%s: %d names are not file names", sv->version,
		    NF_SUM_FILE, (int)sv->nbadnames);
		errors++;
	}
	return (errors);
}

/*
 * Check the images of the boot directory.  A symbolic link is never an
 * installed image, and an image that is not there is not one either.
 */
static int
nf_verify_boot(const struct nf_config *cfg, const struct nf_boot *boot)
{
	char path[PATH_MAX];
	size_t i;
	int errors = 0;

	for (i = 0; i < boot->nentries; i++) {
		const struct nf_boot_entry *e = &boot->entries[i];
		char sha[NF_SHA256_LEN];

		snprintf(path, sizeof(path), "%s/%s", cfg->boot, e->name);
		if (nf_sha256_regular(path, sha) != 0) {
			nf_error("%s: %s", e->name, nf_verify_reason(errno));
			errors++;
			continue;
		}
		if (strcmp(sha, e->sha) != 0) {
			nf_error("%s: changed", e->name);
			errors++;
		}
	}
	return (errors);
}

static int
nf_cmd_verify(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_share share;
	struct nf_boot boot;
	size_t i;
	int errors = 0;

	(void)opt;
	if (nf_load_share(cfg, &share) != 0) {
		nf_error("cannot read %s", cfg->share);
		return (NF_EXIT_ERROR);
	}
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_share_free(&share);
		return (NF_EXIT_ERROR);
	}

	for (i = 0; i < share.nversions; i++)
		errors += nf_verify_version(&share.versions[i]);
	errors += nf_verify_boot(cfg, &boot);

	if (cfg->json) {
		xo_open_container_h(NULL, "verify");
		nf_json_field("result", "%s", errors == 0 ? "ok" : "failed");
		nf_json_field("errors", "%d", errors);
		nf_json_name_list("foreign", &boot.foreign);
		xo_close_container_h(NULL, "verify");
	} else {
		for (i = 0; i < boot.foreign.n; i++)
			nf_text("foreign: {:name/%s}\n", boot.foreign.items[i]);
		nf_text("verify: {:result/%s} ({:errors/%d} errors)\n",
		    errors == 0 ? "ok" : "failed", errors);
	}

	nf_boot_free(&boot);
	nf_share_free(&share);
	return (errors == 0 ? NF_EXIT_OK : NF_EXIT_ERROR);
}

/* --------------------------------------------------------------- install */

/*
 * Check every source of the install before the boot directory is
 * changed.  A source that is not there, that is not a regular file or
 * whose hash is not the one of the version is an error: the boot
 * directory is not touched at all when the check fails.  The check runs
 * in the dry run as well.
 */
static int
nf_install_preflight(const struct nf_config *cfg,
    const struct nf_share *share, const char *version,
    const struct nf_options *opt, const struct nf_boot *boot)
{
	const struct nf_share_version *sv;
	size_t i;

	sv = nf_share_version(share, version);
	if (sv == NULL) {
		nf_error("version %s is not installed", version);
		return (-1);
	}
	for (i = 0; i < sv->nimages; i++) {
		const struct nf_share_image *si = &sv->images[i];

		if (!opt->all && strcmp(si->name, opt->image) != 0)
			continue;
		if (nf_foreign_check(cfg, boot, si->name) != 0)
			return (-1);
		if (nf_source_check(cfg, version, si) != 0)
			return (-1);
	}
	return (0);
}

/*
 * Make a copy of every image of the install.  The copies are not put in
 * place here: that happens when every copy has been made.
 */
static int
nf_install_place(struct nf_config *cfg, const struct nf_share *share,
    struct nf_boot *boot, const char *version, const struct nf_options *opt,
    struct nf_copies *copies)
{
	size_t i, j;

	for (i = 0; i < share->nversions; i++) {
		const struct nf_share_version *sv = &share->versions[i];

		if (strcmp(sv->version, version) != 0)
			continue;
		for (j = 0; j < sv->nimages; j++) {
			const struct nf_share_image *si = &sv->images[j];
			struct nf_stage stage;
			char src[PATH_MAX];

			if (!opt->all && strcmp(si->name, opt->image) != 0)
				continue;
			snprintf(src, sizeof(src), "%s/%s/%s", cfg->share,
			    version, si->name);
			if (!cfg->dryrun) {
				if (nf_boot_stage(boot, si->name, version,
				    si->sha, src, si->size, 1, &stage) != 0) {
					nf_error("cannot place %s: %s", si->name,
					    strerror(errno));
					return (-1);
				}
				if (nf_copies_add(copies, &stage) != 0) {
					nf_boot_discard(&stage);
					nf_error("cannot place %s: out of memory",
					    si->name);
					return (-1);
				}
			}
			if (cfg->json) {
				xo_open_instance_h(NULL, "installed");
				nf_json_field("name", "%s", si->name);
				nf_json_field("version", "%s", version);
				nf_json_field("forced", "%s", "yes");
				xo_close_instance_h(NULL, "installed");
			} else {
				nf_text("installed: {:name/%s} ({:version/%s})\n",
				    si->name, version);
			}
		}
	}
	return (0);
}

/*
 * Read everything the install needs and check it.  Nothing in the boot
 * directory is changed here: every source is checked before the first
 * file of the boot directory is written.
 */
static int
nf_install_prepare(struct nf_config *cfg, struct nf_options *opt,
    struct nf_share *share, struct nf_boot *boot, const char **version)
{
	if (nf_load_share(cfg, share) != 0)
		return (-1);
	*version = nf_pick_version(share, opt->version);
	if (*version == NULL) {
		nf_share_free(share);
		return (-1);
	}
	if (nf_boot_load(cfg->boot, boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_share_free(share);
		return (-1);
	}
	if (!opt->all) {
		const struct nf_share_version *sv;

		sv = nf_share_version(share, *version);
		if (nf_share_image(sv, opt->image) == NULL) {
			nf_error("%s is not an image of version %s", opt->image,
			    *version);
			nf_boot_free(boot);
			nf_share_free(share);
			return (-1);
		}
	}
	/*
	 * Every source is checked before the first file of the boot
	 * directory is written.  A check that fails leaves the boot
	 * directory as it was, and the dry run fails in the same way as
	 * the run that follows it.
	 */
	if (nf_install_preflight(cfg, share, *version, opt, boot) != 0) {
		nf_boot_free(boot);
		nf_share_free(share);
		return (-1);
	}
	return (0);
}

static int
nf_cmd_install(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_share share;
	struct nf_boot boot;
	struct nf_copies copies;
	const char *version;
	int error = 0;

	if (!opt->all && opt->image == NULL) {
		nf_error("install needs --force IMAGE or --all");
		nf_usage();
		return (NF_EXIT_USAGE);
	}
	if (!opt->all && !nf_valid_image_name(opt->image)) {
		nf_error("%s is not the name of an image", opt->image);
		return (NF_EXIT_ERROR);
	}
	if (!nf_module_unloaded())
		return (NF_EXIT_ERROR);
	/*
	 * The install command needs the images of the share directory and
	 * nothing else: it does not need the family table.
	 */
	if (nf_install_prepare(cfg, opt, &share, &boot, &version) != 0)
		return (NF_EXIT_ERROR);

	nf_copies_init(&copies);
	if (cfg->json) {
		xo_open_container_h(NULL, "install");
		xo_open_list_h(NULL, "installed");
	}
	if (nf_install_place(cfg, &share, &boot, version, opt, &copies) != 0)
		error = 1;
	if (cfg->json) {
		xo_close_list_h(NULL, "installed");
		xo_close_container_h(NULL, "install");
	}
	if (error == 0) {
		size_t i;

		for (i = 0; i < copies.n; i++) {
			if (nf_boot_commit(&boot, &copies.items[i]) != 0) {
				nf_error("cannot place %s: %s",
				    copies.items[i].name, strerror(errno));
				error = 1;
				break;
			}
		}
	}
	nf_copies_free(&copies);
	if (error == 0 && nf_boot_write(&boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		error = 1;
	}

	nf_boot_free(&boot);
	nf_share_free(&share);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
}

/* ---------------------------------------------------------------- remove */

/*
 * The version the driver uses: the one it is pinned to, or the one the
 * image came from when nothing is pinned.
 */
static const char *
nf_remove_version(const struct nf_boot_entry *e)
{
	const char *pin;

	pin = nf_version_pin();
	if (pin != NULL && *pin != '\0')
		return (pin);
	return (e->version);
}

/*
 * Remove an image from the boot directory and drop its forced mark.  The
 * image is not removed while the loaded driver uses it, and a file that is
 * not one of ours is never removed.
 */
static int
nf_cmd_remove(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	struct nf_strlist users;
	struct nf_boot_entry *e;
	const char *version;
	size_t i;
	int error = 0;

	if (opt->arg == NULL) {
		nf_error("remove needs the name of an image");
		nf_usage();
		return (NF_EXIT_USAGE);
	}
	if (!nf_valid_image_name(opt->arg)) {
		nf_error("%s is not the name of an image", opt->arg);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The image of a card that is present may be in use by the loaded
	 * driver.  The guard does not depend on the card being found: the
	 * driver may use the image of a card that is not there.
	 */
	if (!nf_module_unloaded())
		return (NF_EXIT_ERROR);
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		return (NF_EXIT_ERROR);
	}
	e = nf_boot_find(&boot, opt->arg);
	if (e == NULL) {
		nf_error("%s is not one of our images", opt->arg);
		nf_boot_free(&boot);
		return (NF_EXIT_ERROR);
	}
	version = nf_remove_version(e);
	if (nf_load_families(cfg, &fams) != 0) {
		nf_error("cannot read the family table");
		nf_boot_free(&boot);
		return (NF_EXIT_ERROR);
	}
	if (nf_load_cards(&fams, &cards, &ncards) != 0) {
		nf_families_free(&fams);
		nf_boot_free(&boot);
		return (NF_EXIT_ERROR);
	}
	nf_cards_for_image(cards, ncards, &fams, version, opt->arg, &users);
	for (i = 0; i < users.n; i++)
		nf_warn("%s needs %s", users.items[i], opt->arg);
	nf_strlist_free(&users);
	if (nf_boot_remove(&boot, opt->arg, cfg->dryrun) != 0) {
		nf_error("cannot remove %s: %s", opt->arg, strerror(errno));
		error = 1;
	}
	if (error == 0 && nf_boot_write(&boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		error = 1;
	}
	if (error == 0)
		nf_boot_cleanup(&boot, cfg->dryrun);
	/*
	 * The result is only told when the image really went away: a run
	 * that failed does not say that it removed anything.
	 */
	if (error == 0) {
		if (cfg->json) {
			xo_open_container_h(NULL, "remove");
			nf_json_field("name", "%s", opt->arg);
			nf_json_field("forced", "%s", "no");
			xo_close_container_h(NULL, "remove");
		} else {
			nf_text("removed: {:name/%s}\n", opt->arg);
		}
	}
	free(cards);
	nf_families_free(&fams);
	nf_boot_free(&boot);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
}

/* ----------------------------------------------------------------- clean */

/*
 * Check the files that are to be taken away before any of them is
 * removed.  A name of the inventory that is a directory cannot be
 * removed: nothing is removed then.
 */
static int
nf_clean_preflight(const struct nf_config *cfg, const struct nf_boot *boot)
{
	size_t i;

	for (i = 0; i < boot->nentries; i++) {
		char path[PATH_MAX];
		struct stat st;

		if (!nf_valid_image_name(boot->entries[i].name)) {
			nf_error("%s: %s is not the name of an image", cfg->boot,
			    boot->entries[i].name);
			return (-1);
		}
		snprintf(path, sizeof(path), "%s/%s", cfg->boot,
		    boot->entries[i].name);
		if (lstat(path, &st) != 0) {
			if (errno == ENOENT)
				continue;	/* it is not there any more */
			nf_error("%s: %s", path, strerror(errno));
			return (-1);
		}
		if (S_ISDIR(st.st_mode)) {
			nf_error("%s: %s is a directory", cfg->boot,
			    boot->entries[i].name);
			return (-1);
		}
	}
	return (0);
}

static int
nf_cmd_clean(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_boot boot;
	size_t i;

	(void)opt;
	if (!nf_module_unloaded())
		return (NF_EXIT_ERROR);
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The files that go away are checked before the first one of them
	 * is taken away.
	 */
	if (nf_clean_preflight(cfg, &boot) != 0) {
		nf_boot_free(&boot);
		return (NF_EXIT_ERROR);
	}
	if (cfg->json) {
		xo_open_container_h(NULL, "clean");
		xo_open_list_h(NULL, "removed");
	}
	for (i = 0; i < boot.nentries; i++) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s", cfg->boot,
		    boot.entries[i].name);
		if (!cfg->dryrun && unlink(path) != 0 && errno != ENOENT) {
			nf_error("cannot remove %s: %s", boot.entries[i].name,
			    strerror(errno));
			if (cfg->json)
				xo_close_list_h(NULL, "removed");
			if (cfg->json)
				xo_close_container_h(NULL, "clean");
			nf_boot_free(&boot);
			return (NF_EXIT_ERROR);
		}
		if (cfg->json) {
			xo_open_instance_h(NULL, "removed");
			nf_json_field("name", "%s", boot.entries[i].name);
			xo_close_instance_h(NULL, "removed");
		} else {
			nf_text("removed: {:name/%s}\n", boot.entries[i].name);
		}
	}
	boot.nentries = 0;
	if (cfg->json) {
		xo_close_list_h(NULL, "removed");
		xo_close_container_h(NULL, "clean");
	}
	if (nf_boot_write(&boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		nf_boot_free(&boot);
		return (NF_EXIT_ERROR);
	}
	nf_boot_cleanup(&boot, cfg->dryrun);
	nf_boot_free(&boot);
	return (NF_EXIT_OK);
}

/* ------------------------------------------------------------------ main */

static void
nf_usage(void)
{
	fprintf(stderr,
	"usage: nvidia-firmware command [options]\n"
	"commands:\n"
	"  detect [-n] [--no-delete] [--keep-versions] [--expect-card] [--version V]\n"
	"  list [--cards]\n"
	"  status\n"
	"  verify\n"
	"  install [-n] [--force IMAGE | --all] [--version V]\n"
	"  remove [-n] IMAGE\n"
	"  clean [-n]\n"
	"options:\n"
	"  -n                 change nothing\n"
	"  --no-delete        keep the images that are not needed\n"
	"  --keep-versions    keep the images of the older versions\n"
	"  --expect-card      fail when the machine has no NVIDIA adapter\n"
	"  --cards            list the adapters of the machine\n"
	"  --all              every image of the version\n"
	"  --force IMAGE      place one image and mark it forced\n"
	"  --version VERSION  the driver version to use\n"
	"  --json             write JSON instead of text\n"
	"  --libxo json       write JSON instead of text\n");
}

/*
 * One option of the command line: a flag that is set, or a value that is
 * taken from the argument behind it.
 */
struct nf_option {
	const char *name;
	int *flag;
	const char **value;
};

/* One command of the tool. */
struct nf_command {
	const char *name;
	int (*run)(struct nf_config *, struct nf_options *);
};

static const struct nf_option *
nf_option_find(const struct nf_option *options, size_t n, const char *name)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (strcmp(options[i].name, name) == 0)
			return (&options[i]);
	}
	return (NULL);
}

int
main(int argc, char **argv)
{
	static const struct nf_command commands[] = {
		{ "detect", nf_cmd_detect },
		{ "list", nf_cmd_list },
		{ "status", nf_cmd_status },
		{ "verify", nf_cmd_verify },
		{ "install", nf_cmd_install },
		{ "remove", nf_cmd_remove },
		{ "clean", nf_cmd_clean },
	};
	struct nf_config cfg;
	struct nf_options opt;
	struct nf_option options[] = {
		{ "-n", &cfg.dryrun, NULL },
		{ "--no-delete", &opt.no_delete, NULL },
		{ "--keep-versions", &opt.keep_versions, NULL },
		{ "--expect-card", &opt.expect_card, NULL },
		{ "--cards", &opt.cards, NULL },
		{ "--all", &opt.all, NULL },
		{ "--force", NULL, &opt.image },
		{ "--version", NULL, &opt.version },
	};
	const struct nf_command *cmd = NULL;
	const char *command;
	size_t i;
	int result;

	memset(&cfg, 0, sizeof(cfg));
	memset(&opt, 0, sizeof(opt));

	/*
	 * libxo reads its own options out of the argument list.  The alias
	 * --json is turned into the option libxo understands.
	 */
	for (i = 1; i < (size_t)argc; i++) {
		if (strcmp(argv[i], "--json") == 0) {
			char *alias = nf_strdup("--libxo=json");

			if (alias == NULL)
				return (NF_EXIT_ERROR);
			argv[i] = alias;
		}
	}
	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return (NF_EXIT_ERROR);
	cfg.json = (xo_get_style(NULL) == XO_STYLE_JSON);

	if (argc < 2) {
		nf_usage();
		return (NF_EXIT_USAGE);
	}
	command = argv[1];

	for (i = 2; i < (size_t)argc; i++) {
		const char *arg = argv[i];
		const struct nf_option *o;

		o = nf_option_find(options,
		    sizeof(options) / sizeof(options[0]), arg);
		if (o != NULL) {
			if (o->flag != NULL) {
				*o->flag = 1;
				continue;
			}
			if (i + 1 >= (size_t)argc) {
				nf_usage();
				return (NF_EXIT_USAGE);
			}
			*o->value = argv[++i];
			continue;
		}
		if (arg[0] == '-') {
			nf_error("unknown option: %s", arg);
			nf_usage();
			return (NF_EXIT_USAGE);
		}
		/* A word of its own, such as the image of remove. */
		opt.arg = arg;
	}

	nf_paths(&cfg);

	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		if (strcmp(command, commands[i].name) == 0) {
			cmd = &commands[i];
			break;
		}
	}
	if (cmd == NULL) {
		nf_error("unknown command: %s", command);
		nf_usage();
		return (NF_EXIT_USAGE);
	}
	result = cmd->run(&cfg, &opt);

	xo_finish_h(NULL);
	return (result);
}
