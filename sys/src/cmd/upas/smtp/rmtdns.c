/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include	"common.h"
#include	"smtp.h"
#include	<ndb.h>

int
rmtdns(int8_t *net, int8_t *path)
{
	int fd, n, nb, r;
	int8_t *domain, *cp, buf[Maxdomain + 5];

	if(net == 0 || path == 0)
		return 0;

	domain = strdup(path);
	cp = strchr(domain, '!');
	if(cp){
		*cp = 0;
		n = cp-domain;
	} else
		n = strlen(domain);

	if(*domain == '[' && domain[n-1] == ']'){ /* accept [nnn.nnn.nnn.nnn] */
		domain[n-1] = 0;
		r = strcmp(ipattr(domain+1), "ip");
		domain[n-1] = ']';
	} else
		r = strcmp(ipattr(domain), "ip"); /* accept nnn.nnn.nnn.nnn */
	if(r == 0){
		free(domain);
		return 0;
	}

	snprint(buf, sizeof buf, "%s/dns", net);
	fd = open(buf, ORDWR);			/* look up all others */
	if(fd < 0){				/* dns screw up - can't check */
		free(domain);
		return 0;
	}

	n = snprint(buf, sizeof buf, "%s all", domain);
	free(domain);
	seek(fd, 0, 0);
	nb = write(fd, buf, n);
	close(fd);
	if(nb != n){
		rerrstr(buf, sizeof buf);
		if (strcmp(buf, "dns: name does not exist") == 0)
			return -1;
	}
	return 0;
}

/*
void
main(int, char *argv[])
{
	print("return = %d\n", rmtdns("/net.alt", argv[1]));
	exits(0);
}
*/
