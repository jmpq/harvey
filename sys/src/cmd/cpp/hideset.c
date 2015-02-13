/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <u.h>
#include <libc.h>
#include <stdio.h>
#include "cpp.h"

/*
 * A hideset is a null-terminated array of Nlist pointers.
 * They are referred to by indices in the hidesets array.
 * Hideset 0 is empty.
 */

#define	HSSIZ	32
typedef	Nlist	**Hideset;
Hideset	*hidesets;
int	nhidesets = 0;
int	maxhidesets = 3;
int	inserths(Hideset, Hideset, Nlist *);

/*
 * Test for membership in a hideset
 */
int
checkhideset(int hs, Nlist *np)
{
	Hideset hsp;

	if (hs>=nhidesets)
		abort();
	for (hsp = hidesets[hs]; *hsp; hsp++) {
		if (*hsp == np)
			return 1;
	}
	return 0;
}

/*
 * Return the (possibly new) hideset obtained by adding np to hs.
 */
int
newhideset(int hs, Nlist *np)
{
	int i, len;
	Nlist *nhs[HSSIZ+3];
	Hideset hs1, hs2;

	len = inserths(nhs, hidesets[hs], np);
	for (i=0; i<nhidesets; i++) {
		for (hs1=nhs, hs2=hidesets[i]; *hs1==*hs2; hs1++, hs2++)
			if (*hs1 == NULL)
				return i;
	}
	if (len>=HSSIZ)
		return hs;
	if (nhidesets >= maxhidesets) {
		maxhidesets = 3*maxhidesets/2+1;
		hidesets = (Hideset *)realloc(hidesets, (sizeof (Hideset *))*maxhidesets);
	}
	hs1 = (Hideset)domalloc(len*sizeof(Hideset));
	memmove(hs1, nhs, len*sizeof(Hideset));
	hidesets[nhidesets] = hs1;
	return nhidesets++;
}

int
inserths(Hideset dhs, Hideset shs, Nlist *np)
{
	Hideset odhs = dhs;

	while (*shs && *shs < np)
		*dhs++ = *shs++;
	if (*shs != np)
		*dhs++ = np;
	do {
		*dhs++ = *shs;
	} while (*shs++);
	return dhs - odhs;
}

/*
 * Hideset union
 */
int
unionhideset(int hs1, int hs2)
{
	Hideset hp;

	for (hp = hidesets[hs2]; *hp; hp++)
		hs1 = newhideset(hs1, *hp);
	return hs1;
}

void
iniths(void)
{
	hidesets = (Hideset *)domalloc(maxhidesets*sizeof(Hideset *));
	hidesets[0] = (Hideset)domalloc(sizeof(Hideset));
	*hidesets[0] = NULL;
	nhidesets++;
}

void
prhideset(int hs)
{
	Hideset np;

	for (np = hidesets[hs]; *np; np++) {
		fprintf(stderr, (int8_t*)(*np)->name, (*np)->len);
		fprintf(stderr, " ", 1);
	}
}
