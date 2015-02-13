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
#include <auth.h>
#include <fcall.h>
#include <libsec.h>
#include "9p1.h"

int8_t	*user;
int	newfd;
int	roldfd;
int	woldfd;
int	debug;
int	dofcall;
QLock	servelock;
QLock	fidlock;
QLock	taglock;
int	mainpid;
int	ntag;
int	nfork;
int8_t FLUSHED[] = "FLUSHED";

enum{
	Maxfdata = 8192
};

enum{
	Command,
	Network,
	File,
	Stdio,
};

typedef struct Tag Tag;
struct Tag
{
	int	tag;
	int	flushed;
	int	received;
	int	ref;
	Tag	*next;
};

typedef struct Message Message;
struct Message
{
	int8_t	*data;
	int	n;
};

typedef struct Fid Fid;

struct Fid
{
	int16_t	busy;
	int16_t	allocated;
	int	fid;
	Qid	qid;
	uint32_t	newoffset;
	uint32_t	oldoffset;
	Fid	*next;
};

Fid	*fids;
Tag	*tags;

int8_t	*rflush(Fcall*, Fcall*, int8_t*),
	*rversion(Fcall*, Fcall*, int8_t*),
	*rauth(Fcall*, Fcall*, int8_t*),
	*rattach(Fcall*, Fcall*, int8_t*),
	*rwalk(Fcall*, Fcall*, int8_t*),
	*ropen(Fcall*, Fcall*, int8_t*),
	*rcreate(Fcall*, Fcall*, int8_t*),
	*rread(Fcall*, Fcall*, int8_t*),
	*rwrite(Fcall*, Fcall*, int8_t*),
	*rclunk(Fcall*, Fcall*, int8_t*),
	*rremove(Fcall*, Fcall*, int8_t*),
	*rstat(Fcall*, Fcall*, int8_t*),
	*rwstat(Fcall*, Fcall*, int8_t*);

char 	*(*fcalls[])(Fcall*, Fcall*, char*) = {
	[Tversion]	rversion,
	[Tflush]	rflush,
	[Tauth]	rauth,
	[Tattach]	rattach,
	[Twalk]		rwalk,
	[Topen]		ropen,
	[Tcreate]	rcreate,
	[Tread]		rread,
	[Twrite]	rwrite,
	[Tclunk]	rclunk,
	[Tremove]	rremove,
	[Tstat]		rstat,
	[Twstat]	rwstat,
};

int8_t Etoolong[] = "name too long";

void	connect(int, int8_t*);
void	post(int, int8_t*);
void	serve(void);
void	demux(void);
void*	emalloc(uint32_t);
int8_t*	transact9p1(Fcall9p1*, Fcall9p1*, int8_t*);
Fid*	newfid(int);

struct
{
	int8_t	chal[CHALLEN];		/* my challenge */
	int8_t	rchal[CHALLEN];		/* his challenge */
	int8_t	authid[NAMEREC];
	int8_t	authdom[DOMLEN];
	int	id;
} ai;

void
usage(void)
{
	fprint(2, "usage: srvold9p [-abcCd] [-u user] [-s | [-m mountpoint]] [-x 'command' | -n network-addr | -f file] [-F] [-p servicename]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int method;
	char *oldstring;
	char *mountpoint, *postname;
	int mountflag, mountfd;
	int p[2];
int i;

	fmtinstall('F', fcallfmt);
	fmtinstall('G', fcallfmt9p1);
	fmtinstall('D', dirfmt);

	user = getuser();
	mountpoint = nil;
	mountflag = 0;
	postname = nil;
	oldstring = nil;
	method = -1;
	mountfd = -1;

	ARGBEGIN{
	case 'a':
		mountflag |= MAFTER;
		break;
	case 'b':
		mountflag |= MBEFORE;
		break;
	case 'c':
		mountflag |= MCREATE;
		break;
	case 'C':
		mountflag |= MCACHE;
		break;
	case 'd':
		debug++;
		break;
	case 'f':
		method = File;
		oldstring = ARGF();
		break;
	case 'F':
		dofcall++;
		break;
	case 'm':
		mountpoint = EARGF(usage());
		break;
	case 'n':
		method = Network;
		oldstring = ARGF();
		break;
	case 'p':
		postname = ARGF();
		if(postname == nil)
			usage();
		break;
	case 's':
		method = Stdio;
		break;
	case 'u':
		user = EARGF(usage());
		break;
	case 'x':
		method = Command;
		oldstring = ARGF();
		break;
	default:
		usage();
	}ARGEND;

	if(method == Stdio){
		if(mountpoint!=nil || argc!=0)
			usage();
	}else{
		if(oldstring == nil || argc != 0 || (mountflag!=0 && mountpoint==nil))
			usage();
	}

	rfork(RFNOTEG|RFREND);

	connect(method, oldstring);

	if(method == Stdio)
		newfd = 0;
	else{
		if(pipe(p) < 0)
			fatal("pipe: %r");
		if(postname != nil)
			post(p[0], postname);
		mountfd = p[0];
		newfd = p[1];
	}
	if(debug)
		fprint(2, "connected and posted\n");

	switch(rfork(RFPROC|RFMEM|RFNAMEG|RFFDG)){
	case 0:
		mainpid = getpid();
		/* child does all the work */
		if(mountfd >= 0)
			close(mountfd);
		switch(rfork(RFPROC|RFMEM|RFFDG)){
		case 0:
			for(i = 0; i < 20; i++)
				if (i != roldfd) close(i);
			demux();
			return;
		case -1:
			fatal("fork error: %r");
			break;
		}
		for(i = 0; i < 20; i++)
			if (i != newfd && i != woldfd && (debug == 0 || i != 2)) close(i);
		serve();
		break;
	case -1:
		fatal("fork error: %r");
		break;
	default:
		/* parent mounts if required, then exits */
		if(mountpoint){
			if(mount(mountfd, -1, mountpoint, mountflag, "") < 0)
				fatal("can't mount: %r");
		}
		break;
	}
	exits(nil);
}

void
connect(int method, int8_t *oldstring)
{
	int8_t *s;
	int8_t dir[256];

	switch(method){
	default:
		roldfd = -1;
		woldfd = -1;
		fatal("can't handle method type %d", method);
		break;
	case Network:
		s = netmkaddr(oldstring, 0, "9fs");
		roldfd = dial(s, 0, dir, 0);
		if(roldfd < 0)
			fatal("dial %s: %r", s);
		woldfd = roldfd;
		if(dofcall)
			roldfd = fcall(woldfd);
		break;
	case File:
		roldfd = open(oldstring, ORDWR);
		if(roldfd < 0)
			fatal("can't open %s: %r", oldstring);
		woldfd = roldfd;
		if(dofcall)
			roldfd = fcall(woldfd);
		break;
	case Stdio:
		roldfd = fcall(1);
		woldfd = 1;
		break;
	}
}

void
post(int fd, int8_t *srv)
{
	int f;
	int8_t buf[128];

	snprint(buf, sizeof buf, "/srv/%s", srv);
	f = create(buf, OWRITE, 0666);
	if(f < 0)
		fatal("can't create %s: %r", buf);
	sprint(buf, "%d", fd);
	if(write(f, buf, strlen(buf)) != strlen(buf))
		fatal("post write: %r");
	close(f);
}

Fid *
newfid(int fid)
{
	Fid *f, *ff;

	ff = 0;
	qlock(&fidlock);
	for(f = fids; f; f = f->next)
		if(f->fid == fid){
			f->allocated = 1;
			qunlock(&fidlock);
			return f;
		}
		else if(!ff && !f->allocated)
			ff = f;
	if(ff){
		ff->fid = fid;
		ff->allocated = 1;
		qunlock(&fidlock);
		return ff;
	}
	f = emalloc(sizeof *f);
	f->fid = fid;
	f->next = fids;
	f->allocated = 1;
	fids = f;
	qunlock(&fidlock);
	return f;
}

/*
 * Reads returning 9P1 messages and demultiplexes them.
 * BUG: assumes one read per message.
 */
void
demux(void)
{
	int m, n;
	int8_t *data;
	Fcall9p1 r;
	Message *msg;
	Tag *t;

	for(;;){
		data = malloc(IOHDRSZ+Maxfdata);	/* no need to clear memory */
		if(data == nil)
			fatal("demux malloc: %r");
		m = read(roldfd, data, IOHDRSZ+Maxfdata);
		if(m <= 0)
			fatal("read error talking to old system: %r");
		n = convM2S9p1(data, &r, m);
		if(n == 0)
			fatal("bad conversion receiving from old system");
		if(debug)
			fprint(2, "srvold9p:<=%G\n", &r);
		qlock(&taglock);
		for(t=tags; t!=nil; t=t->next)
			if(t->tag == r.tag){
				t->received = 1;
				break;
			}
		qunlock(&taglock);
		/*
		 * Fcall9p1 tag is used to rendezvous.
		 * Recipient converts message a second time, but that's OK.
		 */
		msg = emalloc(sizeof(Message));
		msg->data = data;
		msg->n = n;
		rendezvous((void*)r.tag, msg);
	}
}

Tag*
newtag(int tag)
{
	Tag *t;

	t = emalloc(sizeof(Tag));
	t->tag = tag;
	t->flushed = 0;
	t->received = 0;
	t->ref = 1;
	qlock(&taglock);
	t->next = tags;
	tags = t;
	qunlock(&taglock);
	return t;
}

void
freetag(Tag *tag)	/* called with taglock set */
{
	Tag *t, *prev;

	if(tag->ref-- == 1){
		prev = nil;
		for(t=tags; t!=nil; t=t->next){
			if(t == tag){
				if(prev == nil)
					tags = t->next;
				else
					prev->next = t->next;
				break;
			}
			prev = t;
		}
		if(t == nil)
			sysfatal("freetag");
		free(tag);
	}
}

void
serve(void)
{
	int8_t *err;
	int n;
	Fcall thdr;
	Fcall	rhdr;
	uint8_t mdata[IOHDRSZ+Maxfdata];
	int8_t mdata9p1[IOHDRSZ+Maxfdata];
	Tag *tag;

	for(;;){
		qlock(&servelock);
		for(;;){
			n = read9pmsg(newfd, mdata, sizeof mdata);

			if(n == 0)
				continue;
			if(n < 0)
				break;
			if(n > 0 && convM2S(mdata, n, &thdr) > 0)
				break;
		}
		if(n>0 && servelock.head==nil)	/* no other processes waiting to read */
			switch(rfork(RFPROC|RFMEM)){
			case 0:
				/* child starts serving */
				continue;
				break;
			case -1:
				fatal("fork error: %r");
				break;
			default:
				break;
			}
		qunlock(&servelock);

		if(n < 0)
			fatal(nil);	/* exit quietly; remote end has just hung up */

		if(debug)
			fprint(2, "srvold9p:<-%F\n", &thdr);

		tag = newtag(thdr.tag);

		if(!fcalls[thdr.type])
			err = "bad fcall type";
		else
			err = (*fcalls[thdr.type])(&thdr, &rhdr, mdata9p1);

		qlock(&taglock);
		if(tag->flushed){
			freetag(tag);
			qunlock(&taglock);
			continue;
		}
		qunlock(&taglock);

		if(err){
			rhdr.type = Rerror;
			rhdr.ename = err;
		}else{
			rhdr.type = thdr.type + 1;
			rhdr.fid = thdr.fid;
		}
		rhdr.tag = thdr.tag;
		if(debug)
			fprint(2, "srvold9p:->%F\n", &rhdr);/**/
		n = convS2M(&rhdr, mdata, sizeof mdata);
		if(n == 0)
			fatal("convS2M error on write");
		if(write(newfd, mdata, n) != n)
			fatal("mount write");

		qlock(&taglock);
		freetag(tag);
		qunlock(&taglock);
	}
}

void
send9p1(Fcall9p1 *t, int8_t *data)
{
	int m, n;

	if(debug)
		fprint(2, "srvold9p:=>%G\n", t);
	n = convS2M9p1(t, data);
	if(n == 0)
		fatal("bad conversion sending to old system");
	m = write(woldfd, data, n);
	if(m != n)
		fatal("wrote %d to old system; should be %d", m, n);
}

int
recv9p1(Fcall9p1 *r, int tag, int8_t *data)
{
	int n;
	Message *msg;

	msg = rendezvous((void*)tag, 0);
	if(msg == (void*)~0)
		fatal("rendezvous: %r");
	if(msg == nil){
		if(debug)
			fprint(2, "recv flushed\n");
		return -1;
	}
	/* copy data to local buffer */
	memmove(data, msg->data, msg->n);
	n = convM2S9p1(data, r, msg->n);
	if(n == 0)
		fatal("bad conversion receiving from old system");
	free(msg->data);
	free(msg);
	return 1;
}

int8_t*
transact9p1(Fcall9p1 *t, Fcall9p1 *r, int8_t *mdata9p1)
{
	send9p1(t, mdata9p1);
	if(recv9p1(r, t->tag, mdata9p1) < 0)
		return FLUSHED;
	if(r->type == Rerror9p1)
		return r->ename;
	if(r->type != t->type+1)
		fatal("bad message type; expected %d got %d", t->type+1, r->type);
	return nil;
}

int8_t*
rflush(Fcall *t, Fcall *, int8_t *mdata9p1)
{
	Fcall9p1 t9, r9;
	Tag *oldt;

	t9.type = Tflush9p1;
	t9.tag = t->tag;
	t9.oldtag = t->oldtag;
	qlock(&taglock);
	for(oldt=tags; oldt!=nil; oldt=oldt->next)
		if(oldt->tag == t->oldtag){
			oldt->flushed = 1;
			oldt->ref++;
			break;
		}
	qunlock(&taglock);

	if(oldt == nil){	/* nothing to flush */
		if(debug)
			fprint(2, "no such tag to flush\n");
		return 0;
	}

	transact9p1(&t9, &r9, mdata9p1);	/* can't error */

	qlock(&taglock);
	if(oldt->received == 0){	/* wake up receiver */
		if(debug)
			fprint(2, "wake up receiver\n");
		oldt->received = 1;
		rendezvous((void*)t->oldtag, 0);
	}
	freetag(oldt);
	qunlock(&taglock);
	return 0;
}

int8_t*
rversion(Fcall *t, Fcall *r, int8_t*)
{
	Fid *f;

	/* just ack; this one doesn't go to old service */
	if(t->msize > IOHDRSZ+Maxfdata)
		r->msize = IOHDRSZ+Maxfdata;
	else
		r->msize = t->msize;
	if(strncmp(t->version, "9P2000", 6) != 0)
		return "unknown 9P version";
	r->version = "9P2000";

	qlock(&fidlock);
	for(f = fids; f; f = f->next){
		f->busy = 0;
		f->allocated = 0;
	}
	qunlock(&fidlock);

	return 0;
}

int8_t*
rauth(Fcall *, Fcall *, int8_t *)
{
	return "srvold9p: authentication not supported";
}

#ifdef asdf

void
memrandom(void *p, int n)
{
	uint32_t *lp;
	uint8_t *cp;

	for(lp = p; n >= sizeof(uint32_t); n -= sizeof(uint32_t))
		*lp++ = fastrand();
	for(cp = (uint8_t*)lp; n > 0; n--)
		*cp++ = fastrand();
}

int8_t*
rsession(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	t9.type = Tsession9p1;
	t9.tag = t->tag;
	if(doauth)
		memrandom(t9.chal, sizeof t9.chal);
	else
		memset(t9.chal, 0, sizeof t9.chal);
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	qlock(&fidlock);
	for(f = fids; f; f = f->next){
		f->busy = 0;
		f->allocated = 0;
	}
	qunlock(&fidlock);

	if(doauth){
		memmove(ai.authid, r9.authid, sizeof ai.authid);
		memmove(ai.authdom, r9.authdom, sizeof ai.authid);
		memmove(ai.rchal, r9.chal, sizeof ai.rchal);
		memmove(ai.chal, t9.chal, sizeof ai.chal);
		r->authid = ai.authid;
		r->authdom = ai.authdom;
		r->chal = (uint8_t*)ai.rchal;
		r->nchal = CHALLEN;
	} else {
		r->authid = "";
		r->authdom = "";
		r->nchal = 0;
		r->chal = nil;
	}
	return 0;
}
#endif

int8_t*
rattach(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(f->busy)
		return "attach: fid in use";
	/* no authentication! */
	t9.type = Tattach9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	strncpy(t9.uname, t->uname, NAMEREC);
	if(strcmp(user, "none") == 0)
		strncpy(t9.uname, user, NAMEREC);
	strncpy(t9.aname, t->aname, NAMEREC);
	memset(t9.ticket, 0, sizeof t9.ticket);
	memset(t9.auth, 0, sizeof t9.auth);
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	r->qid.path = r9.qid.path & ~0x80000000;
	r->qid.vers = r9.qid.version;
	r->qid.type = QTDIR;
	f->busy = 1;
	f->qid = r->qid;
	return 0;
}

int8_t*
rwalk(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	int i, fid;
	Qid *q;
	Fid *f, *nf;

	f = newfid(t->fid);
	if(!f->busy)
		return "walk: bad fid";

	fid = t->fid;
	nf = nil;
	if(t->fid != t->newfid){
		nf = newfid(t->newfid);
		if(nf->busy)
			return "walk: newfid in use";
		t9.type = Tclone9p1;
		t9.tag = t->tag;
		t9.fid = t->fid;
		t9.newfid = t->newfid;
		err = transact9p1(&t9, &r9, mdata9p1);
		if(err){
			nf->busy = 0;
			nf->allocated = 0;
			return err;
		}
		fid = t->newfid;
		nf->busy = 1;
	}

	err = nil;
	r->nwqid = 0;
	for(i=0; i<t->nwname && err==nil; i++){
		if(i > MAXWELEM)
			break;
		t9.type = Twalk9p1;
		t9.tag = t->tag;
		t9.fid = fid;
		strncpy(t9.name, t->wname[i], NAMEREC);
		err = transact9p1(&t9, &r9, mdata9p1);
		if(err == FLUSHED){
			i = -1;	/* guarantee cleanup */
			break;
		}
		if(err == nil){
			q = &r->wqid[r->nwqid++];
			q->type = QTFILE;
			if(r9.qid.path & 0x80000000)
				q->type = QTDIR;
			q->vers = r9.qid.version;
			q->path = r9.qid.path & ~0x80000000;
		}
	}

	if(nf!=nil && (err!=nil || i<t->nwname)){
		/* clunk the new fid */
		t9.type = Tclunk9p1;
		t9.tag = t->tag;
		t9.fid = t->newfid;
		transact9p1(&t9, &r9, mdata9p1);
		/* ignore more errors */
		nf->busy = 0;
		nf->allocated = 0;
	}

	if(i>0 && i==t->nwname && err==nil)
		f->qid = r->wqid[r->nwqid-1];
	if(i > 0)
		return 0;
	return err;
}

int8_t*
ropen(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "open: bad fid";

	t9.type = Topen9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	t9.mode = t->mode;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	r->qid.path = r9.qid.path & ~0x80000000;
	r->qid.vers = r9.qid.version;
	r->qid.type = QTFILE;
	if(r9.qid.path & 0x80000000)
		r->qid.type = QTDIR;
	f->qid = r->qid;
	f->newoffset = 0;
	f->oldoffset = 0;
	r->iounit = 0;
	return 0;
}

int8_t*
rcreate(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "create: bad fid";

	t9.type = Tcreate9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	if(strlen(t->name)+1 >= NAMEREC)
		return "file name element too long";
	strncpy(t9.name, t->name, NAMEREC);
	t9.perm = t->perm;
	t9.mode = t->mode;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	r->qid.path = r9.qid.path & ~0x80000000;
	r->qid.vers = r9.qid.version;
	r->qid.type = QTFILE;
	if(r9.qid.path & 0x80000000)
		r->qid.type = QTDIR;
	if(r9.qid.path & 0x40000000)
		r->qid.type |= QTAPPEND;
	if(r9.qid.path & 0x20000000)
		r->qid.type |= QTEXCL;
	f->qid = r->qid;
	r->iounit = 0;
	return 0;
}

int8_t*
dirrread(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;
	int i, ndir, n, count;
	Dir d;
	uint8_t buf[Maxfdata];
	int8_t *old;

	f = newfid(t->fid);
	if(!f->busy)
		return "dirread: bad fid";

	if(f->newoffset != t->offset)
		return "seek in directory disallowed";

	t9.type = Tread9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	t9.offset = f->oldoffset;
	t9.count = t->count;	/* new directories tend to be smaller, so this may overshoot */
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	ndir = r9.count/DIRREC;
	old = r9.data;
	count = 0;
	for(i=0; i<ndir; i++){
		if(convM2D9p1(old, &d) != DIRREC)
			return "bad dir conversion in read";
		n = convD2M(&d, buf+count, sizeof buf-count);
		if(n<=BIT16SZ || count+n>t->count)
			break;
		old += DIRREC;
		f->oldoffset += DIRREC;
		f->newoffset += n;
		count += n;
	}
	memmove(r9.data, buf, count);	/* put it back in stable storage */
	r->data = r9.data;
	r->count = count;
	return 0;
}

int8_t*
rread(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "read: bad fid";

	if(f->qid.type & QTDIR)
		return dirrread(t, r, mdata9p1);

	t9.type = Tread9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	t9.offset = t->offset;
	t9.count = t->count;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	r->count = r9.count;
	r->data = r9.data;	/* points to stable storage */
	return 0;
}

int8_t*
rwrite(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "write: bad fid";

	t9.type = Twrite9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	t9.offset = t->offset;
	t9.count = t->count;
	t9.data = t->data;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	r->count = r9.count;
	return 0;
}

int8_t*
rclunk(Fcall *t, Fcall *, int8_t *mdata9p1)
{
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "clunk: bad fid";
	t9.type = Tclunk9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	transact9p1(&t9, &r9, mdata9p1);
	f->busy = 0;
	f->allocated = 0;
	/* disregard error */
	return 0;
}

int8_t*
rremove(Fcall *t, Fcall*, int8_t *mdata9p1)
{
	int8_t *err;
	Fcall9p1 t9, r9;
	Fid *f;

	f = newfid(t->fid);
	if(!f->busy)
		return "remove: bad fid";
	t9.type = Tremove9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	err = transact9p1(&t9, &r9, mdata9p1);
	f->busy = 0;
	f->allocated = 0;
	return err;
}

int8_t*
rstat(Fcall *t, Fcall *r, int8_t *mdata9p1)
{
	Fcall9p1 t9, r9;
	int8_t *err;
	Fid *f;
	Dir d;
	uint8_t buf[256];	/* big enough; there's no long names */

	f = newfid(t->fid);
	if(!f->busy)
		return "stat: bad fid";

	t9.type = Tstat9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;

	if(convM2D9p1(r9.stat, &d) != DIRREC)
		return "bad conversion in stat";
	r->stat = buf;
	r->nstat = convD2M(&d, buf, sizeof buf);
	return 0;
}

int
anydefault(Dir *d)
{
	if(d->name[0] == '\0')
		return 1;
	if(d->uid[0] == '\0')
		return 1;
	if(d->gid[0] == '\0')
		return 1;
	if(d->mode == ~0)
		return 1;
	if(d->mtime == ~0)
		return 1;
	return 0;
}

int8_t*
rwstat(Fcall *t, Fcall *, int8_t *mdata9p1)
{
	Fcall9p1 t9, r9;
	int8_t strs[DIRREC];
	int8_t *err;
	Fid *f;
	Dir d, cd;

	f = newfid(t->fid);
	if(!f->busy)
		return "wstat: bad fid";

	convM2D(t->stat, t->nstat, &d, strs);
	cd = d;
	if(anydefault(&d)){
		/* must first stat file so we can copy current values */
		t9.type = Tstat9p1;
		t9.tag = t->tag;
		t9.fid = t->fid;
		err = transact9p1(&t9, &r9, mdata9p1);
		if(err)
			return err;
		if(convM2D9p1(r9.stat, &cd) != DIRREC)
			return "bad in conversion in wstat";
	
		/* fill in default values */
		if(d.name[0] != '\0'){
			if(strlen(d.name) >= NAMEREC)
				return Etoolong;
			cd.name = d.name;
		}
		if(d.uid[0] != '\0'){
			if(strlen(d.uid) >= NAMEREC)
				return Etoolong;
			cd.uid = d.uid;
		}
		if(d.gid[0] != '\0'){
			if(strlen(d.gid) >= NAMEREC)
				return Etoolong;
			cd.gid = d.gid;
		}
		if(d.mode != ~0)
			cd.mode = d.mode;
		if(d.mtime != ~0)
			cd.mtime = d.mtime;
		if(d.length != ~0LL)
			cd.length = d.length;
	}

	if(convD2M9p1(&cd, t9.stat) != DIRREC)
		return "bad out conversion in wstat";

	t9.type = Twstat9p1;
	t9.tag = t->tag;
	t9.fid = t->fid;
	err = transact9p1(&t9, &r9, mdata9p1);
	if(err)
		return err;
	return 0;
}

void *
emalloc(uint32_t n)
{
	void *p;

	p = malloc(n);
	if(!p)
		fatal("out of memory: %r");
	memset(p, 0, n);
	return p;
}

void
fatal(int8_t *fmt, ...)
{
	int8_t buf[1024];
	va_list arg;

	if(fmt){
		va_start(arg, fmt);
		vseprint(buf, buf+sizeof(buf), fmt, arg);
		va_end(arg);
		fprint(2, "%s: (pid %d) %s\n", argv0, getpid(), buf);
	}else
		buf[0] = '\0';
	if(mainpid){
		/* two hits are sometimes needed */
		postnote(PNGROUP, mainpid, "die1 - from srvold9p");
		postnote(PNGROUP, mainpid, "die2 - from srvold9p");
	}
	exits(buf);
}
