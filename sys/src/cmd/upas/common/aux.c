/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "common.h"

/* expand a path relative to some `.' */
extern String *
abspath(int8_t *path, int8_t *dot, String *to)
{
	if (*path == '/') {
		to = s_append(to, path);
	} else {
		to = s_append(to, dot);
		to = s_append(to, "/");
		to = s_append(to, path);
	}
	return to;
}

/* return a pointer to the base component of a pathname */
extern int8_t *
basename(int8_t *path)
{
	int8_t *cp;

	cp = strrchr(path, '/');
	return cp==0 ? path : cp+1;
}

/* append a sub-expression match onto a String */
extern void
append_match(Resub *subexp, String *sp, int se)
{
	int8_t *cp, *ep;

	cp = subexp[se].sp;
	ep = subexp[se].ep;
	for (; cp < ep; cp++)
		s_putc(sp, *cp);
	s_terminate(sp);
}

/*
 *  check for shell characters in a String
 */
static int8_t *illegalchars = "\r\n";

extern int
shellchars(int8_t *cp)
{
	int8_t *sp;

	for(sp=illegalchars; *sp; sp++)
		if(strchr(cp, *sp))
			return 1;
	return 0;
}

static int8_t *specialchars = " ()<>{};=\\'\`^&|";
static int8_t *escape = "%%";

int
hexchar(int x)
{
	x &= 0xf;
	if(x < 10)
		return '0' + x;
	else
		return 'A' + x - 10;
}

/*
 *  rewrite a string to escape shell characters
 */
extern String*
escapespecial(String *s)
{
	String *ns;
	int8_t *sp;

	for(sp = specialchars; *sp; sp++)
		if(strchr(s_to_c(s), *sp))
			break;
	if(*sp == 0)
		return s;

	ns = s_new();
	for(sp = s_to_c(s); *sp; sp++){
		if(strchr(specialchars, *sp)){
			s_append(ns, escape);
			s_putc(ns, hexchar(*sp>>4));
			s_putc(ns, hexchar(*sp));
		} else
			s_putc(ns, *sp);
	}
	s_terminate(ns);
	s_free(s);
	return ns;
}

int
hex2uint(int8_t x)
{
	if(x >= '0' && x <= '9')
		return x - '0';
	if(x >= 'A' && x <= 'F')
		return (x - 'A') + 10;
	if(x >= 'a' && x <= 'f')
		return (x - 'a') + 10;
	return -512;
}

/*
 *  rewrite a string to remove shell characters escapes
 */
extern String*
unescapespecial(String *s)
{
	int c;
	String *ns;
	int8_t *sp;
	uint n;

	if(strstr(s_to_c(s), escape) == 0)
		return s;
	n = strlen(escape);

	ns = s_new();
	for(sp = s_to_c(s); *sp; sp++){
		if(strncmp(sp, escape, n) == 0){
			c = (hex2uint(sp[n])<<4) | hex2uint(sp[n+1]);
			if(c < 0)
				s_putc(ns, *sp);
			else {
				s_putc(ns, c);
				sp += n+2-1;
			}
		} else
			s_putc(ns, *sp);
	}
	s_terminate(ns);
	s_free(s);
	return ns;

}

int
returnable(int8_t *path)
{

	return strcmp(path, "/dev/null") != 0;
}
