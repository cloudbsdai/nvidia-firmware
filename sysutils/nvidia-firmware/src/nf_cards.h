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

#ifndef NF_CARDS_H
#define NF_CARDS_H

#include <stddef.h>
#include <stdint.h>

#include "nf_families.h"
#include "nf_util.h"

#define	NF_NVIDIA_VENDOR	0x10de
#define	NF_CLASS_DISPLAY	0x03

struct nf_card {
	char selector[32];		/* pci0:1:0:0 */
	uint16_t vendor;
	uint16_t devid;
	uint8_t class;
	uint8_t subclass;
	char name[NF_CARD_NAME_LEN];	/* from supported-gpus.json */
	char family[NF_FAMILY_LEN];	/* empty when there is none */
	int has_family;
};

/*
 * Find the NVIDIA display adapters of the machine.  Returns the number of
 * cards found, or -1 on error.
 */
int		nf_cards_scan(struct nf_card **, size_t *);

/* Fill in the family of every card. */
void		nf_cards_families(struct nf_card *, size_t,
		    const struct nf_families *);

#endif /* NF_CARDS_H */
