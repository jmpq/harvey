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

typedef struct DS DS;

static int	call(int8_t*, int8_t*, DS*);
static int	csdial(DS*);
static void	_dial_string_parse(int8_t*, DS*);

enum
{
	Maxstring	= 128,
	Maxpath		= 256,
};

struct DS {
	/* dist string */
	int8_t	buf[Maxstring];
	int8_t	*netdir;
	int8_t	*proto;
	int8_t	*rem;

	/* other args */
	int8_t	*local;
	int8_t	*dir;
	int	*cfdp;
};


/*
 *  the dialstring is of the form '[/net/]proto!dest'
 */
int
dial(int8_t *dest, int8_t *local, int8_t *dir, int *cfdp)
{
	DS ds;
	int rv;
	int8_t err[ERRMAX], alterr[ERRMAX];

	ds.local = local;
	ds.dir = dir;
	ds.cfdp = cfdp;

	_dial_string_parse(dest, &ds);
	if(ds.netdir)
		return csdial(&ds);

	ds.netdir = "/net";
	rv = csdial(&ds);
	if(rv >= 0)
		return rv;
	err[0] = '\0';
	errstr(err, sizeof err);
	if(strstr(err, "refused") != 0){
		werrstr("%s", err);
		return rv;
	}
	ds.netdir = "/net.alt";
	rv = csdial(&ds);
	if(rv >= 0)
		return rv;

	alterr[0] = 0;
	errstr(alterr, sizeof alterr);
	if(strstr(alterr, "translate") || strstr(alterr, "does not exist"))
		werrstr("%s", err);
	else
		werrstr("%s", alterr);
	return rv;
}

static int
csdial(DS *ds)
{
	int n, fd, rv;
	int8_t *p, buf[Maxstring], clone[Maxpath], err[ERRMAX], besterr[ERRMAX];

	/*
	 *  open connection server
	 */
	snprint(buf, sizeof(buf), "%s/cs", ds->netdir);
	fd = open(buf, ORDWR);
	if(fd < 0){
		/* no connection server, don't translate */
		snprint(clone, sizeof(clone), "%s/%s/clone", ds->netdir, ds->proto);
		return call(clone, ds->rem, ds);
	}

	/*
	 *  ask connection server to translate
	 */
	snprint(buf, sizeof(buf), "%s!%s", ds->proto, ds->rem);
	if(write(fd, buf, strlen(buf)) < 0){
		close(fd);
		return -1;
	}

	/*
	 *  loop through each address from the connection server till
	 *  we get one that works.
	 */
	*besterr = 0;
	rv = -1;
	seek(fd, 0, 0);
	strcpy(err, "cs gave empty translation list");
	while((n = read(fd, buf, sizeof(buf) - 1)) > 0){
		buf[n] = 0;
		p = strchr(buf, ' ');
		if(p == 0)
			continue;
		*p++ = 0;
		rv = call(buf, p, ds);
		if(rv >= 0)
			break;
		err[0] = '\0';
		errstr(err, sizeof err);
		if(strstr(err, "does not exist") == 0)
			strcpy(besterr, err);
	}
	close(fd);

	if(rv < 0 && *besterr)
		werrstr("%s", besterr);
	else
		werrstr("%s", err);
	return rv;
}

static int
call(int8_t *clone, int8_t *dest, DS *ds)
{
	int fd, cfd, n;
	int8_t name[Maxpath], data[Maxpath], *p;

	cfd = open(clone, ORDWR);
	if(cfd < 0)
		return -1;

	/* get directory name */
	n = read(cfd, name, sizeof(name)-1);
	if(n < 0){
		close(cfd);
		return -1;
	}
	name[n] = 0;
	for(p = name; *p == ' '; p++)
		;
	snprint(name, sizeof(name), "%ld", strtoul(p, 0, 0));
	p = strrchr(clone, '/');
	*p = 0;
	if(ds->dir)
		snprint(ds->dir, NETPATHLEN, "%s/%s", clone, name);
	snprint(data, sizeof(data), "%s/%s/data", clone, name);

	/* connect */
	if(ds->local)
		snprint(name, sizeof(name), "connect %s %s", dest, ds->local);
	else
		snprint(name, sizeof(name), "connect %s", dest);
	if(write(cfd, name, strlen(name)) < 0){
		close(cfd);
		return -1;
	}

	/* open data connection */
	fd = open(data, ORDWR);
	if(fd < 0){
print("open %s: %r\n", data);
		close(cfd);
		return -1;
	}
	if(ds->cfdp)
		*ds->cfdp = cfd;
	else
		close(cfd);
	return fd;
}

/*
 *  parse a dial string
 */
static void
_dial_string_parse(int8_t *str, DS *ds)
{
	int8_t *p, *p2;

	strncpy(ds->buf, str, Maxstring);
	ds->buf[Maxstring-1] = 0;

	p = strchr(ds->buf, '!');
	if(p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->rem = ds->buf;
	} else {
		if(*ds->buf != '/' && *ds->buf != '#'){
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for(p2 = p; *p2 != '/'; p2--)
				;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->rem = p + 1;
	}
}
