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
 * machine need into /boot/firmware/nvidia.
 *
 * The firmware is never shipped with the package.  The package holds the
 * pin table, which says which file belongs to which driver version and
 * how big it is and what its hash is, and the tool fetches the file when
 * it is needed.  Every file is checked against the pin table before it is
 * put in place, and a file that does not match is never placed.
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
#include "nf_fetch.h"
#include "nf_pin.h"
#include "nf_rc.h"
#include "nf_util.h"

#define	NF_EXIT_OK	0
#define	NF_EXIT_ERROR	1
#define	NF_EXIT_USAGE	2
/*
 * The command line is good and the command may run.  It is not an exit
 * code: it is only what the parser of the options answers.
 */
#define	NF_CONTINUE	(-1)

#define	NF_VERSION_PIN	"hw.nvidia.version_pin"

/*
 * The version of the program.  It is our own version: the date of the day
 * the package was made.  The versions of the NVIDIA driver are data in
 * the pin table and are never the version of the program.
 */
struct nf_our_version {
	char text[NF_VERSION_LEN];
	int major;
	int minor;
	int patch;
};

static struct nf_our_version nf_version;

struct nf_config {
	char data[PATH_MAX];		/* the directory the data files are in */
	char boot[PATH_MAX];
	char rcconf[PATH_MAX];
	char rcdir[PATH_MAX];
	const char *mirror;		/* the directory the firmware comes from */
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
	int supported;
	const char *driver;		/* the value of --driver-version */
	const char *image;		/* the value of --force */
	const char *from;		/* the value of --from */
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

/*
 * The version of the program and the version of the schema of the output.
 * Every JSON object of the program carries them, so that a program that
 * reads the output can tell what it is reading.
 */
static void
nf_json_version(void)
{
	nf_json_field("schema_version", "%s", NF_SCHEMA_VERSION);
	nf_json_field("version", "%s", nf_version.text);
	nf_json_field("version_major", "%d", nf_version.major);
	nf_json_field("version_minor", "%d", nf_version.minor);
	nf_json_field("version_patch", "%d", nf_version.patch);
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
		snprintf(cfg->data, sizeof(cfg->data),
		    "%s/share/nvidia-firmware", root);
		snprintf(cfg->boot, sizeof(cfg->boot),
		    "%s/boot/firmware/nvidia", root);
		snprintf(cfg->rcconf, sizeof(cfg->rcconf), "%s/etc/rc.conf",
		    root);
		snprintf(cfg->rcdir, sizeof(cfg->rcdir),
		    "%s/etc/rc.conf.d/nvidia_firmware", root);
		return;
	}
	snprintf(cfg->data, sizeof(cfg->data), "%s", NF_DATA_DIR);
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
 * The lock of the tool.  Every command that changes the boot directory
 * takes it, and the state of the nvidia module is read under it.  The
 * check and the action are one thing then: no run of the tool can be in
 * the middle of them, and the state that was read is the state the whole
 * run uses.
 *
 * The lock file is in /var/run, which only root can write in.  A lock
 * that cannot be taken is an error: the tool does not change the boot
 * directory when it cannot be sure that it is alone in it.
 */
#define	NF_LOCK_PATH	"/var/run/nvidia-firmware.lock"

struct nf_guard {
	int fd;		/* the lock file, -1 when there is none */
	int state;	/* the state of the nvidia module */
};

static const char *
nf_lock_path(void)
{
#ifdef NVIDIA_FIRMWARE_TESTING
	const char *test;

	test = getenv("NVIDIA_FIRMWARE_TEST_LOCK");
	if (test != NULL && *test != '\0')
		return (test);
#endif
	return (NF_LOCK_PATH);
}

/*
 * Take the lock and read the state of the nvidia module under it.
 */
static int
nf_guard_open(struct nf_guard *g)
{
	struct flock fl;
	int error;

	g->fd = -1;
	g->state = NF_KLD_UNKNOWN;
	g->fd = open(nf_lock_path(), O_RDWR | O_CREAT, 0600);
	if (g->fd < 0)
		return (-1);
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	if (fcntl(g->fd, F_SETLK, &fl) != 0) {
		error = errno;
		close(g->fd);
		g->fd = -1;
		errno = error;
		return (-1);
	}
	g->state = nf_nvidia_loaded();
	return (0);
}

static void
nf_guard_close(struct nf_guard *g)
{
	if (g->fd >= 0) {
		close(g->fd);
		g->fd = -1;
	}
}

/*
 * The lock could not be taken.  The lock file is in /var/run, which only
 * root may write in, so the usual reason is that the tool was not run as
 * root: the message says so, because "Permission denied" alone leaves the
 * reader without an idea of what to do.
 */
static void
nf_guard_failed(void)
{
	nf_error("cannot take the lock of the tool: %s; the lock is in %s and "
	    "only root may write it: run this as root (doas nvidia-firmware "
	    "...)", strerror(errno), nf_lock_path());
}

/*
 * Is the nvidia module still in the state the guard found?  The state is
 * read again under the lock, right before the boot directory is changed:
 * a run that changes the firmware of a driver that has come up in the
 * meantime changes the wrong thing.
 *
 * The lock of the tool is not a lock of the kernel: kldload(8) does not
 * take it, so a module that is loaded between the two readings is not
 * seen.  The state is read again before every change of the boot
 * directory, which makes the window as small as the tool can make it,
 * and the page of the tool says what the window is.
 */
static int
nf_guard_still_unloaded(struct nf_guard *g)
{
	int state;

	state = nf_nvidia_loaded();
	if (state == NF_KLD_LOADED) {
		nf_error("the nvidia module was loaded while the tool was "
		    "working; nothing was changed");
		return (0);
	}
	if (state == NF_KLD_UNKNOWN) {
		nf_error("cannot tell whether the nvidia module is loaded; "
		    "kldstat(8) has to be there and has to work");
		return (0);
	}
	g->state = state;
	return (1);
}

/*
 * May the boot directory be changed?  The nvidia module must not be
 * loaded, and the state of the module must be known: a tool that cannot
 * tell whether the driver uses the firmware does not touch it.
 */
static int
nf_guard_allows(const struct nf_guard *g)
{
	if (g->state == NF_KLD_LOADED) {
		nf_error("the nvidia module is loaded, unload it first");
		return (0);
	}
	if (g->state == NF_KLD_UNKNOWN) {
		nf_error("cannot tell whether the nvidia module is loaded; "
		    "kldstat(8) has to be there and has to work");
		return (0);
	}
	return (1);
}

/*
 * May a file that is not there yet be put in the boot directory?  A file
 * that the driver does not have cannot be the firmware of the driver:
 * the state of the module still has to be known, because a run that
 * cannot tell whether the driver uses the firmware does not touch it.
 */
static int
nf_guard_allows_place(const struct nf_guard *g)
{
	if (g->state == NF_KLD_UNKNOWN) {
		nf_error("cannot tell whether the nvidia module is loaded; "
		    "kldstat(8) has to be there and has to work");
		return (0);
	}
	return (1);
}

/* -------------------------------------------------------------- versions */

/*
 * The version the driver uses: the one it is pinned to.  A pin to a
 * version that the pin table does not name is an error and not a reason
 * to use another one: the driver would get the firmware of a version it
 * does not want.
 */
static const char *
nf_pick_version(const struct nf_pin *pin, const char *wanted)
{
	const char *pinned;
	const char *newest;

	if (wanted != NULL && *wanted != '\0') {
		if (!nf_pin_has_version(pin, wanted)) {
			nf_error("the driver version %s is not in the pin "
			    "table", wanted);
			return (NULL);
		}
		return (wanted);
	}
	pinned = nf_version_pin();
	if (pinned != NULL && *pinned != '\0') {
		if (nf_pin_has_version(pin, pinned))
			return (pinned);
		nf_error("the version the driver is pinned to (%s) is not in "
		    "the pin table", pinned);
		return (NULL);
	}
	newest = nf_pin_newest(pin);
	if (newest == NULL)
		nf_error("the pin table names no driver version");
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
 * The lines of products of a family, in parentheses.  The list of the
 * series belongs to this function: it is freed here and the caller does
 * not own it.
 */
static void
nf_image_series(const struct nf_families *fams, const char *family, char *out,
    size_t size)
{
	struct nf_strlist series;
	size_t j;
	size_t len = 0;
	int n;

	out[0] = '\0';
	if (nf_families_series_of(fams, family, &series) != 0)
		return;
	if (series.n == 0) {
		nf_strlist_free(&series);
		return;
	}
	n = snprintf(out, size, " (");
	if (n < 0 || (size_t)n >= size) {
		nf_strlist_free(&series);
		return;
	}
	len = (size_t)n;
	for (j = 0; j < series.n; j++) {
		n = snprintf(out + len, size - len, "%s%s", j > 0 ? ", " : "",
		    series.items[j]);
		if (n < 0 || (size_t)n >= size - len)
			break;
		len += (size_t)n;
	}
	nf_strlist_free(&series);
	if (n < 0 || (size_t)n >= size - len)
		return;
	snprintf(out + len, size - len, ")");
}

/*
 * The families an image serves and the lines of products of those
 * families: what the image is for.
 */
static void
nf_image_serves(const struct nf_families *fams, const char *version,
    const char *image, char *out, size_t size)
{
	struct nf_strlist families;
	size_t i;
	size_t len = 0;
	int n;

	out[0] = '\0';
	nf_image_families(fams, version, image, &families);
	for (i = 0; i < families.n; i++) {
		char series[256];

		n = snprintf(out + len, size - len, "%s%s", i > 0 ? ", " : "",
		    families.items[i]);
		if (n < 0 || (size_t)n >= size - len)
			break;
		len += (size_t)n;
		nf_image_series(fams, families.items[i], series, sizeof(series));
		if (series[0] == '\0')
			continue;
		n = snprintf(out + len, size - len, "%s", series);
		if (n < 0 || (size_t)n >= size - len)
			break;
		len += (size_t)n;
	}
	nf_strlist_free(&families);
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
 * Read the pin table of the package.  The table says which file belongs
 * to which driver version and how big it is and what its hash is.
 */
static int
nf_load_pin(struct nf_config *cfg, struct nf_pin *pin)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cfg->data, NF_PIN_FILE);
	if (nf_pin_load(path, pin) != 0) {
		nf_error("cannot read %s", path);
		return (-1);
	}
	return (0);
}

/*
 * Load the family table of the package.  The table has to be there: a
 * package whose table is missing is broken and the tool does not go on
 * with half of the data.
 */
static int
nf_load_families_strict(struct nf_config *cfg, struct nf_families *fams)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cfg->data, NF_FAMILIES_FILE);
	if (nf_families_load(path, fams) != 0) {
		nf_error("cannot read %s", path);
		return (-1);
	}
	return (0);
}

/*
 * Load the family table.  The table of the package is used when it is
 * there; the table that is compiled into the program is used when it is
 * not, so that the cards of the machine can be named without the
 * firmware.  A table that is there but broken is an error and does not
 * fall back to the compiled one: the fallback would hide a package that
 * is broken.
 */
static int
nf_load_families(struct nf_config *cfg, struct nf_families *fams)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cfg->data, NF_FAMILIES_FILE);
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
nf_free_all(struct nf_pin *pin, struct nf_families *fams,
    struct nf_card *cards)
{
	free(cards);
	nf_families_free(fams);
	nf_pin_free(pin);
}

/*
 * Load everything that the commands that look at the machine need: the
 * pin table, the family table, the cards of the machine and the inventory
 * of the boot directory.  A boot directory whose inventory cannot be
 * read is an error and not an empty boot directory.
 */
static int
nf_load_all(struct nf_config *cfg, struct nf_pin *pin,
    struct nf_families *fams, struct nf_card **cards, size_t *ncards,
    struct nf_boot *boot)
{
	if (nf_load_pin(cfg, pin) != 0)
		return (-1);
	if (nf_load_families_strict(cfg, fams) != 0) {
		nf_pin_free(pin);
		return (-1);
	}
	if (nf_load_cards(fams, cards, ncards) != 0) {
		nf_free_all(pin, fams, NULL);
		return (-1);
	}
	if (nf_boot_load(cfg->boot, boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_free_all(pin, fams, *cards);
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
 * Every image the cards need must be in the pin table for the version
 * that is going to be used.  An image that is not there is an error and
 * is named: the tool does not place a firmware that is missing and does
 * not take the old one away either.
 */
static int
nf_detect_missing(const struct nf_pin *pin, const char *version,
    const struct nf_strlist *needed)
{
	size_t i;

	for (i = 0; i < needed->n; i++) {
		if (nf_pin_image(pin, version, needed->items[i]) != NULL)
			continue;
		nf_error("the image %s is not in the pin table for the driver "
		    "version %s", needed->items[i], version);
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
 * The same check for every file the install is about to place.
 */
static int
nf_install_foreign_check(const struct nf_config *cfg,
    const struct nf_boot *boot, const struct nf_strlist *images)
{
	size_t i;

	for (i = 0; i < images->n; i++) {
		if (nf_foreign_check(cfg, boot, images->items[i]) != 0)
			return (-1);
	}
	return (0);
}

/*
 * Is the image of the version already in place?  The file that is there
 * is hashed through its own descriptor: a file that was changed is not
 * the file of the version and is replaced.  A symbolic link is never
 * taken for an installed image.
 */
static int
nf_detect_in_place(const struct nf_image *be, const char *version,
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
 * The files that have to be fetched: the ones that are wanted and that
 * are not in place yet.
 *
 * Every file of somebody else that has the name of an image is refused
 * before anything is fetched, and an image that the pin table does not
 * name for the version is a forced image of another version: it is kept,
 * not replaced by a file that does not exist.
 *
 * A file that is in place is never replaced when the driver may be using
 * it: only the run that may change the boot directory replaces it.
 */
static int
nf_detect_missing_files(struct nf_config *cfg, const struct nf_pin *pin,
    const char *version, const struct nf_strlist *wanted,
    const struct nf_boot *boot, int may_replace, struct nf_strlist *todo)
{
	size_t i;

	nf_strlist_init(todo);
	for (i = 0; i < wanted->n; i++) {
		const char *image = wanted->items[i];
		const struct nf_image *pi;
		const struct nf_image *be;
		char path[PATH_MAX];

		pi = nf_pin_image(pin, version, image);
		if (pi == NULL)
			continue;	/* a forced image of another version */
		if (nf_foreign_check(cfg, boot, image) != 0) {
			nf_strlist_free(todo);
			return (-1);
		}
		be = nf_boot_find((struct nf_boot *)boot, image);
		snprintf(path, sizeof(path), "%s/%s", cfg->boot, image);
		if (nf_detect_in_place(be, version, pi->sha, path))
			continue;	/* already the right file */
		/*
		 * The inventory names the file but the file is not there: it
		 * was taken away by hand.  The file is put in place again,
		 * and the driver that is running cannot be using a file
		 * that is not there.
		 */
		if (be != NULL && !may_replace && nf_regular_file(path)) {
			nf_error("%s is in the boot directory and the nvidia "
			    "module may be using it; it is not replaced",
			    image);
			nf_strlist_free(todo);
			return (-1);
		}
		if (nf_strlist_add(todo, image) != 0) {
			nf_strlist_free(todo);
			return (-1);
		}
	}
	return (0);
}

/*
 * Put the copies that were made in place and record them.  The forced
 * mark says that the image is there because it was asked for and not
 * because it is needed.
 *
 * The mark of an image that the inventory already names is kept: a file
 * that was forced stays forced when it is replaced.  The mark of a new
 * entry is the one of the run: install asks for one and detect does not.
 *
 * A file that the inventory does not name is a file of somebody else and
 * is never replaced: the check is the last one before the rename.
 *
 * The names of the files that were really put in place are put in the
 * list that is given, so that the run can tell what it did from what it
 * wanted to do.
 */
static int
nf_images_commit(struct nf_boot *boot, const struct nf_fetch *fetch, int forced,
    struct nf_strlist *placed)
{
	size_t i;

	for (i = 0; i < fetch->n; i++) {
		struct nf_stage stage;
		struct nf_image *e;
		struct stat st;
		char dst[PATH_MAX];

		memset(&stage, 0, sizeof(stage));
		snprintf(stage.name, sizeof(stage.name), "%s",
		    fetch->items[i].name);
		snprintf(stage.version, sizeof(stage.version), "%s",
		    fetch->items[i].version);
		snprintf(stage.sha, sizeof(stage.sha), "%s", fetch->items[i].sha);
		snprintf(stage.tmp, sizeof(stage.tmp), "%s",
		    fetch->items[i].tmp);
		stage.size = fetch->items[i].size;
		snprintf(dst, sizeof(dst), "%s/%s", boot->dir, stage.name);
		e = nf_boot_find(boot, stage.name);
		if (e != NULL)
			stage.forced = e->forced || forced;
		else
			stage.forced = forced;
		if (e == NULL && lstat(dst, &st) == 0) {
			nf_error("%s is a file of somebody else; it is not "
			    "overwritten", dst);
			return (-1);
		}
		if (nf_commit_staged(stage.tmp, dst) != 0) {
			nf_error("cannot place %s: %s", stage.name,
			    strerror(errno));
			return (-1);
		}
		stage.tmp[0] = '\0';
		if (nf_boot_set(boot, stage.name, stage.version, stage.sha,
		    stage.size, stage.forced) != 0) {
			/*
			 * The file is in place and the inventory does not
			 * name it: it would be a file of somebody else.  It
			 * is taken away again.
			 */
			nf_error("cannot record %s: %s", stage.name,
			    strerror(errno));
			unlink(dst);
			return (-1);
		}
		if (placed != NULL &&
		    nf_strlist_add(placed, stage.name) != 0) {
			nf_error("out of memory");
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
	int policy;
	int failed = 0;
	size_t i = 0;

	no_delete = opt->no_delete;
	if (nf_rc_no_delete(cfg->rcconf, cfg->rcdir, &policy) != 0) {
		/*
		 * The policy cannot be read.  The tool cannot tell that it
		 * may take the images away, so it takes none of them away
		 * and says that it did not.
		 */
		nf_error("cannot read the no-delete policy: %s; "
		    "no image was deleted", strerror(errno));
		policy = 1;
		failed = 1;
	}
	if (policy != 0)
		no_delete = 1;
	while (i < boot->nentries) {
		struct nf_image *e = &boot->entries[i];
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
	return (failed ? -1 : 0);
}

static void
nf_detect_report_json(const char *version, size_t ncards,
    const struct nf_strlist *placed, const struct nf_strlist *removed,
    const struct nf_boot *boot)
{
	xo_open_container_h(NULL, "detect");
	nf_json_version();
	nf_json_name_list("placed", placed);
	nf_json_name_list("removed", removed);
	nf_json_name_list("foreign", &boot->foreign);
	nf_json_field("driver_version", "%s", version);
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
	struct nf_pin pin;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards;
	size_t ncards;
	struct nf_strlist needed;
	struct nf_strlist forced;
	struct nf_strlist wanted;
	struct nf_strlist todo;
	struct nf_strlist placed;
	struct nf_strlist removed;
	struct nf_fetch fetch;
	const char *version;
	int prune;			/* 1: the images that are not needed go */
};

static void
nf_detect_free(struct nf_detect *d)
{
	nf_fetch_free(&d->fetch);
	nf_strlist_free(&d->placed);
	nf_strlist_free(&d->removed);
	nf_strlist_free(&d->todo);
	nf_strlist_free(&d->wanted);
	nf_strlist_free(&d->forced);
	nf_strlist_free(&d->needed);
	nf_boot_free(&d->boot);
	nf_free_all(&d->pin, &d->fams, d->cards);
}

/*
 * Read everything the detect needs and check it.  Nothing in the boot
 * directory is changed here: every file that is fetched is checked
 * against the pin table before it is put in place.
 */
static int
nf_detect_prepare(struct nf_detect *d, struct nf_config *cfg,
    struct nf_options *opt, int may_replace)
{
	if (nf_load_pin(cfg, &d->pin) != 0)
		return (-1);
	if (nf_load_families_strict(cfg, &d->fams) != 0) {
		nf_pin_free(&d->pin);
		return (-1);
	}
	if (d->pin.nimages == 0) {
		nf_error("the pin table names no file");
		nf_free_all(&d->pin, &d->fams, NULL);
		return (-1);
	}
	if (nf_load_cards(&d->fams, &d->cards, &d->ncards) != 0) {
		nf_free_all(&d->pin, &d->fams, NULL);
		return (-1);
	}
	if (d->ncards == 0 && opt->expect_card) {
		nf_error("expected an NVIDIA GPU, found none");
		nf_free_all(&d->pin, &d->fams, d->cards);
		d->cards = NULL;
		return (-1);
	}
	d->version = nf_pick_version(&d->pin, opt->driver);
	if (d->version == NULL) {
		nf_free_all(&d->pin, &d->fams, d->cards);
		d->cards = NULL;
		return (-1);
	}
	nf_detect_needed(d->cards, d->ncards, &d->fams, d->version, &d->needed);
	/*
	 * Every image the cards need has to be in the pin table before
	 * anything in the boot directory is changed.
	 */
	if (nf_detect_missing(&d->pin, d->version, &d->needed) != 0)
		return (-1);
	if (nf_boot_load(cfg->boot, &d->boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		return (-1);
	}
	nf_detect_forced(&d->boot, &d->forced);
	nf_detect_wanted(&d->needed, &d->forced, &d->wanted);
	/*
	 * The files of somebody else are refused before anything is
	 * fetched, and the list of the files that have to be fetched is
	 * made here.
	 */
	if (nf_detect_missing_files(cfg, &d->pin, d->version, &d->wanted,
	    &d->boot, may_replace, &d->todo) != 0)
		return (-1);
	return (0);
}

/*
 * Change the boot directory: fetch the images that are not in place yet,
 * put them in place, take away the ones that are not needed and write
 * the inventory.
 *
 * The inventory is written on every path that comes after the copies
 * were put in place.  A file in the boot directory that the inventory
 * does not name is a file of somebody else, and the tool would refuse to
 * work on the directory afterwards.
 */
static int
nf_detect_execute(struct nf_detect *d, struct nf_config *cfg,
    struct nf_options *opt, struct nf_guard *guard)
{
	char tmpdir[PATH_MAX];
	size_t i;

	tmpdir[0] = '\0';
	if (d->todo.n > 0) {
		/*
		 * The dry run fetches the files into a directory of its own:
		 * it checks them and it places nothing.  The run that
		 * follows fails in the same way when a file is not the one
		 * that is pinned.
		 */
		if (cfg->dryrun) {
			if (nf_fetch_tmpdir(tmpdir, sizeof(tmpdir)) != 0) {
				nf_error("cannot make a directory for the "
				    "files: %s", strerror(errno));
				return (-1);
			}
		}
		if (nf_fetch_get(&d->fetch, cfg->mirror, d->version, &d->pin,
		    &d->todo, cfg->dryrun ? tmpdir : cfg->boot) != 0) {
			nf_fetch_rmdir(tmpdir);
			return (-1);
		}
		nf_fetch_rmdir(tmpdir);
	}
	if (cfg->dryrun) {
		/*
		 * The dry run places nothing, but it says what it would
		 * place: the files were fetched and checked.
		 */
		for (i = 0; i < d->fetch.n; i++)
			nf_strlist_add(&d->placed, d->fetch.items[i].name);
	}
	if (!cfg->dryrun) {
		/*
		 * The state of the module is read again right before the
		 * boot directory is changed: a driver that came up in the
		 * meantime may be using the files that are there.
		 */
		if (!nf_guard_still_unloaded(guard))
			return (-1);
		if (nf_images_commit(&d->boot, &d->fetch, 0, &d->placed) != 0) {
			/*
			 * The copies that were made are recorded, so that the
			 * directory stays one that the tool can work on.
			 */
			if (nf_boot_write(&d->boot, cfg->dryrun) != 0)
				nf_error("cannot write %s/%s", cfg->boot,
				    NF_SUM_FILE);
			return (-1);
		}
	}
	if (d->prune) {
		if (!nf_guard_still_unloaded(guard))
			return (-1);
		if (nf_detect_prune(cfg, &d->boot, d->version, &d->wanted, opt,
		    &d->removed) != 0) {
			if (nf_boot_write(&d->boot, cfg->dryrun) != 0)
				nf_error("cannot write %s/%s", cfg->boot,
				    NF_SUM_FILE);
			return (-1);
		}
	}
	if (nf_boot_write(&d->boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		return (-1);
	}
	return (nf_boot_cleanup(&d->boot, cfg->dryrun));
}

/*
 * The machine has no PCI bus: a jail and a build host have none.  There
 * are no adapters to be found there, so the command says why and changes
 * nothing.  The package can be installed there, and the firmware is
 * placed on the host with "nvidia-firmware detect".
 */
static void
nf_no_bus_report(const struct nf_config *cfg, int error)
{
	if (cfg->json) {
		xo_open_container_h(NULL, "detect");
		nf_json_version();
		nf_json_field("result", "%s", "no-pci-bus");
		nf_json_field("reason", "%s", strerror(error));
		xo_close_container_h(NULL, "detect");
		return;
	}
	nf_text("the machine has no PCI bus: {:reason/%s}\n", strerror(error));
	nf_text("nothing was changed; run 'nvidia-firmware detect' on the host\n");
}

/*
 * Place the firmware the adapters of the machine need.
 *
 * The two commands are the same run with one difference: the detect also
 * takes away the files that are not needed, and it may only run when the
 * nvidia module is not loaded.  The fetch only puts files in place and
 * never takes one away, so it may run while the module is loaded: a file
 * that is not there yet is not the firmware of the driver that is
 * running.
 */
static int
nf_cmd_place(struct nf_config *cfg, struct nf_options *opt, int prune)
{
	struct nf_detect d;
	struct nf_guard guard;
	int error;
	int bus;

	memset(&d, 0, sizeof(d));
	nf_fetch_init(&d.fetch);
	d.prune = prune;
	bus = nf_cards_bus_available();
	if (bus < 0) {
		nf_error("the PCI bus cannot be read: %s; run the command as root "
		    "or give the caller access to /dev/pci", strerror(errno));
		return (NF_EXIT_ERROR);
	}
	if (bus == 0) {
		/*
		 * The machine has no PCI bus.  A caller that expects an
		 * adapter is told that there is none: the command would
		 * have done nothing at all.
		 */
		if (opt->expect_card) {
			nf_error("expected an NVIDIA GPU, found none: the "
			    "machine has no PCI bus");
			return (NF_EXIT_ERROR);
		}
		nf_no_bus_report(cfg, errno);
		return (NF_EXIT_OK);
	}
	if (nf_guard_open(&guard) != 0) {
		nf_guard_failed();
		return (NF_EXIT_ERROR);
	}
	if (prune) {
		if (!nf_guard_allows(&guard)) {
			nf_guard_close(&guard);
			return (NF_EXIT_ERROR);
		}
	} else {
		if (!nf_guard_allows_place(&guard)) {
			nf_guard_close(&guard);
			return (NF_EXIT_ERROR);
		}
	}
	if (nf_detect_prepare(&d, cfg, opt, prune) != 0) {
		nf_detect_free(&d);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	error = nf_detect_execute(&d, cfg, opt, &guard) != 0;
	if (cfg->json)
		nf_detect_report_json(d.version, d.ncards, &d.placed, &d.removed,
		    &d.boot);
	else
		nf_detect_report_text(d.version, d.ncards, &d.placed, &d.removed,
		    &d.boot);
	nf_detect_free(&d);
	nf_guard_close(&guard);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
}

static int
nf_cmd_detect(struct nf_config *cfg, struct nf_options *opt)
{
	return (nf_cmd_place(cfg, opt, 1));
}

static int
nf_cmd_fetch(struct nf_config *cfg, struct nf_options *opt)
{
	return (nf_cmd_place(cfg, opt, 0));
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

static void
nf_list_cards_text(const struct nf_pin *pin, const struct nf_families *fams,
    const struct nf_card *cards, size_t ncards)
{
	size_t i, v;

	for (i = 0; i < ncards; i++) {
		nf_text("{:selector/%s}  devid={:devid/0x%04x}  "
		    "family={:family/%s}\n", cards[i].selector, cards[i].devid,
		    cards[i].has_family ? cards[i].family : "-");
		for (v = 0; v < pin->nversions; v++) {
			struct nf_strlist images;
			size_t k;

			if (nf_card_image_list(fams, pin->versions[v],
			    &cards[i], &images) != 0)
				continue;
			nf_text("  {:version/%-12s}", pin->versions[v]);
			for (k = 0; k < images.n; k++)
				nf_text("{:image/%s} ", images.items[k]);
			nf_text("\n");
			nf_strlist_free(&images);
		}
	}
}

static void
nf_list_cards_json(const struct nf_pin *pin, const struct nf_families *fams,
    const struct nf_card *cards, size_t ncards)
{
	size_t i, v;

	xo_open_container_h(NULL, "list");
	nf_json_version();
	xo_open_list_h(NULL, "card");
	for (i = 0; i < ncards; i++) {
		xo_open_instance_h(NULL, "card");
		nf_json_field("selector", "%s", cards[i].selector);
		nf_json_field("devid", "0x%04x", cards[i].devid);
		nf_json_field("family", "%s",
		    cards[i].has_family ? cards[i].family : "");
		xo_open_list_h(NULL, "image");
		for (v = 0; v < pin->nversions; v++) {
			struct nf_strlist images;
			char list[512];

			if (nf_card_image_list(fams, pin->versions[v],
			    &cards[i], &images) != 0)
				continue;
			nf_join(&images, list, sizeof(list));
			xo_open_instance_h(NULL, "image");
			nf_json_field("version", "%s", pin->versions[v]);
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
 * placed: the family table is compiled into the program.
 */
static int
nf_list_cards(struct nf_config *cfg)
{
	struct nf_pin pin;
	struct nf_families fams;
	struct nf_card *cards = NULL;
	size_t ncards = 0;

	memset(&pin, 0, sizeof(pin));
	if (nf_load_pin(cfg, &pin) != 0)
		return (NF_EXIT_ERROR);
	if (nf_load_families(cfg, &fams) != 0) {
		nf_error("cannot read the family table");
		nf_pin_free(&pin);
		return (NF_EXIT_ERROR);
	}
	if (nf_load_cards(&fams, &cards, &ncards) != 0) {
		nf_families_free(&fams);
		nf_pin_free(&pin);
		return (NF_EXIT_ERROR);
	}
	if (cfg->json)
		nf_list_cards_json(&pin, &fams, cards, ncards);
	else
		nf_list_cards_text(&pin, &fams, cards, ncards);
	free(cards);
	nf_families_free(&fams);
	nf_pin_free(&pin);
	return (NF_EXIT_OK);
}

/*
 * One row of the list of the firmware: the version and the file, with
 * the tables the row is rendered from.
 */
struct nf_image_row {
	const struct nf_config *cfg;
	const struct nf_families *fams;
	const struct nf_image *pi;
	const struct nf_card *cards;
	size_t ncards;
	const struct nf_boot *boot;
	int tell_placed;		/* 1: tell the state, 0: do not tell it */
};

static void
nf_list_image_row(const struct nf_image_row *row)
{
	struct nf_strlist families;
	struct nf_strlist users;
	char serves[512];
	char userlist[256];
	int placed;

	nf_strlist_init(&families);
	nf_strlist_init(&users);
	nf_image_families(row->fams, row->pi->version, row->pi->name, &families);
	nf_image_serves(row->fams, row->pi->version, row->pi->name, serves,
	    sizeof(serves));

	nf_cards_for_image(row->cards, row->ncards, row->fams, row->pi->version,
	    row->pi->name, &users);
	nf_join(&users, userlist, sizeof(userlist));

	/*
	 * The image is placed when the file is there and when it is a
	 * regular file: the inventory says that the file is ours, the
	 * file itself says that it is there.  A file that is not there
	 * and a symbolic link are not an installed image.
	 */
	placed = nf_boot_placed(row->boot, row->pi->name);
	if (!row->tell_placed)
		placed = -1;

	if (row->cfg->json) {
		xo_open_instance_h(NULL, "image");
		nf_json_field("version", "%s", row->pi->version);
		nf_json_field("name", "%s", row->pi->name);
		nf_json_field("size", "%lld", (long long)row->pi->size);
		nf_json_field("sha256", "%s", row->pi->sha);
		nf_json_field("families", "%s", serves);
		nf_json_field("attaches", "%s",
		    nf_image_attaches(row->fams, &families) ? "yes" : "not yet");
		if (placed >= 0)
			nf_json_field("placed", "%s", placed ? "yes" : "no");
		nf_json_field("cards", "%s", userlist);
		xo_close_instance_h(NULL, "image");
	} else {
		if (placed >= 0) {
			nf_text("{:version/%-12s}{:image/%-22s}"
			    "{:size/%12lld}  {:sha/%-12s}  {:families/%-60s}"
			    "{:placed/%-4s}  {:cards/%s}\n",
			    row->pi->version, row->pi->name,
			    (long long)row->pi->size, row->pi->sha, serves,
			    placed ? "yes" : "no", userlist);
		} else {
			nf_text("{:version/%-12s}{:image/%-22s}"
			    "{:size/%12lld}  {:sha/%-12s}  {:families/%s}\n",
			    row->pi->version, row->pi->name,
			    (long long)row->pi->size, row->pi->sha, serves);
		}
	}
	nf_strlist_free(&families);
	nf_strlist_free(&users);
}

/*
 * Every file of the pin table.  The state of the boot directory is only
 * told when the list asks for it: the table of the firmware the package
 * supports is told without it.
 */
static int
nf_list_images(struct nf_config *cfg, int supported)
{
	struct nf_pin pin;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	size_t i;

	memset(&pin, 0, sizeof(pin));
	memset(&boot, 0, sizeof(boot));
	if (nf_load_pin(cfg, &pin) != 0)
		return (NF_EXIT_ERROR);
	if (nf_load_families(cfg, &fams) != 0) {
		nf_error("cannot read the family table");
		nf_pin_free(&pin);
		return (NF_EXIT_ERROR);
	}
	if (!supported) {
		if (nf_load_cards(&fams, &cards, &ncards) != 0) {
			nf_families_free(&fams);
			nf_pin_free(&pin);
			return (NF_EXIT_ERROR);
		}
		if (nf_boot_load(cfg->boot, &boot) != 0) {
			nf_error("cannot read %s", cfg->boot);
			nf_families_free(&fams);
			nf_pin_free(&pin);
			free(cards);
			return (NF_EXIT_ERROR);
		}
	}
	if (cfg->json) {
		xo_open_container_h(NULL, "list");
		nf_json_version();
		xo_open_list_h(NULL, "image");
	}
	for (i = 0; i < pin.nimages; i++) {
		struct nf_image_row row;

		row.cfg = cfg;
		row.fams = &fams;
		row.pi = &pin.images[i];
		row.cards = cards;
		row.ncards = ncards;
		row.boot = &boot;
		row.tell_placed = supported ? 0 : 1;
		nf_list_image_row(&row);
	}
	if (cfg->json) {
		xo_close_list_h(NULL, "image");
		xo_close_container_h(NULL, "list");
	}
	nf_boot_free(&boot);
	free(cards);
	nf_families_free(&fams);
	nf_pin_free(&pin);
	return (NF_EXIT_OK);
}

static int
nf_cmd_list(struct nf_config *cfg, struct nf_options *opt)
{
	if (opt->cards)
		return (nf_list_cards(cfg));
	return (nf_list_images(cfg, opt->supported));
}

/* ---------------------------------------------------------------- status */

static void
nf_status_json(const struct nf_families *fams, const struct nf_card *cards,
    size_t ncards, struct nf_boot *boot, const char *path, int no_delete)
{
	size_t i;

	xo_open_container_h(NULL, "status");
	nf_json_version();
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
	struct nf_pin pin;
	struct nf_families fams;
	struct nf_boot boot;
	struct nf_card *cards = NULL;
	size_t ncards = 0;
	int no_delete;
	int policy;
	int error = 0;

	(void)opt;
	memset(&pin, 0, sizeof(pin));
	if (nf_load_all(cfg, &pin, &fams, &cards, &ncards, &boot) != 0)
		return (NF_EXIT_ERROR);
	if (nf_rc_no_delete(cfg->rcconf, cfg->rcdir, &policy) != 0) {
		/*
		 * The policy cannot be read: the tool cannot tell that it
		 * may take the images away, so it says that it may not.
		 */
		nf_error("cannot read the no-delete policy: %s", strerror(errno));
		policy = 1;
		error = 1;
	}
	no_delete = policy;
	if (cfg->json)
		nf_status_json(&fams, cards, ncards, &boot, cfg->boot,
		    no_delete);
	else
		nf_status_text(&fams, cards, ncards, &boot, cfg->boot,
		    no_delete);
	nf_boot_free(&boot);
	nf_free_all(&pin, &fams, cards);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
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
 * Check the images of the boot directory against the pin table.  Every
 * image that is in the directory has to be a file of the pin table, and
 * it has to have the hash that the pin table gives it.
 *
 * A symbolic link is never an installed image: the file is opened without
 * following a link and the descriptor has to be a regular file.
 */
static int
nf_verify_boot(const struct nf_config *cfg, const struct nf_pin *pin,
    const struct nf_boot *boot)
{
	char path[PATH_MAX];
	size_t i;
	int errors = 0;

	for (i = 0; i < boot->nentries; i++) {
		const struct nf_image *e = &boot->entries[i];
		const struct nf_image *pi;
		char sha[NF_SHA256_LEN];

		pi = nf_pin_image(pin, e->version, e->name);
		if (pi == NULL) {
			nf_error("%s: the driver version %s of the file is not "
			    "in the pin table", e->name, e->version);
			errors++;
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", cfg->boot, e->name);
		if (nf_sha256_regular(path, sha) != 0) {
			nf_error("%s: %s", e->name, nf_verify_reason(errno));
			errors++;
			continue;
		}
		if (strcmp(sha, pi->sha) != 0) {
			nf_error("%s: changed", e->name);
			errors++;
		}
	}
	return (errors);
}

static int
nf_cmd_verify(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_pin pin;
	struct nf_boot boot;
	size_t i;
	int errors = 0;

	(void)opt;
	memset(&pin, 0, sizeof(pin));
	if (nf_load_pin(cfg, &pin) != 0) {
		nf_error("cannot read the pin table");
		return (NF_EXIT_ERROR);
	}
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_pin_free(&pin);
		return (NF_EXIT_ERROR);
	}

	errors += nf_verify_boot(cfg, &pin, &boot);

	if (cfg->json) {
		xo_open_container_h(NULL, "verify");
		nf_json_version();
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
	nf_pin_free(&pin);
	return (errors == 0 ? NF_EXIT_OK : NF_EXIT_ERROR);
}

/* --------------------------------------------------------------- install */

/*
 * The images the install places: every image of the version, or the one
 * that is asked for.
 */
static int
nf_install_images(const struct nf_pin *pin, const char *version,
    const struct nf_options *opt, struct nf_strlist *images)
{
	size_t i;

	nf_strlist_init(images);
	if (opt->all) {
		for (i = 0; i < pin->nimages; i++) {
			if (strcmp(pin->images[i].version, version) != 0)
				continue;
			if (nf_strlist_add(images, pin->images[i].name) != 0) {
				nf_strlist_free(images);
				return (-1);
			}
		}
		return (0);
	}
	if (nf_pin_image(pin, version, opt->image) == NULL) {
		nf_error("%s is not a file of the driver version %s", opt->image,
		    version);
		return (-1);
	}
	if (nf_strlist_add(images, opt->image) != 0)
		return (-1);
	return (0);
}

/*
 * Read everything the install needs and check it.  Nothing in the boot
 * directory is changed here: every file that is fetched is checked
 * against the pin table before it is put in place.
 */
static int
nf_install_prepare(struct nf_config *cfg, struct nf_options *opt,
    struct nf_pin *pin, struct nf_boot *boot, struct nf_strlist *images,
    const char **version)
{
	if (nf_load_pin(cfg, pin) != 0)
		return (-1);
	*version = nf_pick_version(pin, opt->driver);
	if (*version == NULL) {
		nf_pin_free(pin);
		return (-1);
	}
	if (nf_install_images(pin, *version, opt, images) != 0) {
		nf_pin_free(pin);
		return (-1);
	}
	if (nf_boot_load(cfg->boot, boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_pin_free(pin);
		nf_strlist_free(images);
		return (-1);
	}
	/*
	 * A file of somebody else that has the name of an image is never
	 * overwritten.  The install checks it before it fetches anything,
	 * the same way the detect does.
	 */
	if (nf_install_foreign_check(cfg, boot, images) != 0) {
		nf_pin_free(pin);
		nf_boot_free(boot);
		nf_strlist_free(images);
		return (-1);
	}
	return (0);
}

/*
 * The files the install did not place: the ones that were asked for and
 * that are not in the boot directory.
 *
 * A dry run places nothing, so it has no file that failed: the files it
 * would place are told apart in the report, and a dry run that fails
 * says so with its exit code and its message.  A report that names every
 * file of a dry run as failed would tell a machine that every file it
 * asked for could not be installed, while the files were checked and
 * found to be the ones that are pinned.
 */
static void
nf_install_failed(const struct nf_strlist *images,
    const struct nf_strlist *placed, struct nf_strlist *failed, int dryrun)
{
	size_t i;

	nf_strlist_init(failed);
	if (dryrun)
		return;
	for (i = 0; i < images->n; i++) {
		if (nf_strlist_has(placed, images->items[i]))
			continue;
		if (nf_strlist_add(failed, images->items[i]) != 0)
			return;
	}
}

/*
 * The result of the install.  Only the files that are really in the boot
 * directory are told as installed: a run that failed says which files it
 * did not place, and a dry run says what it would place.
 */
static void
nf_install_report_json(const char *version, const struct nf_strlist *images,
    const struct nf_strlist *placed, const struct nf_strlist *failed,
    int dryrun)
{
	size_t i;

	xo_open_container_h(NULL, "install");
	nf_json_version();
	nf_json_field("dry_run", "%s", dryrun ? "yes" : "no");
	xo_open_list_h(NULL, "installed");
	for (i = 0; i < placed->n; i++) {
		xo_open_instance_h(NULL, "installed");
		nf_json_field("name", "%s", placed->items[i]);
		nf_json_field("version", "%s", version);
		nf_json_field("forced", "%s", "yes");
		xo_close_instance_h(NULL, "installed");
	}
	xo_close_list_h(NULL, "installed");
	xo_open_list_h(NULL, "would_install");
	for (i = 0; i < images->n; i++) {
		if (!dryrun)
			break;
		xo_open_instance_h(NULL, "would_install");
		nf_json_field("name", "%s", images->items[i]);
		nf_json_field("version", "%s", version);
		xo_close_instance_h(NULL, "would_install");
	}
	xo_close_list_h(NULL, "would_install");
	xo_open_list_h(NULL, "failed");
	for (i = 0; i < failed->n; i++) {
		xo_open_instance_h(NULL, "failed");
		nf_json_field("name", "%s", failed->items[i]);
		xo_close_instance_h(NULL, "failed");
	}
	xo_close_list_h(NULL, "failed");
	xo_close_container_h(NULL, "install");
}

static void
nf_install_report_text(const char *version, const struct nf_strlist *images,
    const struct nf_strlist *placed, const struct nf_strlist *failed,
    int dryrun)
{
	size_t i;

	for (i = 0; i < placed->n; i++)
		nf_text("installed: {:name/%s} ({:version/%s})\n",
		    placed->items[i], version);
	for (i = 0; i < images->n; i++) {
		if (!dryrun)
			break;
		nf_text("would install: {:name/%s} ({:version/%s})\n",
		    images->items[i], version);
	}
	for (i = 0; i < failed->n; i++)
		nf_text("not installed: {:name/%s}\n", failed->items[i]);
}

static int
nf_cmd_install(struct nf_config *cfg, struct nf_options *opt)
{
	struct nf_pin pin;
	struct nf_boot boot;
	struct nf_strlist images;
	struct nf_strlist placed;
	struct nf_strlist failed;
	struct nf_fetch fetch;
	struct nf_guard guard;
	const char *version;
	char tmpdir[PATH_MAX];
	int error = 0;

	memset(&pin, 0, sizeof(pin));
	memset(&boot, 0, sizeof(boot));
	nf_strlist_init(&images);
	nf_strlist_init(&placed);
	nf_strlist_init(&failed);
	nf_fetch_init(&fetch);
	tmpdir[0] = '\0';
	if (!opt->all && opt->image == NULL) {
		nf_error("install needs --force IMAGE or --all");
		nf_usage();
		return (NF_EXIT_USAGE);
	}
	if (!opt->all && !nf_valid_image_name(opt->image)) {
		nf_error("%s is not the name of an image", opt->image);
		return (NF_EXIT_ERROR);
	}
	if (nf_guard_open(&guard) != 0) {
		nf_guard_failed();
		return (NF_EXIT_ERROR);
	}
	if (!nf_guard_allows(&guard)) {
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The install command needs the pin table and nothing else: it
	 * does not need the family table.
	 */
	if (nf_install_prepare(cfg, opt, &pin, &boot, &images, &version) != 0) {
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The dry run fetches the files into a directory of its own and
	 * checks them there: it fails in the same way as the run that
	 * follows it, and it places nothing.
	 */
	if (cfg->dryrun) {
		if (nf_fetch_tmpdir(tmpdir, sizeof(tmpdir)) != 0) {
			nf_error("cannot make a directory for the files: %s",
			    strerror(errno));
			error = 1;
		}
	}
	if (error == 0) {
		if (nf_fetch_get(&fetch, cfg->mirror, version, &pin, &images,
		    cfg->dryrun ? tmpdir : cfg->boot) != 0)
			error = 1;
	}
	if (error == 0 && !cfg->dryrun) {
		if (!nf_guard_still_unloaded(&guard))
			error = 1;
		else if (nf_images_commit(&boot, &fetch, 1, &placed) != 0)
			error = 1;
	}
	nf_fetch_rmdir(tmpdir);
	nf_fetch_free(&fetch);
	nf_install_failed(&images, &placed, &failed, cfg->dryrun);
	if (cfg->json)
		nf_install_report_json(version, &images, &placed, &failed,
		    cfg->dryrun);
	else
		nf_install_report_text(version, &images, &placed, &failed,
		    cfg->dryrun);
	/*
	 * The inventory is written even when a copy could not be put in
	 * place: an image that is in the boot directory and that the
	 * inventory does not name is a file of somebody else, and the
	 * tool would refuse to work on the directory afterwards.
	 */
	if (nf_boot_write(&boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		error = 1;
	}

	nf_strlist_free(&failed);
	nf_strlist_free(&placed);
	nf_strlist_free(&images);
	nf_boot_free(&boot);
	nf_pin_free(&pin);
	nf_guard_close(&guard);
	return (error ? NF_EXIT_ERROR : NF_EXIT_OK);
}

/* ---------------------------------------------------------------- remove */

/*
 * The version the driver uses: the one it is pinned to, or the one the
 * image came from when nothing is pinned.
 */
static const char *
nf_remove_version(const struct nf_image *e)
{
	const char *pin;

	pin = nf_version_pin();
	if (pin != NULL && *pin != '\0')
		return (pin);
	return (e->version);
}

/*
 * The cards of the machine that need the image.  The driver may be using
 * the image of a card that is not there, so the answer does not depend on
 * the card being found.
 */
static void
nf_remove_warn_users(const struct nf_families *fams, const struct nf_card *cards,
    size_t ncards, const char *version, const char *image)
{
	struct nf_strlist users;
	size_t i;

	nf_cards_for_image(cards, ncards, fams, version, image, &users);
	for (i = 0; i < users.n; i++)
		nf_warn("%s needs %s", users.items[i], image);
	nf_strlist_free(&users);
}

/*
 * Take the image out of the boot directory.  The state of the module is
 * read again right before the file goes: a driver that came up in the
 * meantime may be using it.
 */
static int
nf_remove_image(struct nf_config *cfg, struct nf_boot *boot,
    struct nf_guard *guard, const char *image)
{
	if (!nf_guard_still_unloaded(guard))
		return (-1);
	if (nf_boot_remove(boot, image, cfg->dryrun) != 0) {
		nf_error("cannot remove %s: %s", image, strerror(errno));
		return (-1);
	}
	return (0);
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
	struct nf_image *e;
	struct nf_guard guard;
	const char *version;
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
	if (nf_guard_open(&guard) != 0) {
		nf_guard_failed();
		return (NF_EXIT_ERROR);
	}
	if (!nf_guard_allows(&guard)) {
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The image of a card that is present may be in use by the loaded
	 * driver.  The guard does not depend on the card being found: the
	 * driver may use the image of a card that is not there.
	 */
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	e = nf_boot_find(&boot, opt->arg);
	if (e == NULL) {
		nf_error("%s is not one of our images", opt->arg);
		nf_boot_free(&boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	version = nf_remove_version(e);
	if (nf_load_families(cfg, &fams) != 0) {
		nf_error("cannot read the family table");
		nf_boot_free(&boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	if (nf_load_cards(&fams, &cards, &ncards) != 0) {
		nf_families_free(&fams);
		nf_boot_free(&boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	nf_remove_warn_users(&fams, cards, ncards, version, opt->arg);
	if (nf_remove_image(cfg, &boot, &guard, opt->arg) != 0) {
		error = 1;
	} else if (nf_boot_write(&boot, cfg->dryrun) != 0) {
		nf_error("cannot write %s/%s", cfg->boot, NF_SUM_FILE);
		error = 1;
	} else {
		nf_boot_cleanup(&boot, cfg->dryrun);
	}
	/*
	 * The result is only told when the image really went away: a run
	 * that failed does not say that it removed anything.
	 */
	if (error == 0) {
		if (cfg->json) {
			xo_open_container_h(NULL, "remove");
			nf_json_version();
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
	nf_guard_close(&guard);
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
	struct nf_guard guard;
	size_t i;

	(void)opt;
	if (nf_guard_open(&guard) != 0) {
		nf_guard_failed();
		return (NF_EXIT_ERROR);
	}
	if (!nf_guard_allows(&guard)) {
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	if (nf_boot_load(cfg->boot, &boot) != 0) {
		nf_error("cannot read %s", cfg->boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The files that go away are checked before the first one of them
	 * is taken away.
	 */
	if (nf_clean_preflight(cfg, &boot) != 0) {
		nf_boot_free(&boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	/*
	 * The state of the module is read again right before the first
	 * file is taken away.
	 */
	if (!nf_guard_still_unloaded(&guard)) {
		nf_boot_free(&boot);
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	if (cfg->json) {
		xo_open_container_h(NULL, "clean");
		nf_json_version();
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
			nf_guard_close(&guard);
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
		nf_guard_close(&guard);
		return (NF_EXIT_ERROR);
	}
	nf_boot_cleanup(&boot, cfg->dryrun);
	nf_boot_free(&boot);
	nf_guard_close(&guard);
	return (NF_EXIT_OK);
}

/* ---------------------------------------------------------------- version */

/*
 * The version of the program.  It is our own version: the date of the day
 * the package was made.
 */
static int
nf_cmd_version(struct nf_config *cfg)
{
	(void)cfg;
	if (cfg->json) {
		xo_open_container_h(NULL, "version");
		nf_json_version();
		xo_close_container_h(NULL, "version");
		return (NF_EXIT_OK);
	}
	nf_text("nvidia-firmware {:version/%s}\n", nf_version.text);
	return (NF_EXIT_OK);
}

/* ------------------------------------------------------------------ main */

static void
nf_usage(void)
{
	fprintf(stderr,
	"usage: nvidia-firmware command [options]\n"
	"commands:\n"
	"  detect [-n] [--no-delete] [--keep-versions] [--expect-card] [--driver-version VERSION] [--from DIR]\n"
	"  fetch [-n] [--driver-version VERSION] [--from DIR]\n"
	"  list [--cards] [--supported]\n"
	"  status\n"
	"  verify\n"
	"  install [-n] [--force IMAGE | --all] [--driver-version VERSION] [--from DIR]\n"
	"  remove [-n] IMAGE\n"
	"  clean [-n]\n"
	"options:\n"
	"  -n                     change nothing\n"
	"  --no-delete            keep the images that are not needed\n"
	"  --keep-versions        keep the images of the older versions\n"
	"  --expect-card          fail when the machine has no NVIDIA adapter\n"
	"  --cards                list the adapters of the machine\n"
	"  --supported            list the firmware the package supports\n"
	"  --all                  every file of the driver version\n"
	"  --force IMAGE          place one file and mark it forced\n"
	"  --driver-version VERSION  the driver version to use\n"
	"  --from DIR             take the firmware from DIR, not from the network\n"
	"  --version              print the version of the program\n"
	"  --json                 write JSON instead of text\n"
	"  --libxo json           write JSON instead of text\n");
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

/*
 * Read the options of the command line.  The options are the ones of
 * the table below: the flags and the values they carry are in the
 * fields of the configuration and of the options of the run.
 *
 * The command line starts at the word behind the command, so the
 * command itself is never read as an option.
 *
 * Returns NF_CONTINUE when the command line is good and the command may
 * run.  When the tool is done, the exit code of the tool is returned:
 * the version was asked for, or the command line is not good.
 */
static int
nf_parse_options(int argc, char **argv, struct nf_options *opt,
    struct nf_config *cfg)
{
	struct nf_option options[] = {
		{ "-n", &cfg->dryrun, NULL },
		{ "--no-delete", &opt->no_delete, NULL },
		{ "--keep-versions", &opt->keep_versions, NULL },
		{ "--expect-card", &opt->expect_card, NULL },
		{ "--cards", &opt->cards, NULL },
		{ "--supported", &opt->supported, NULL },
		{ "--all", &opt->all, NULL },
		{ "--force", NULL, &opt->image },
		{ "--driver-version", NULL, &opt->driver },
		{ "--from", NULL, &opt->from },
	};
	const struct nf_option *o;
	const char *arg;
	size_t i;

	for (i = 2; i < (size_t)argc; i++) {
		arg = argv[i];
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
		if (strcmp(arg, "--version") == 0) {
			/*
			 * The version of the program is printed by the
			 * program itself: "nvidia-firmware --version".
			 * The old form of a command that took a driver
			 * version is refused here, because a run that
			 * prints the version of the program and stops is
			 * not the run that was asked for.
			 */
			nf_error("--version is not an option of a command; the "
			    "version of the program is printed by "
			    "'nvidia-firmware --version' and the driver version "
			    "is chosen with --driver-version VERSION");
			nf_usage();
			return (NF_EXIT_USAGE);
		}
		if (arg[0] == '-') {
			nf_error("unknown option: %s", arg);
			nf_usage();
			return (NF_EXIT_USAGE);
		}
		/* A word of its own, such as the image of remove. */
		opt->arg = arg;
	}
	return (NF_CONTINUE);
}

int
main(int argc, char **argv)
{
	static const struct nf_command commands[] = {
		{ "detect", nf_cmd_detect },
		{ "fetch", nf_cmd_fetch },
		{ "list", nf_cmd_list },
		{ "status", nf_cmd_status },
		{ "verify", nf_cmd_verify },
		{ "install", nf_cmd_install },
		{ "remove", nf_cmd_remove },
		{ "clean", nf_cmd_clean },
	};
	struct nf_config cfg;
	struct nf_options opt;
	const struct nf_command *cmd = NULL;
	const char *command;
	size_t i;
	int result;

	memset(&cfg, 0, sizeof(cfg));
	memset(&opt, 0, sizeof(opt));

	/*
	 * The version of the program is read with the one parser of the
	 * program: the text of the version and the three numbers of the
	 * version always come from the same place.
	 */
	snprintf(nf_version.text, sizeof(nf_version.text), "%s", NF_VERSION);
	if (nf_version_parse(NF_VERSION, &nf_version.major, &nf_version.minor,
	    &nf_version.patch) != 0) {
		fprintf(stderr, "nvidia-firmware: the version of the program "
		    "is not a date version: %s\n", NF_VERSION);
		return (NF_EXIT_ERROR);
	}

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

	result = nf_parse_options(argc, argv, &opt, &cfg);
	if (result != NF_CONTINUE) {
		xo_finish_h(NULL);
		return (result);
	}

	nf_paths(&cfg);
	cfg.mirror = opt.from;
	if (cfg.mirror == NULL) {
		const char *mirror = getenv("NVIDIA_FIRMWARE_MIRROR");

		if (mirror != NULL && *mirror != '\0')
			cfg.mirror = mirror;
	}

	if (strcmp(command, "--version") == 0) {
		result = nf_cmd_version(&cfg);
		xo_finish_h(NULL);
		return (result);
	}

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
