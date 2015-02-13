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
#include "cpp.h"

Includelist	includelist[NINCLUDE];

int8_t	*objname;

void
doinclude(Tokenrow *trp)
{
	int8_t fname[256], iname[256], *p;
	Includelist *ip;
	int angled, len, fd, i;

	trp->tp += 1;
	if (trp->tp>=trp->lp)
		goto syntax;
	if (trp->tp->type!=STRING && trp->tp->type!=LT) {
		len = trp->tp - trp->bp;
		expandrow(trp, "<include>", Notinmacro);
		trp->tp = trp->bp+len;
	}
	if (trp->tp->type==STRING) {
		len = trp->tp->len-2;
		if (len > sizeof(fname) - 1)
			len = sizeof(fname) - 1;
		strncpy(fname, (int8_t*)trp->tp->t+1, len);
		angled = 0;
	} else if (trp->tp->type==LT) {
		len = 0;
		trp->tp++;
		while (trp->tp->type!=GT) {
			if (trp->tp>trp->lp || len+trp->tp->len+2 >= sizeof(fname))
				goto syntax;
			strncpy(fname+len, (int8_t*)trp->tp->t, trp->tp->len);
			len += trp->tp->len;
			trp->tp++;
		}
		angled = 1;
	} else
		goto syntax;
	trp->tp += 2;
	if (trp->tp < trp->lp || len==0)
		goto syntax;
	fname[len] = '\0';
	if (fname[0]=='/') {
		fd = open(fname, 0);
		strcpy(iname, fname);
	} else for (fd=-1,i=NINCLUDE-1; i>=0; i--) {
		ip = &includelist[i];
		if (ip->file==NULL || ip->deleted || (angled && ip->always==0))
			continue;
		if (strlen(fname)+strlen(ip->file)+2 > sizeof(iname))
			continue;
		strcpy(iname, ip->file);
		strcat(iname, "/");
		strcat(iname, fname);
		if ((fd = open(iname, 0)) >= 0)
			break;
	}
	if(fd < 0) {
		strcpy(iname, cursource->filename);
		p = strrchr(iname, '/');
		if(p != NULL) {
			*p = '\0';
			strcat(iname, "/");
			strcat(iname, fname);
			fd = open(iname, 0);
		}
	}
	if ( Mflag>1 || !angled&&Mflag==1 ) {
		write(1,objname,strlen(objname));
		write(1,iname,strlen(iname));
		write(1,"\n",1);
	}
	if (fd >= 0) {
		if (++incdepth > 20)
			error(FATAL, "#include too deeply nested");
		setsource((int8_t*)newstring((uint8_t*)iname, strlen(iname), 0),
			  fd, NULL);
		genline();
	} else {
		trp->tp = trp->bp+2;
		error(ERROR, "Could not find include file %r", trp);
	}
	return;
syntax:
	error(ERROR, "Syntax error in #include");
	return;
}

/*
 * Generate a line directive for cursource
 */
void
genline(void)
{
	static Token ta = { UNCLASS, NULL, 0, 0 };
	static Tokenrow tr = { &ta, &ta, &ta+1, 1 };
	uint8_t *p;

	if(nolineinfo)
		return;

	ta.t = p = (uint8_t*)outp;
	strcpy((int8_t*)p, "#line ");
	p += sizeof("#line ")-1;
	p = (uint8_t*)outnum((int8_t*)p, cursource->line);
	*p++ = ' '; *p++ = '"';
	if (cursource->filename[0]!='/' && wd[0]) {
		strcpy((int8_t*)p, wd);
		p += strlen(wd);
		*p++ = '/';
	}
	strcpy((int8_t*)p, cursource->filename);
	p += strlen((int8_t*)p);
	*p++ = '"'; *p++ = '\n';
	ta.len = (int8_t*)p-outp;
	outp = (int8_t*)p;
	tr.tp = tr.bp;
	puttokens(&tr);
}

void
setobjname(int8_t *f)
{
	int n = strlen(f);
	objname = (int8_t*)domalloc(n+5);
	strcpy(objname,f);
	if(objname[n-2]=='.'){
		strcpy(objname+n-1,"$O: ");
	}else{
		strcpy(objname+n,"$O: ");
	}
}
