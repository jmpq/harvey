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

void
werrstr(int8_t *fmt, ...)
{
	va_list arg;
	int8_t buf[ERRMAX];

	va_start(arg, fmt);
	vseprint(buf, buf+ERRMAX, fmt, arg);
	va_end(arg);
	errstr(buf, ERRMAX);
}
