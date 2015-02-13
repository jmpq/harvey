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
#include <bio.h>
#include <draw.h>
#include "imagefile.h"

#include "rgbv.h"
#include "ycbcr.h"

#define	CLAMPOFF 128

static	int	clamp[CLAMPOFF+256+CLAMPOFF];
static	int	inited;

void*
_remaperror(int8_t *fmt, ...)
{
	va_list arg;
	int8_t buf[256];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);

	werrstr(buf);
	return nil;
}

Rawimage*
torgbv(Rawimage *i, int errdiff)
{
	int j, k, rgb, x, y, er, eg, eb, col, t;
	int r, g, b, r1, g1, b1;
	int *ered, *egrn, *eblu, *rp, *gp, *bp;
	int bpc;
	uint *map3;
	uint8_t *closest;
	Rawimage *im;
	int dx, dy;
	int8_t err[ERRMAX];
	uint8_t *cmap, *cm, *in, *out, *inp, *outp, cmap1[3*256], map[256], *rpic, *bpic, *gpic;

	err[0] = '\0';
	errstr(err, sizeof err);	/* throw it away */
	im = malloc(sizeof(Rawimage));
	if(im == nil)
		return nil;
	memset(im, 0, sizeof(Rawimage));
	im->chans[0] = malloc(i->chanlen);
	if(im->chans[0] == nil){
		free(im);
		return nil;
	}
	im->r = i->r;
	im->nchans = 1;
	im->chandesc = CRGBV;
	im->chanlen = i->chanlen;

	dx = i->r.max.x-i->r.min.x;
	dy = i->r.max.y-i->r.min.y;
	cmap = i->cmap;

	if(inited == 0){
		inited = 1;
		for(j=0; j<CLAMPOFF; j++)
			clamp[j] = 0;
		for(j=0; j<256; j++)
			clamp[CLAMPOFF+j] = (j>>4);
		for(j=0; j<CLAMPOFF; j++)
			clamp[CLAMPOFF+256+j] = (255>>4);
	}

	in = i->chans[0];
	inp = in;
	out = im->chans[0];
	outp = out;

	ered = malloc((dx+1)*sizeof(int));
	egrn = malloc((dx+1)*sizeof(int));
	eblu = malloc((dx+1)*sizeof(int));
	if(ered==nil || egrn==nil || eblu==nil){
		free(im->chans[0]);
		free(im);
		free(ered);
		free(egrn);
		free(eblu);
		return _remaperror("remap: malloc failed: %r");
	}
	memset(ered, 0, (dx+1)*sizeof(int));
	memset(egrn, 0, (dx+1)*sizeof(int));
	memset(eblu, 0, (dx+1)*sizeof(int));

	switch(i->chandesc){
	default:
		return _remaperror("remap: can't recognize channel type %d", i->chandesc);
	case CRGB1:
		if(cmap == nil)
			return _remaperror("remap: image has no color map");
		if(i->nchans != 1)
			return _remaperror("remap: can't handle nchans %d", i->nchans);
		for(j=1; j<=8; j++)
			if(i->cmaplen == 3*(1<<j))
				break;
		if(j > 8)
			return _remaperror("remap: can't do colormap size 3*%d", i->cmaplen/3);
		if(i->cmaplen != 3*256){
			/* to avoid a range check in inner loop below, make a full-size cmap */
			memmove(cmap1, cmap, i->cmaplen);
			cmap = cmap1;
		}
		if(errdiff == 0){
			k = 0;
			for(j=0; j<256; j++){
				r = cmap[k]>>4;
				g = cmap[k+1]>>4;
				b = cmap[k+2]>>4;
				k += 3;
				map[j] = closestrgb[b+16*(g+16*r)];
			}
			for(j=0; j<i->chanlen; j++)
				out[j] = map[in[j]];
		}else{
			/* modified floyd steinberg, coefficients (1 0) 3/16, (0, 1) 3/16, (1, 1) 7/16 */
			for(y=0; y<dy; y++){
				er = 0;
				eg = 0;
				eb = 0;
				rp = ered;
				gp = egrn;
				bp = eblu;
				for(x=0; x<dx; x++){
					cm = &cmap[3 * *inp++];
					r = cm[0] +*rp;
					g = cm[1] +*gp;
					b = cm[2] +*bp;

					/* sanity checks are new */
					if(r >= 256+CLAMPOFF)
						r = 0;
					if(g >= 256+CLAMPOFF)
						g = 0;
					if(b >= 256+CLAMPOFF)
						b = 0;
					r1 = clamp[r+CLAMPOFF];
					g1 = clamp[g+CLAMPOFF];
					b1 = clamp[b+CLAMPOFF];
					if(r1 >= 16 || g1 >= 16 || b1 >= 16)
						col = 0;
					else
						col = closestrgb[b1+16*(g1+16*r1)];
					*outp++ = col;

					rgb = rgbmap[col];
					r -= (rgb>>16) & 0xFF;
					t = (3*r)>>4;
					*rp++ = t+er;
					*rp += t;
					er = r-3*t;

					g -= (rgb>>8) & 0xFF;
					t = (3*g)>>4;
					*gp++ = t+eg;
					*gp += t;
					eg = g-3*t;

					b -= rgb & 0xFF;
					t = (3*b)>>4;
					*bp++ = t+eb;
					*bp += t;
					eb = b-3*t;
				}
			}
		}
		break;

	case CYCbCr:
		bpc = 1;
		rpic = i->chans[0];
		gpic = i->chans[1];
		bpic = i->chans[2];
		closest = closestycbcr;
		map3 = ycbcrmap;
		if(i->nchans != 3)
			return _remaperror("remap: RGB image has %d channels", i->nchans);
		goto Threecolor;

	case CRGB:
		bpc = 1;
		rpic = i->chans[0];
		gpic = i->chans[1];
		bpic = i->chans[2];
		if(i->nchans != 3)
			return _remaperror("remap: RGB image has %d channels", i->nchans);
		goto rgbgen;

	case CRGB24:
		bpc = 3;
		bpic = i->chans[0];
		gpic = i->chans[0] + 1;
		rpic = i->chans[0] + 2;
		goto rgbgen;

	case CRGBA32:
		bpc = 4;
		/* i->chans[0]+0 is alpha */
		bpic = i->chans[0] + 1;
		gpic = i->chans[0] + 2;
		rpic = i->chans[0] + 3;

	rgbgen:
		closest = closestrgb;
		map3 = rgbmap;

	Threecolor:

		if(errdiff == 0){
			outp = out;
			for(j=0; j<i->chanlen; j+=bpc){
				r = rpic[j]>>4;
				g = gpic[j]>>4;
				b = bpic[j]>>4;
				*outp++ = closest[b+16*(g+16*r)];
			}
		}else{
			/* modified floyd steinberg, coefficients (1 0) 3/16, (0, 1) 3/16, (1, 1) 7/16 */
			for(y=0; y<dy; y++){
				er = 0;
				eg = 0;
				eb = 0;
				rp = ered;
				gp = egrn;
				bp = eblu;
				for(x=0; x<dx; x++){
					r = *rpic + *rp;
					g = *gpic + *gp;
					b = *bpic + *bp;
					rpic += bpc;
					gpic += bpc;
					bpic += bpc;
					/*
					 * Errors can be uncorrectable if converting from YCbCr,
					 * since we can't guarantee that an extremal value of one of
					 * the components selects a color with an extremal value.
					 * If we don't, the errors accumulate without bound.  This
					 * doesn't happen in RGB because the closest table can guarantee
					 * a color on the edge of the gamut, producing a zero error in
					 * that component.  For the rotation YCbCr space, there may be
					 * no color that can guarantee zero error at the edge.
					 * Therefore we must clamp explicitly rather than by assuming
					 * an upper error bound of CLAMPOFF.  The performance difference
					 * is miniscule anyway.
					 */
					if(r < 0)
						r = 0;
					else if(r > 255)
						r = 255;
					if(g < 0)
						g = 0;
					else if(g > 255)
						g = 255;
					if(b < 0)
						b = 0;
					else if(b > 255)
						b = 255;
					r1 = r>>4;
					g1 = g>>4;
					b1 = b>>4;
					col = closest[b1+16*(g1+16*r1)];
					*outp++ = col;

					rgb = map3[col];
					r -= (rgb>>16) & 0xFF;
					t = (3*r)>>4;
					*rp++ = t+er;
					*rp += t;
					er = r-3*t;

					g -= (rgb>>8) & 0xFF;
					t = (3*g)>>4;
					*gp++ = t+eg;
					*gp += t;
					eg = g-3*t;

					b -= rgb & 0xFF;
					t = (3*b)>>4;
					*bp++ = t+eb;
					*bp += t;
					eb = b-3*t;
				}
			}
		}
		break;

	case CYA16:
		bpc = 2;
		/* i->chans[0] + 0 is alpha */
		rpic = i->chans[0] + 1;
		goto greygen;

	case CY:
		bpc = 1;
		rpic = i->chans[0];
		if(i->nchans != 1)
			return _remaperror("remap: Y image has %d chans", i->nchans);

	greygen:
		if(errdiff == 0){
			for(j=0; j<i->chanlen; j+=bpc){
				r = rpic[j]>>4;
				*outp++ = closestrgb[r+16*(r+16*r)];
			}
		}else{
			/* modified floyd steinberg, coefficients (1 0) 3/16, (0, 1) 3/16, (1, 1) 7/16 */
			for(y=0; y<dy; y++){
				er = 0;
				rp = ered;
				for(x=0; x<dx; x++){
					r = *rpic + *rp;
					rpic += bpc;
					r1 = clamp[r+CLAMPOFF];
					col = closestrgb[r1+16*(r1+16*r1)];
					*outp++ = col;

					rgb = rgbmap[col];
					r -= (rgb>>16) & 0xFF;
					t = (3*r)>>4;
					*rp++ = t+er;
					*rp += t;
					er = r-3*t;
				}
			}
		}
		break;
	}
	free(ered);
	free(egrn);
	free(eblu);
	return im;
}
