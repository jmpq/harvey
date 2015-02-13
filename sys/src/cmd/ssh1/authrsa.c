/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "ssh.h"

static int
authrsafn(Conn *c)
{
	uint8_t chalbuf[32+SESSIDLEN], response[MD5dlen];
	int8_t *s, *p;
	int afd, ret;
	AuthRpc *rpc;
	Msg *m;
	mpint *chal, *decr, *unpad, *mod;

	debug(DBG_AUTH, "rsa!\n");

	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0){
		debug(DBG_AUTH, "open /mnt/factotum/rpc: %r\n");
		return -1;
	}
	if((rpc = auth_allocrpc(afd)) == nil){
		debug(DBG_AUTH, "auth_allocrpc: %r\n");
		close(afd);
		return -1;
	}
	s = "proto=rsa role=client";
	if(auth_rpc(rpc, "start", s, strlen(s)) != ARok){
		debug(DBG_AUTH, "auth_rpc start %s failed: %r\n", s);
		auth_freerpc(rpc);
		close(afd);
		return -1;
	}

	ret = -1;
	debug(DBG_AUTH, "trying factotum rsa keys\n");
	while(auth_rpc(rpc, "read", nil, 0) == ARok){
		debug(DBG_AUTH, "try %s\n", (int8_t*)rpc->arg);
		mod = strtomp(rpc->arg, nil, 16, nil);
		m = allocmsg(c, SSH_CMSG_AUTH_RSA, 16+(mpsignif(mod)+7/8));
		putmpint(m, mod);
		sendmsg(m);
		mpfree(mod);
		m = recvmsg(c, -1);
		switch(m->type){
		case SSH_SMSG_FAILURE:
			debug(DBG_AUTH, "\tnot accepted %s\n",
			      (int8_t*)rpc->arg);
			free(m);
			continue;
		default:
			badmsg(m, 0);
		case SSH_SMSG_AUTH_RSA_CHALLENGE:
			break;
		}
		chal = getmpint(m);
		debug(DBG_AUTH, "\tgot challenge %B\n", chal);
		free(m);
		p = mptoa(chal, 16, nil, 0);
		mpfree(chal);
		if(p == nil){
			debug(DBG_AUTH, "\tmptoa failed: %r\n");
			unpad = mpnew(0);
			goto Keepgoing;
		}
		if(auth_rpc(rpc, "write", p, strlen(p)) != ARok){
			debug(DBG_AUTH, "\tauth_rpc write failed: %r\n");
			free(p);
			unpad = mpnew(0);	/* it will fail, we'll go round again */
			goto Keepgoing;
		}
		free(p);
		if(auth_rpc(rpc, "read", nil, 0) != ARok){
			debug(DBG_AUTH, "\tauth_rpc read failed: %r\n");
			unpad = mpnew(0);
			goto Keepgoing;
		}
		decr = strtomp(rpc->arg, nil, 16, nil);
		debug(DBG_AUTH, "\tdecrypted %B\n", decr);
		unpad = rsaunpad(decr);
		debug(DBG_AUTH, "\tunpadded %B\n", unpad);
		mpfree(decr);
	Keepgoing:
		mptoberjust(unpad, chalbuf, 32);
		mpfree(unpad);
		debug(DBG_AUTH, "\trjusted %.*H\n", 32, chalbuf);
		memmove(chalbuf+32, c->sessid, SESSIDLEN);
		debug(DBG_AUTH, "\tappend sesskey %.*H\n", 32, chalbuf);
		md5(chalbuf, 32+SESSIDLEN, response, nil);
		m = allocmsg(c, SSH_CMSG_AUTH_RSA_RESPONSE, MD5dlen);
		putbytes(m, response, MD5dlen);
		sendmsg(m);

		m = recvmsg(c, -1);
		switch(m->type){
		case SSH_SMSG_FAILURE:
			free(m);
			continue;
		default:
			badmsg(m, 0);
		case SSH_SMSG_SUCCESS:
			break;
		}
		ret = 0;
		break;
	}
	auth_freerpc(rpc);
	close(afd);
	return ret;
}

Auth authrsa =
{
	SSH_AUTH_RSA,
	"rsa",
	authrsafn,
};
