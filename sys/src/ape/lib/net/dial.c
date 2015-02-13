/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <libnet.h>

#define NAMELEN 28

static int
call(int8_t *clone, int8_t *dest, int *cfdp, int8_t *dir, int8_t *local)
{
	int fd, cfd;
	int n;
	int8_t name[3*NAMELEN+5];
	int8_t data[3*NAMELEN+10];
	int8_t *p;

	cfd = open(clone, O_RDWR);
	if(cfd < 0)
		return -1;

	/* get directory name */
	n = read(cfd, name, sizeof(name)-1);
	if(n < 0){
		close(cfd);
		return -1;
	}
	name[n] = 0;
	p = strrchr(clone, '/');
	*p = 0;
	if(dir)
		sprintf(dir, "%.*s/%.*s", 2*NAMELEN+1, clone, NAMELEN, name);
	sprintf(data, "%.*s/%.*s/data", 2*NAMELEN+1, clone, NAMELEN, name);

	/* set local side (port number, for example) if we need to */
	if(local)
		sprintf(name, "connect %.*s %.*s", 2*NAMELEN, dest, NAMELEN, local);
	else
		sprintf(name, "connect %.*s", 2*NAMELEN, dest);

	/* connect */
	if(write(cfd, name, strlen(name)) < 0){
		close(cfd);
		return -1;
	}

	/* open data connection */
	fd = open(data, O_RDWR);
	if(fd < 0){
		close(cfd);
		return -1;
	}
	if(cfdp)
		*cfdp = cfd;
	else
		close(cfd);
	return fd;
}

int
dial(int8_t *dest, int8_t *local, int8_t *dir, int *cfdp)
{
	int8_t net[128];
	int8_t netdir[128], csname[NETPATHLEN], *slp;
	int8_t clone[NAMELEN+12];
	int8_t *p;
	int n;
	int fd;
	int rv;

	/* go for a standard form net!... */
	p = strchr(dest, '!');
	if(p == 0){
		sprintf(net, "net!%.*s", sizeof(net)-5, dest);
	} else {
		strncpy(net, dest, sizeof(net)-1);
		net[sizeof(net)-1] = 0;
	}

	slp = strrchr(net, '/');
	if (slp != 0) {
		*slp++ = '\0';
		strcpy(netdir, net);
		memmove(net, slp, strlen(slp)+1);
	} else
		strcpy(netdir, "/net");
 

	/* call the connection server */
	sprintf(csname, "%s/cs", netdir);
	fd = open(csname, O_RDWR);
	if(fd < 0){
		/* no connection server, don't translate */
		p = strchr(net, '!');
		*p++ = 0;
		sprintf(clone, "%s/%s/clone", netdir, net);
		return call(clone, p, cfdp, dir, local);
	}

	/*
	 *  send dest to connection to translate
	 */
	if(write(fd, net, strlen(net)) < 0){
		close(fd);
		return -1;
	}

	/*
	 *  loop through each address from the connection server till
	 *  we get one that works.
	 */
	rv = -1;
	lseek(fd, 0, 0);
	while((n = read(fd, net, sizeof(net) - 1)) > 0){
		net[n] = 0;
		p = strchr(net, ' ');
		if(p == 0)
			continue;
		*p++ = 0;
		rv = call(net, p, cfdp, dir, local);
		if(rv >= 0)
			break;
	}
	close(fd);
	return rv;
}
