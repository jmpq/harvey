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
#include <venti.h>

VtConn*
vtdial(int8_t *addr)
{
	int8_t *na;
	int fd;
	VtConn *z;

	if(addr == nil)
		addr = getenv("venti");
	if(addr == nil)
		addr = "$venti";

	na = netmkaddr(addr, "tcp", "venti");
	if((fd = dial(na, nil, nil, nil)) < 0)
		return nil;

	z = vtconn(fd, fd);
	if(z)
		strecpy(z->addr, z->addr+sizeof z->addr, na);
	return z;
}
