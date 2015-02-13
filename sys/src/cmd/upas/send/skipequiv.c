/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "common.h"
#include "send.h"

#define isspace(c) ((c)==' ' || (c)=='\t' || (c)=='\n')

/*
 *  skip past all systems in equivlist
 */
extern int8_t*
skipequiv(int8_t *base)
{
	int8_t *sp;
	static Biobuf *fp;

	while(*base){
		sp = strchr(base, '!');
		if(sp==0)
			break;
		*sp = '\0';
		if(lookup(base, "equivlist", &fp, 0, 0)==1){
			/* found or us, forget this system */
			*sp='!';
			base=sp+1;
		} else {
			/* no files or system is not found, and not us */
			*sp='!';
			break;
		}
	}
	return base;
}

static int
okfile(int8_t *cp, Biobuf *fp)
{
	int8_t *buf;
	int len;
	int8_t *bp, *ep;
	int c;

	len = strlen(cp);
	Bseek(fp, 0, 0);
	
	/* one iteration per system name in the file */
	while(buf = Brdline(fp, '\n')) {
		ep = &buf[Blinelen(fp)];
		for(bp=buf; bp < ep;){
			while(isspace(*bp) || *bp==',')
				bp++;
			if(strncmp(bp, cp, len) == 0) {
				c = *(bp+len);
				if(isspace(c) || c==',')
					return 1;
			}
			while(bp < ep && (!isspace(*bp)) && *bp!=',')
				bp++;
		}
	}

	/* didn't find it, prohibit forwarding */
	return 0;
}

/* return 1 if name found in one of the files
 *	  0 if name not found in one of the files
 *	  -1 if neither file exists
 */
extern int
lookup(int8_t *cp, int8_t *local, Biobuf **lfpp, int8_t *global,
       Biobuf **gfpp)
{
	static String *file = 0;

	if (local) {
		if (file == 0)
			file = s_new();
		abspath(local, UPASLIB, s_restart(file));
		if (*lfpp != 0 || (*lfpp = sysopen(s_to_c(file), "r", 0)) != 0) {
			if (okfile(cp, *lfpp))
				return 1;
		} else
			local = 0;
	}
	if (global) {
		abspath(global, UPASLIB, s_restart(file));
		if (*gfpp != 0 || (*gfpp = sysopen(s_to_c(file), "r", 0)) != 0) {
			if (okfile(cp, *gfpp))
				return 1;
		} else
			global = 0;
	}
	return (local || global)? 0 : -1;
}
