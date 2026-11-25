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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/pciio.h>

#include "nf_cards.h"

#define	NF_PCI_MATCHES	64

/*
 * The test hook replaces the ioctl with a file of device ids.  It is only
 * honoured in a binary built for the test suite.
 */
#ifdef NVIDIA_FIRMWARE_TESTING
static const char *
nf_cards_test_file(void)
{
	const char *path;

	path = getenv("NVIDIA_FIRMWARE_TEST_PCI");
	if (path == NULL || *path == '\0')
		return (NULL);
	return (path);
}
#endif

static int
nf_cards_add(struct nf_card **cards, size_t *n, size_t *cap,
    const char *selector, uint16_t vendor, uint16_t devid, uint8_t class,
    uint8_t subclass)
{
	struct nf_card *c;

	if (*n == *cap) {
		size_t newcap = *cap ? *cap * 2 : 8;
		struct nf_card *newcards;

		newcards = realloc(*cards, newcap * sizeof(*newcards));
		if (newcards == NULL)
			return (-1);
		*cards = newcards;
		*cap = newcap;
	}
	c = &(*cards)[(*n)++];
	memset(c, 0, sizeof(*c));
	snprintf(c->selector, sizeof(c->selector), "%s", selector);
	c->vendor = vendor;
	c->devid = devid;
	c->class = class;
	c->subclass = subclass;
	return (0);
}

#ifdef NVIDIA_FIRMWARE_TESTING
/*
 * One line of the test file: a device id and, behind it, the PCI selector
 * of the card.  Returns 0 when the line holds a card and 1 when it holds
 * nothing; the selector is empty when the line does not name one.
 */
static int
nf_cards_test_line(char *line, char *selector, size_t size, uint32_t *devid)
{
	char *p = line;
	char *devid_str;
	char *sel = NULL;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '#' || *p == '\0')
		return (1);
	devid_str = p;
	while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '#')
		p++;
	if (*p != '\0') {
		*p++ = '\0';
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '\0' && *p != '#') {
			sel = p;
			while (*p != '\0' && *p != ' ' && *p != '\t')
				p++;
			*p = '\0';
		}
	}
	if (nf_parse_devid(devid_str, devid) != 0)
		return (1);
	selector[0] = '\0';
	if (sel != NULL)
		snprintf(selector, size, "%s", sel);
	return (0);
}

static int
nf_cards_scan_test(const char *path, struct nf_card **cards, size_t *n,
    size_t *cap)
{
	char line[256];
	FILE *fp;
	int index = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return (-1);
	while (nf_read_line(fp, line, sizeof(line)) == 0) {
		char selector[32];
		uint32_t devid;

		if (nf_cards_test_line(line, selector, sizeof(selector),
		    &devid) != 0)
			continue;
		if (selector[0] == '\0')
			snprintf(selector, sizeof(selector), "pci0:0:%d.0",
			    index);
		if (nf_cards_add(cards, n, cap, selector, NF_NVIDIA_VENDOR,
		    (uint16_t)devid, NF_CLASS_DISPLAY, 0) != 0) {
			fclose(fp);
			return (-1);
		}
		index++;
	}
	fclose(fp);
	return (0);
}
#endif

static int
nf_cards_scan_pci(struct nf_card **cards, size_t *n, size_t *cap)
{
	struct pci_conf_io conf;
	struct pci_match_conf pattern;
	struct pci_conf matches[NF_PCI_MATCHES];
	int fd;
	int error = 0;

	fd = open("/dev/pci", O_RDONLY);
	if (fd < 0)
		return (-1);
	memset(&pattern, 0, sizeof(pattern));
	pattern.pc_vendor = NF_NVIDIA_VENDOR;
	pattern.pc_class = NF_CLASS_DISPLAY;
	pattern.flags = PCI_GETCONF_MATCH_VENDOR | PCI_GETCONF_MATCH_CLASS;

	memset(&conf, 0, sizeof(conf));
	conf.pat_buf_len = sizeof(pattern);
	conf.num_patterns = 1;
	conf.patterns = &pattern;
	conf.match_buf_len = sizeof(matches);
	conf.num_matches = NF_PCI_MATCHES;
	conf.matches = matches;
	conf.offset = 0;
	conf.generation = 0;

	for (;;) {
		u_int32_t i;

		if (ioctl(fd, PCIOCGETCONF, &conf) < 0) {
			error = -1;
			break;
		}
		for (i = 0; i < conf.num_matches && i < NF_PCI_MATCHES; i++) {
			char selector[32];

			snprintf(selector, sizeof(selector), "pci%u:%u:%u:%u",
			    matches[i].pc_sel.pc_domain,
			    matches[i].pc_sel.pc_bus, matches[i].pc_sel.pc_dev,
			    matches[i].pc_sel.pc_func);
			if (nf_cards_add(cards, n, cap, selector,
			    matches[i].pc_vendor, matches[i].pc_device,
			    matches[i].pc_class, matches[i].pc_subclass) != 0) {
				error = -1;
				break;
			}
		}
		if (error != 0)
			break;
		if (conf.status != PCI_GETCONF_MORE_DEVS)
			break;
	}
	close(fd);
	return (error);
}

int
nf_cards_scan(struct nf_card **cards, size_t *n)
{
	size_t cap = 0;

	*cards = NULL;
	*n = 0;
#ifdef NVIDIA_FIRMWARE_TESTING
	{
		const char *path = nf_cards_test_file();

		if (path != NULL) {
			if (nf_cards_scan_test(path, cards, n, &cap) != 0)
				return (-1);
			return ((int)*n);
		}
	}
#endif
	if (nf_cards_scan_pci(cards, n, &cap) != 0) {
		free(*cards);
		*cards = NULL;
		*n = 0;
		return (-1);
	}
	return ((int)*n);
}

void
nf_cards_families(struct nf_card *cards, size_t n, const struct nf_families *fams)
{
	size_t i;

	for (i = 0; i < n; i++) {
		const char *fam;

		fam = nf_families_family(fams, cards[i].devid);
		if (fam == NULL) {
			cards[i].family[0] = '\0';
			cards[i].has_family = 0;
			continue;
		}
		snprintf(cards[i].family, sizeof(cards[i].family), "%s", fam);
		cards[i].has_family = 1;
	}
}
