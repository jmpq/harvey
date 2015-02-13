/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXFORKS	20
#define NSYSFILE	3
#define	tst(a,b)	(*mode == 'r'? (b) : (a))
#define	RDR	0
#define	WTR	1

struct a_fork {
	int16_t	done;
	int16_t	fd;
	int	pid;
	int	status;
};
static struct a_fork the_fork[MAXFORKS];

FILE *
popen(int8_t *cmd, int8_t *mode)
{
	int p[2];
	int myside, hisside, pid;
	int i, ind;

	for (ind = 0; ind < MAXFORKS; ind++)
		if (the_fork[ind].pid == 0)
			break;
	if (ind == MAXFORKS)
		return NULL;
	if(pipe(p) < 0)
		return NULL;
	myside = tst(p[WTR], p[RDR]);
	hisside = tst(p[RDR], p[WTR]);
	switch (pid = fork()) {
	case -1:
		return NULL;
	case 0:
		/* myside and hisside reverse roles in child */
		close(myside);
		dup2(hisside, tst(0, 1));
		for (i=NSYSFILE; i<FOPEN_MAX; i++)
			close(i);
		execl("/bin/ape/sh", "sh", "-c", cmd, NULL);
		_exit(1);
	default:
		the_fork[ind].pid = pid;
		the_fork[ind].fd = myside;
		the_fork[ind].done = 0;
		close(hisside);
		return(fdopen(myside, mode));
	}
}

int
pclose(FILE *ptr)
{
	int f, r, ind;
	int status;

	f = fileno(ptr);
	fclose(ptr);
	for (ind = 0; ind < MAXFORKS; ind++)
		if (the_fork[ind].fd == f && the_fork[ind].pid != 0)
			break;
	if (ind == MAXFORKS)
		return 0;
	if (!the_fork[ind].done) {
		do {
			r = wait(&status);
			for (f = 0; f < MAXFORKS; f++)
				if (the_fork[f].pid == r) {
					the_fork[f].done = 1;
					the_fork[f].status = status;
					break;
				}
		} while(r != the_fork[ind].pid && r != -1);
		the_fork[ind].status = r == -1 ? -1 : status;
	}
	the_fork[ind].pid = 0;
	return (the_fork[ind].status);
}
