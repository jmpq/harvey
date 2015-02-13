/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

/* socket extensions */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>

#include "priv.h"

static int8_t pbotch[] = "rcmd: protocol botch\n";
static int8_t lbotch[] = "rcmd: botch starting error stream\n";

static void
ding(int)
{
}

int
rcmd(int8_t **dst, int port, int8_t *luser, int8_t *ruser, int8_t *cmd,
     int *fd2p)
{
	int8_t c;
	int i, fd, lfd, fd2, port2;
	struct hostent *h;
	Rock *r;
	struct sockaddr_in in;
	int8_t buf[128];
	void	(*x)(int);

	h = gethostbyname(*dst);
	if(h == 0)
		return -1;
	*dst = h->h_name;

	/* connect using a reserved tcp port */
	fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd < 0)
		return -1;
	r = _sock_findrock(fd, 0);
	if(r == 0){
		errno = ENOTSOCK;
		return -1;
	}
	r->reserved = 1;
	in.sin_family = AF_INET;
	in.sin_port = htons(port);
	memmove(&in.sin_addr, h->h_addr_list[0], sizeof(in.sin_addr));
	if(connect(fd, &in, sizeof(in)) < 0){
		close(fd);
		return -1;
	}

	/* error stream */
	lfd = -1;
	if(fd2p){
		/* create an error stream and wait for a call in */
		for(i = 0; i < 10; i++){
			lfd = rresvport(&port2);
			if(lfd < 0)
				continue;
			if(listen(lfd, 1) == 0)
				break;
			close(lfd);
		}
		if(i >= 10){
			fprintf(stderr, pbotch);
			return -1;
		}

		snprintf(buf, sizeof buf, "%d", port2);
		if(write(fd, buf, strlen(buf)+1) < 0){
			close(fd);
			close(lfd);
			fprintf(stderr, lbotch);
			return -1;
		}
	} else {
		if(write(fd, "", 1) < 0){
			fprintf(stderr, pbotch);
			return -1;
		}
	}

	/* pass id's and command */
	if(write(fd, luser, strlen(luser)+1) < 0
	|| write(fd, ruser, strlen(ruser)+1) < 0
	|| write(fd, cmd, strlen(cmd)+1) < 0){
		fprintf(stderr, pbotch);
		return -1;
	}

	fd2 = -1;
	if(fd2p){
		x = signal(SIGALRM, ding);
		alarm(15);
		fd2 = accept(lfd, &in, &i);
		alarm(0);
		close(lfd);
		signal(SIGALRM, x);

		if(fd2 < 0){
			close(fd);
			close(lfd);
			fprintf(stderr, lbotch);
			return -1;
		}
		*fd2p = fd2;
	}

	/* get reply */
	if(read(fd, &c, 1) != 1){
		if(fd2p){
			close(fd2);
			*fd2p = -1;
		}
		fprintf(stderr, pbotch);
		return -1;
	}
	if(c == 0)
		return fd;
	i = 0;
	while(c){
		buf[i++] = c;
		if(read(fd, &c, 1) != 1)
			break;
		if(i >= sizeof(buf)-1)
			break;
	}
	buf[i] = 0;
	fprintf(stderr, "rcmd: %s\n", buf);
	close(fd);
	return -1;
}
