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
#include <mach.h>

static int debug = 0;

#define	BITS(a, b)	((1<<(b+1))-(1<<a))

#define LSR(v, s)	((ulong)(v) >> (s))
#define ASR(v, s)	((int32_t)(v) >> (s))
#define ROR(v, s)	(LSR((v), (s)) | (((v) & ((1 << (s))-1)) << (32 - (s))))



typedef struct	Instr	Instr;
struct	Instr
{
	Map	*map;
	uint32_t	w;
	uint64_t	addr;
	uint8_t	op;			/* super opcode */

	uint8_t	cond;			/* bits 28-31 */
	uint8_t	store;			/* bit 20 */

	uint8_t	rd;			/* bits 12-15 */
	uint8_t	rn;			/* bits 16-19 */
	uint8_t	rs;			/* bits 0-11 (shifter operand) */

	int32_t	imm;			/* rotated imm */
	int8_t*	curr;			/* fill point in buffer */
	int8_t*	end;			/* end of buffer */
	int8_t*	err;			/* error message */
};

typedef struct Opcode Opcode;
struct Opcode
{
	int8_t*	o;
	void	(*fmt)(Opcode*, Instr*);
	uint64_t	(*foll)(Map*, Rgetter, Instr*, uint64_t);
	int8_t*	a;
};

static	void	format(int8_t*, Instr*, int8_t*);
static	int8_t	FRAMENAME[] = ".frame";

/*
 * Arm-specific debugger interface
 */

static	int8_t	*armexcep(Map*, Rgetter);
static	int	armfoll(Map*, uint64_t, Rgetter, uint64_t*);
static	int	arminst(Map*, uint64_t, int8_t, int8_t*, int);
static	int	armdas(Map*, uint64_t, int8_t*, int);
static	int	arminstlen(Map*, uint64_t);

/*
 *	Debugger interface
 */
Machdata armmach =
{
	{0x70, 0x00, 0x20, 0xE1},		/* break point */	/* E1200070 */
	4,			/* break point size */

	leswab,			/* short to local byte order */
	leswal,			/* long to local byte order */
	leswav,			/* long to local byte order */
	risctrace,		/* C traceback */
	riscframe,		/* Frame finder */
	armexcep,			/* print exception */
	0,			/* breakpoint fixup */
	0,			/* single precision float printer */
	0,			/* double precision float printer */
	armfoll,		/* following addresses */
	arminst,		/* print instruction */
	armdas,			/* dissembler */
	arminstlen,		/* instruction size */
};

static int8_t*
armexcep(Map *map, Rgetter rget)
{
	uint64_t c;

	c = (*rget)(map, "TYPE");
	switch ((int)c&0x1f) {
	case 0x11:
		return "Fiq interrupt";
	case 0x12:
		return "Mirq interrupt";
	case 0x13:
		return "SVC/SWI Exception";
	case 0x17:
		return "Prefetch Abort/Breakpoint";
	case 0x18:
		return "Data Abort";
	case 0x1b:
		return "Undefined instruction/Breakpoint";
	case 0x1f:
		return "Sys trap";
	default:
		return "Undefined trap";
	}
}

static
int8_t*	cond[16] =
{
	"EQ",	"NE",	"CS",	"CC",
	"MI",	"PL",	"VS",	"VC",
	"HI",	"LS",	"GE",	"LT",
	"GT",	"LE",	0,	"NV"
};

static
int8_t*	shtype[4] =
{
	"<<",	">>",	"->",	"@>"
};

static
int8_t *hb[4] =
{
	"???",	"HU", "B", "H"
};

static
int8_t*	addsub[2] =
{
	"-",	"+",
};

int
armclass(int32_t w)
{
	int op, done, cp;

	op = (w >> 25) & 0x7;
	switch(op) {
	case 0:	/* data processing r,r,r */
		if((w & 0x0ff00080) == 0x01200000) {
			op = (w >> 4) & 0x7;
			if(op == 7)
				op = 124;	/* bkpt */
			else if (op > 0 && op < 4)
				op += 124;	/* bx, blx */
			else
				op = 92;	/* unk */
			break;
		}
		op = ((w >> 4) & 0xf);
		if(op == 0x9) {
			op = 48+16;		/* mul, swp or *rex */
			if((w & 0x0ff00fff) == 0x01900f9f) {
				op = 93;	/* ldrex */
				break;
			}
			if((w & 0x0ff00ff0) == 0x01800f90) {
				op = 94;	/* strex */
				break;
			}
			if(w & (1<<24)) {
				op += 2;
				if(w & (1<<22))
					op++;	/* swpb */
				break;
			}
			if(w & (1<<23)) {	/* mullu */
				op = (48+24+4+4+2+2+4);
				if(w & (1<<22))	/* mull */
					op += 2;
			}
			if(w & (1<<21))
				op++;		/* mla */
			break;
		}
		if((op & 0x9) == 0x9)		/* ld/st byte/half s/u */
		{
			op = (48+16+4) + ((w >> 22) & 0x1) + ((w >> 19) & 0x2);
			break;
		}
		op = (w >> 21) & 0xf;
		if(w & (1<<4))
			op += 32;
		else
		if((w & (31<<7)) || (w & (1<<5)))
			op += 16;
		break;
	case 1:	/* data processing i,r,r */
		op = (48) + ((w >> 21) & 0xf);
		break;
	case 2:	/* load/store byte/word i(r) */
		if ((w & 0xffffff8f) == 0xf57ff00f) {	/* barriers, clrex */
			done = 1;
			switch ((w >> 4) & 7) {
			case 1:
				op = 95;	/* clrex */
				break;
			case 4:
				op = 96;	/* dsb */
				break;
			case 5:
				op = 97;	/* dmb */
				break;
			case 6:
				op = 98;	/* isb */
				break;
			default:
				done = 0;
				break;
			}
			if (done)
				break;
		}
		op = (48+24) + ((w >> 22) & 0x1) + ((w >> 19) & 0x2);
		break;
	case 3:	/* load/store byte/word (r)(r) */
		op = (48+24+4) + ((w >> 22) & 0x1) + ((w >> 19) & 0x2);
		break;
	case 4:	/* block data transfer (r)(r) */
		if ((w & 0xfe50ffff) == 0xf8100a00) {	/* v7 RFE */
			op = 99;
			break;
		}
		op = (48+24+4+4) + ((w >> 20) & 0x1);
		break;
	case 5:	/* branch / branch link */
		op = (48+24+4+4+2) + ((w >> 24) & 0x1);
		break;
	case 7:	/* coprocessor crap */
		cp = (w >> 8) & 0xF;
		if(cp == 10 || cp == 11){	/* vfp */
			if((w >> 4) & 0x1){
				/* vfp register transfer */
				switch((w >> 21) & 0x7){
				case 0:
					op = 118 + ((w >> 20) & 0x1);
					break;
				case 7:
					op = 118+2 + ((w >> 20) & 0x1);
					break;
				default:
					op = (48+24+4+4+2+2+4+4);
					break;
				}
				break;
			}
			/* vfp data processing */
			if(((w >> 23) & 0x1) == 0){
				op = 100 + ((w >> 19) & 0x6) + ((w >> 6) & 0x1);
				break;
			}
			switch(((w >> 19) & 0x6) + ((w >> 6) & 0x1)){
			case 0:
				op = 108;
				break;
			case 7:
				if(((w >> 19) & 0x1) == 0){
					if(((w >> 17) & 0x1) == 0)
						op = 109 + ((w >> 16) & 0x4) +
							((w >> 15) & 0x2) +
							((w >> 7) & 0x1);
					else if(((w >> 16) & 0x7) == 0x7)
						op = 117;
				}else
					switch((w >> 16) & 0x7){
					case 0:
					case 4:
					case 5:
						op = 117;
						break;
					}
				break;
			}
			if(op == 7)
				op = (48+24+4+4+2+2+4+4);
			break;
		}
		op = (48+24+4+4+2+2) + ((w >> 3) & 0x2) + ((w >> 20) & 0x1);
		break;
	case 6:	/* vfp load / store */
		if(((w >> 21) &0x9) == 0x8){
			op = 122 + ((w >> 20) & 0x1);
			break;
		}
		/* fall through */
	default:	  
		op = (48+24+4+4+2+2+4+4);
		break;
	}
	return op;
}

static int
decode(Map *map, uint64_t pc, Instr *i)
{
	uint32_t w;

	if(get4(map, pc, &w) < 0) {
		werrstr("can't read instruction: %r");
		return -1;
	}
	i->w = w;
	i->addr = pc;
	i->cond = (w >> 28) & 0xF;
	i->op = armclass(w);
	i->map = map;
	return 1;
}

#pragma	varargck	argpos	bprint		2

static void
bprint(Instr *i, int8_t *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	i->curr = vseprint(i->curr, i->end, fmt, arg);
	va_end(arg);
}

static int
plocal(Instr *i)
{
	int8_t *reg;
	Symbol s;
	int8_t *fn;
	int class;
	int offset;

	if(!findsym(i->addr, CTEXT, &s)) {
		if(debug)fprint(2,"fn not found @%llux: %r\n", i->addr);
		return 0;
	}
	fn = s.name;
	if (!findlocal(&s, FRAMENAME, &s)) {
		if(debug)fprint(2,"%s.%s not found @%s: %r\n", fn, FRAMENAME, s.name);
			return 0;
	}
	if(s.value > i->imm) {
		class = CAUTO;
		offset = s.value-i->imm;
		reg = "(SP)";
	} else {
		class = CPARAM;
		offset = i->imm-s.value-4;
		reg = "(FP)";
	}
	if(!getauto(&s, offset, class, &s)) {
		if(debug)fprint(2,"%s %s not found @%ux: %r\n", fn,
			class == CAUTO ? " auto" : "param", offset);
		return 0;
	}
	bprint(i, "%s%c%lld%s", s.name, class == CPARAM ? '+' : '-', s.value, reg);
	return 1;
}

/*
 * Print value v as name[+offset]
 */
static int
gsymoff(int8_t *buf, int n, uint32_t v, int space)
{
	Symbol s;
	int r;
	int32_t delta;

	r = delta = 0;		/* to shut compiler up */
	if (v) {
		r = findsym(v, space, &s);
		if (r)
			delta = v-s.value;
		if (delta < 0)
			delta = -delta;
	}
	if (v == 0 || r == 0 || delta >= 4096)
		return snprint(buf, n, "#%lux", v);
	if (strcmp(s.name, ".string") == 0)
		return snprint(buf, n, "#%lux", v);
	if (!delta)
		return snprint(buf, n, "%s", s.name);
	if (s.type != 't' && s.type != 'T')
		return snprint(buf, n, "%s+%llux", s.name, v-s.value);
	else
		return snprint(buf, n, "#%lux", v);
}

static void
armdps(Opcode *o, Instr *i)
{
	i->store = (i->w >> 20) & 1;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	i->rs = (i->w >> 0) & 0xf;
	if(i->rn == 15 && i->rs == 0) {
		if(i->op == 8) {
			format("MOVW", i,"CPSR, R%d");
			return;
		} else
		if(i->op == 10) {
			format("MOVW", i,"SPSR, R%d");
			return;
		}
	} else
	if(i->rn == 9 && i->rd == 15) {
		if(i->op == 9) {
			format("MOVW", i, "R%s, CPSR");
			return;
		} else
		if(i->op == 11) {
			format("MOVW", i, "R%s, SPSR");
			return;
		}
	}
	if(i->rd == 15) {
		if(i->op == 120) {
			format("MOVW", i, "PSR, %x");
			return;
		} else
		if(i->op == 121) {
			format("MOVW", i, "%x, PSR");
			return;
		}
	}
	format(o->o, i, o->a);
}

static void
armdpi(Opcode *o, Instr *i)
{
	uint32_t v;
	int c;

	v = (i->w >> 0) & 0xff;
	c = (i->w >> 8) & 0xf;
	while(c) {
		v = (v<<30) | (v>>2);
		c--;
	}
	i->imm = v;
	i->store = (i->w >> 20) & 1;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	i->rs = i->w&0x0f;

		/* RET is encoded as ADD #0,R14,R15 */
	if((i->w & 0x0fffffff) == 0x028ef000){
		format("RET%C", i, "");
		return;
	}
	if((i->w & 0x0ff0ffff) == 0x0280f000){
		format("B%C", i, "0(R%n)");
		return;
	}
	format(o->o, i, o->a);
}

static void
armsdti(Opcode *o, Instr *i)
{
	uint32_t v;

	v = i->w & 0xfff;
	if(!(i->w & (1<<23)))
		v = -v;
	i->store = ((i->w >> 23) & 0x2) | ((i->w >>21) & 0x1);
	i->imm = v;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
		/* RET is encoded as LW.P x,R13,R15 */
	if ((i->w & 0x0ffff000) == 0x049df000)
	{
		format("RET%C%p", i, "%I");
		return;
	}
	format(o->o, i, o->a);
}

static void
armvstdi(Opcode *o, Instr *i)
{
	uint32_t v;

	v = (i->w & 0xff) << 2;
	if(!(i->w & (1<<23)))
		v = -v;
	i->imm = v;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	format(o->o, i, o->a);
}

/* arm V4 ld/st halfword, signed byte */
static void
armhwby(Opcode *o, Instr *i)
{
	i->store = ((i->w >> 23) & 0x2) | ((i->w >>21) & 0x1);
	i->imm = (i->w & 0xf) | ((i->w >> 8) & 0xf);
	if (!(i->w & (1 << 23)))
		i->imm = - i->imm;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	i->rs = (i->w >> 0) & 0xf;
	format(o->o, i, o->a);
}

static void
armsdts(Opcode *o, Instr *i)
{
	i->store = ((i->w >> 23) & 0x2) | ((i->w >>21) & 0x1);
	i->rs = (i->w >> 0) & 0xf;
	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	format(o->o, i, o->a);
}

static void
armbdt(Opcode *o, Instr *i)
{
	i->store = (i->w >> 21) & 0x3;		/* S & W bits */
	i->rn = (i->w >> 16) & 0xf;
	i->imm = i->w & 0xffff;
	if(i->w == 0xe8fd8000)
		format("RFE", i, "");
	else
		format(o->o, i, o->a);
}

static void
armund(Opcode *o, Instr *i)
{
	format(o->o, i, o->a);
}

static void
armcdt(Opcode *o, Instr *i)
{
	format(o->o, i, o->a);
}

static void
armunk(Opcode *o, Instr *i)
{
	format(o->o, i, o->a);
}

static void
armb(Opcode *o, Instr *i)
{
	uint32_t v;

	v = i->w & 0xffffff;
	if(v & 0x800000)
		v |= ~0xffffff;
	i->imm = (v<<2) + i->addr + 8;
	format(o->o, i, o->a);
}

static void
armbpt(Opcode *o, Instr *i)
{
	i->imm = ((i->w >> 4) & 0xfff0) | (i->w &0xf);
	format(o->o, i, o->a);
}

static void
armco(Opcode *o, Instr *i)		/* coprocessor instructions */
{
	int op, p, cp;

	int8_t buf[1024];

	i->rn = (i->w >> 16) & 0xf;
	i->rd = (i->w >> 12) & 0xf;
	i->rs = i->w&0xf;
	cp = (i->w >> 8) & 0xf;
	p = (i->w >> 5) & 0x7;
	if(i->w&(1<<4)) {
		op = (i->w >> 21) & 0x07;
		snprint(buf, sizeof(buf), "#%x, #%x, R%d, C(%d), C(%d), #%x", cp, op, i->rd, i->rn, i->rs, p);
	} else {
		op = (i->w >> 20) & 0x0f;
		snprint(buf, sizeof(buf), "#%x, #%x, C(%d), C(%d), C(%d), #%x", cp, op, i->rd, i->rn, i->rs, p);
	}
	format(o->o, i, buf);
}

static int
armcondpass(Map *map, Rgetter rget, uint8_t cond)
{
	uint64_t psr;
	uint8_t n;
	uint8_t z;
	uint8_t c;
	uint8_t v;

	psr = rget(map, "PSR");
	n = (psr >> 31) & 1;
	z = (psr >> 30) & 1;
	c = (psr >> 29) & 1;
	v = (psr >> 28) & 1;

	switch(cond) {
	default:
	case 0:		return z;
	case 1:		return !z;
	case 2:		return c;
	case 3:		return !c;
	case 4:		return n;
	case 5:		return !n;
	case 6:		return v;
	case 7:		return !v;
	case 8:		return c && !z;
	case 9:		return !c || z;
	case 10:	return n == v;
	case 11:	return n != v;
	case 12:	return !z && (n == v);
	case 13:	return z || (n != v);
	case 14:	return 1;
	case 15:	return 0;
	}
}

static uint32_t
armshiftval(Map *map, Rgetter rget, Instr *i)
{
	if(i->w & (1 << 25)) {				/* immediate */
		uint32_t imm = i->w & BITS(0, 7);
		uint32_t s = (i->w & BITS(8, 11)) >> 7; /* this contains the *2 */
		return ROR(imm, s);
	} else {
		int8_t buf[8];
		uint32_t v;
		uint32_t s = (i->w & BITS(7,11)) >> 7;

		sprint(buf, "R%ld", i->w & 0xf);
		v = rget(map, buf);

		switch((i->w & BITS(4, 6)) >> 4) {
		default:
		case 0:					/* LSLIMM */
			return v << s;
		case 1:					/* LSLREG */
			sprint(buf, "R%lud", s >> 1);
			s = rget(map, buf) & 0xFF;
			if(s >= 32) return 0;
			return v << s;
		case 2:					/* LSRIMM */
			return LSR(v, s);
		case 3:					/* LSRREG */
			sprint(buf, "R%ld", s >> 1);
			s = rget(map, buf) & 0xFF;
			if(s >= 32) return 0;
			return LSR(v, s);
		case 4:					/* ASRIMM */
			if(s == 0) {
				if((v & (1U<<31)) == 0)
					return 0;
				return 0xFFFFFFFF;
			}
			return ASR(v, s);
		case 5:					/* ASRREG */
			sprint(buf, "R%ld", s >> 1);
			s = rget(map, buf) & 0xFF;
			if(s >= 32) {
				if((v & (1U<<31)) == 0)
					return 0;
				return 0xFFFFFFFF;
			}
			return ASR(v, s);
		case 6:					/* RORIMM */
			if(s == 0) {
				uint32_t c = (rget(map, "PSR") >> 29) & 1;

				return (c << 31) | LSR(v, 1);
			}
			return ROR(v, s);
		case 7:					/* RORREG */
			sprint(buf, "R%ld", (s>>1)&0xF);
			s = rget(map, buf);
			if(s == 0 || (s & 0xF) == 0)
				return v;
			return ROR(v, s & 0xF);
		}
	}
}

static int
nbits(uint32_t v)
{
	int n = 0;
	int i;

	for(i=0; i < 32 ; i++) {
		if(v & 1) ++n;
		v >>= 1;
	}

	return n;
}

static uint32_t
armmaddr(Map *map, Rgetter rget, Instr *i)
{
	uint32_t v;
	uint32_t nb;
	int8_t buf[8];
	uint32_t rn;

	rn = (i->w >> 16) & 0xf;
	sprint(buf,"R%ld", rn);

	v = rget(map, buf);
	nb = nbits(i->w & ((1 << 15) - 1));

	switch((i->w >> 23) & 3) {
	default:
	case 0: return (v - (nb*4)) + 4;
	case 1: return v;
	case 2: return v - (nb*4);
	case 3: return v + 4;
	}
}

static uint64_t
armaddr(Map *map, Rgetter rget, Instr *i)
{
	int8_t buf[8];
	uint32_t rn;

	snprint(buf, sizeof(buf), "R%ld", (i->w >> 16) & 0xf);
	rn = rget(map, buf);

	if((i->w & (1<<24)) == 0)			/* POSTIDX */
		return rn;

	if((i->w & (1<<25)) == 0) {			/* OFFSET */
		if(i->w & (1U<<23))
			return rn + (i->w & BITS(0,11));
		return rn - (i->w & BITS(0,11));
	} else {					/* REGOFF */
		uint32_t index = 0;
		uint8_t c;
		uint8_t rm;

		sprint(buf, "R%ld", i->w & 0xf);
		rm = rget(map, buf);

		switch((i->w & BITS(5,6)) >> 5) {
		case 0: index = rm << ((i->w & BITS(7,11)) >> 7);	break;
		case 1: index = LSR(rm, ((i->w & BITS(7,11)) >> 7));	break;
		case 2: index = ASR(rm, ((i->w & BITS(7,11)) >> 7));	break;
		case 3:
			if((i->w & BITS(7,11)) == 0) {
				c = (rget(map, "PSR") >> 29) & 1;
				index = c << 31 | LSR(rm, 1);
			} else {
				index = ROR(rm, ((i->w & BITS(7,11)) >> 7));
			}
			break;
		}
		if(i->w & (1<<23))
			return rn + index;
		return rn - index;
	}
}

static uint64_t
armfadd(Map *map, Rgetter rget, Instr *i, uint64_t pc)
{
	int8_t buf[8];
	int r;

	r = (i->w >> 12) & 0xf;
	if(r != 15 || !armcondpass(map, rget, (i->w >> 28) & 0xf))
		return pc+4;

	r = (i->w >> 16) & 0xf;
	sprint(buf, "R%d", r);

	return rget(map, buf) + armshiftval(map, rget, i);
}

static uint64_t
armfbx(Map *map, Rgetter rget, Instr *i, uint64_t pc)
{
	int8_t buf[8];
	int r;

	if(!armcondpass(map, rget, (i->w>>28)&0xf))
		return pc+4;
	r = (i->w >> 0) & 0xf;
	sprint(buf, "R%d", r);
	return rget(map, buf);
}

static uint64_t
armfmovm(Map *map, Rgetter rget, Instr *i, uint64_t pc)
{
	uint32_t v;
	uint32_t addr;

	v = i->w & 1<<15;
	if(!v || !armcondpass(map, rget, (i->w>>28)&0xf))
		return pc+4;

	addr = armmaddr(map, rget, i) + nbits(i->w & BITS(0,15));
	if(get4(map, addr, &v) < 0) {
		werrstr("can't read addr: %r");
		return -1;
	}
	return v;
}

static uint64_t
armfbranch(Map *map, Rgetter rget, Instr *i, uint64_t pc)
{
	if(!armcondpass(map, rget, (i->w >> 28) & 0xf))
		return pc+4;

	return pc + (((signed long)i->w << 8) >> 6) + 8;
}

static uint64_t
armfmov(Map *map, Rgetter rget, Instr *i, uint64_t pc)
{
	uint32_t rd, v;

	rd = (i->w >> 12) & 0xf;
	if(rd != 15 || !armcondpass(map, rget, (i->w>>28)&0xf))
		return pc+4;

	 /* LDR */
	/* BUG: Needs LDH/B, too */
	if(((i->w>>26)&0x3) == 1) {
		if(get4(map, armaddr(map, rget, i), &v) < 0) {
			werrstr("can't read instruction: %r");
			return pc+4;
		}
		return v;
	}

	 /* MOV */
	v = armshiftval(map, rget, i);

	return v;
}

static Opcode opcodes[] =
{
	"AND%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"EOR%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"SUB%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"RSB%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"ADD%C%S",	armdps, armfadd,	"R%s,R%n,R%d",
	"ADC%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"SBC%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"RSC%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"TST%C%S",	armdps, 0,	"R%s,R%n",
	"TEQ%C%S",	armdps, 0,	"R%s,R%n",
	"CMP%C%S",	armdps, 0,	"R%s,R%n",
	"CMN%C%S",	armdps, 0,	"R%s,R%n",
	"ORR%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"MOVW%C%S",	armdps, armfmov,	"R%s,R%d",
	"BIC%C%S",	armdps, 0,	"R%s,R%n,R%d",
	"MVN%C%S",	armdps, 0,	"R%s,R%d",

/* 16 */
	"AND%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"EOR%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"SUB%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"RSB%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"ADD%C%S",	armdps, armfadd,	"(R%s%h%m),R%n,R%d",
	"ADC%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"SBC%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"RSC%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"TST%C%S",	armdps, 0,	"(R%s%h%m),R%n",
	"TEQ%C%S",	armdps, 0,	"(R%s%h%m),R%n",
	"CMP%C%S",	armdps, 0,	"(R%s%h%m),R%n",
	"CMN%C%S",	armdps, 0,	"(R%s%h%m),R%n",
	"ORR%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"MOVW%C%S",	armdps, armfmov,	"(R%s%h%m),R%d",
	"BIC%C%S",	armdps, 0,	"(R%s%h%m),R%n,R%d",
	"MVN%C%S",	armdps, 0,	"(R%s%h%m),R%d",

/* 32 */
	"AND%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"EOR%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"SUB%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"RSB%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"ADD%C%S",	armdps, armfadd,	"(R%s%hR%M),R%n,R%d",
	"ADC%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"SBC%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"RSC%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"TST%C%S",	armdps, 0,	"(R%s%hR%M),R%n",
	"TEQ%C%S",	armdps, 0,	"(R%s%hR%M),R%n",
	"CMP%C%S",	armdps, 0,	"(R%s%hR%M),R%n",
	"CMN%C%S",	armdps, 0,	"(R%s%hR%M),R%n",
	"ORR%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"MOVW%C%S",	armdps, armfmov,	"(R%s%hR%M),R%d",
	"BIC%C%S",	armdps, 0,	"(R%s%hR%M),R%n,R%d",
	"MVN%C%S",	armdps, 0,	"(R%s%hR%M),R%d",

/* 48 */
	"AND%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"EOR%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"SUB%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"RSB%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"ADD%C%S",	armdpi, armfadd,	"$#%i,R%n,R%d",
	"ADC%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"SBC%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"RSC%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"TST%C%S",	armdpi, 0,	"$#%i,R%n",
	"TEQ%C%S",	armdpi, 0,	"$#%i,R%n",
	"CMP%C%S",	armdpi, 0,	"$#%i,R%n",
	"CMN%C%S",	armdpi, 0,	"$#%i,R%n",
	"ORR%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"MOVW%C%S",	armdpi, armfmov,	"$#%i,R%d",
	"BIC%C%S",	armdpi, 0,	"$#%i,R%n,R%d",
	"MVN%C%S",	armdpi, 0,	"$#%i,R%d",

/* 48+16 */
	"MUL%C%S",	armdpi, 0,	"R%M,R%s,R%n",
	"MULA%C%S",	armdpi, 0,	"R%M,R%s,R%n,R%d",
	"SWPW",		armdpi, 0,	"R%s,(R%n),R%d",
	"SWPB",		armdpi, 0,	"R%s,(R%n),R%d",

/* 48+16+4 */
	"MOV%u%C%p",	armhwby, 0,	"R%d,(R%n%UR%M)",
	"MOV%u%C%p",	armhwby, 0,	"R%d,%I",
	"MOV%u%C%p",	armhwby, armfmov,	"(R%n%UR%M),R%d",
	"MOV%u%C%p",	armhwby, armfmov,	"%I,R%d",

/* 48+24 */
	"MOVW%C%p",	armsdti, 0,	"R%d,%I",
	"MOVB%C%p",	armsdti, 0,	"R%d,%I",
	"MOVW%C%p",	armsdti, armfmov,	"%I,R%d",
	"MOVBU%C%p",	armsdti, armfmov,	"%I,R%d",

	"MOVW%C%p",	armsdts, 0,	"R%d,(R%s%h%m)(R%n)",
	"MOVB%C%p",	armsdts, 0,	"R%d,(R%s%h%m)(R%n)",
	"MOVW%C%p",	armsdts, armfmov,	"(R%s%h%m)(R%n),R%d",
	"MOVBU%C%p",	armsdts, armfmov,	"(R%s%h%m)(R%n),R%d",

	"MOVM%C%P%a",	armbdt, armfmovm,		"[%r],(R%n)",
	"MOVM%C%P%a",	armbdt, armfmovm,		"(R%n),[%r]",

	"B%C",		armb, armfbranch,		"%b",
	"BL%C",		armb, armfbranch,		"%b",

	"CDP%C",	armco, 0,		"",
	"CDP%C",	armco, 0,		"",
	"MCR%C",	armco, 0,		"",
	"MRC%C",	armco, 0,		"",

/* 48+24+4+4+2+2+4 */
	"MULLU%C%S",	armdpi, 0,	"R%M,R%s,(R%n,R%d)",
	"MULALU%C%S",	armdpi, 0,	"R%M,R%s,(R%n,R%d)",
	"MULL%C%S",	armdpi, 0,	"R%M,R%s,(R%n,R%d)",
	"MULAL%C%S",	armdpi, 0,	"R%M,R%s,(R%n,R%d)",

/* 48+24+4+4+2+2+4+4 = 92 */
	"UNK",		armunk, 0,	"",

	/* new v7 arch instructions */
/* 93 */
	"LDREX",	armdpi, 0,	"(R%n),R%d",
	"STREX",	armdpi, 0,	"R%s,(R%n),R%d",
	"CLREX",	armunk, 0,	"",

/* 96 */
	"DSB",		armunk, 0,	"",
	"DMB",		armunk, 0,	"",
	"ISB",		armunk, 0,	"",

/* 99 */
	"RFEV7%P%a",	armbdt, 0,	"(R%n)",

/* 100 */
	"MLA%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"MLS%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"NMLS%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"NMLA%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"MUL%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"NMUL%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"ADD%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"SUB%f%C",	armdps,	0,	"F%s,F%n,F%d",
	"DIV%f%C",	armdps,	0,	"F%s,F%n,F%d",

/* 109 */
	"MOV%f%C",	armdps,	0,	"F%s,F%d",
	"ABS%f%C",	armdps,	0,	"F%s,F%d",
	"NEG%f%C",	armdps,	0,	"F%s,F%d",
	"SQRT%f%C",	armdps,	0,	"F%s,F%d",
	"CMP%f%C",	armdps,	0,	"F%s,F%d",
	"CMPE%f%C",	armdps,	0,	"F%s,F%d",
	"CMP%f%C",	armdps,	0,	"$0.0,F%d",
	"CMPE%f%C",	armdps,	0,	"$0.0,F%d",

/* 117 */
	"MOV%F%R%C",	armdps, 0,	"F%s,F%d",

/* 118 */
	"MOVW%C",	armdps, 0,	"R%d,F%n",
	"MOVW%C",	armdps, 0,	"F%n,R%d",
	"MOVW%C",	armdps, 0,	"R%d,%x",
	"MOVW%C",	armdps, 0,	"%x,R%d",

/* 122 */
	"MOV%f%C",	armvstdi,	0,	"F%d,%I",
	"MOV%f%C",	armvstdi,	0,	"%I,F%d",

/* 124 */
	"BKPT%C",	armbpt,	0,		"$#%i",
	"BX%C",		armdps,	armfbx,	"(R%s)",
	"BXJ%C",	armdps,	armfbx,	"(R%s)",
	"BLX%C",	armdps,	armfbx,	"(R%s)",
};

static void
gaddr(Instr *i)
{
	*i->curr++ = '$';
	i->curr += gsymoff(i->curr, i->end-i->curr, i->imm, CANY);
}

static	int8_t *mode[] = { 0, "IA", "DB", "IB" };
static	int8_t *pw[] = { "P", "PW", 0, "W" };
static	int8_t *sw[] = { 0, "W", "S", "SW" };

static void
format(int8_t *mnemonic, Instr *i, int8_t *f)
{
	int j, k, m, n;
	int g;
	int8_t *fmt;

	if(mnemonic)
		format(0, i, mnemonic);
	if(f == 0)
		return;
	if(mnemonic)
		if(i->curr < i->end)
			*i->curr++ = '\t';
	for ( ; *f && i->curr < i->end; f++) {
		if(*f != '%') {
			*i->curr++ = *f;
			continue;
		}
		switch (*++f) {

		case 'C':	/* .CONDITION */
			if(cond[i->cond])
				bprint(i, ".%s", cond[i->cond]);
			break;

		case 'S':	/* .STORE */
			if(i->store)
				bprint(i, ".S");
			break;

		case 'P':	/* P & U bits for block move */
			n = (i->w >>23) & 0x3;
			if (mode[n])
				bprint(i, ".%s", mode[n]);
			break;

		case 'p':	/* P & W bits for single data xfer*/
			if (pw[i->store])
				bprint(i, ".%s", pw[i->store]);
			break;

		case 'a':	/* S & W bits for single data xfer*/
			if (sw[i->store])
				bprint(i, ".%s", sw[i->store]);
			break;

		case 's':
			bprint(i, "%d", i->rs & 0xf);
			break;
				
		case 'M':
			bprint(i, "%lud", (i->w>>8) & 0xf);
			break;
				
		case 'm':
			bprint(i, "%lud", (i->w>>7) & 0x1f);
			break;

		case 'h':
			bprint(i, shtype[(i->w>>5) & 0x3]);
			break;

		case 'u':		/* Signed/unsigned Byte/Halfword */
			bprint(i, hb[(i->w>>5) & 0x3]);
			break;

		case 'I':
			if (i->rn == 13) {
				if (plocal(i))
					break;
			}
			g = 0;
			fmt = "#%lx(R%d)";
			if (i->rn == 15) {
				/* convert load of offset(PC) to a load immediate */
				if (get4(i->map, i->addr+i->imm+8, (uint32_t*)&i->imm) > 0)
				{
					g = 1;
					fmt = "";
				}
			}
			if (mach->sb)
			{
				if (i->rd == 11) {
					uint32_t nxti;

					if (get4(i->map, i->addr+4, &nxti) > 0) {
						if ((nxti & 0x0e0f0fff) == 0x060c000b) {
							i->imm += mach->sb;
							g = 1;
							fmt = "-SB";
						}
					}
				}
				if (i->rn == 12)
				{
					i->imm += mach->sb;
					g = 1;
					fmt = "-SB(SB)";
				}
			}
			if (g)
			{
				gaddr(i);
				bprint(i, fmt, i->rn);
			}
			else
				bprint(i, fmt, i->imm, i->rn);
			break;
		case 'U':		/* Add/subtract from base */
			bprint(i, addsub[(i->w >> 23) & 1]);
			break;

		case 'n':
			bprint(i, "%d", i->rn);
			break;

		case 'd':
			bprint(i, "%d", i->rd);
			break;

		case 'i':
			bprint(i, "%lux", i->imm);
			break;

		case 'b':
			i->curr += symoff(i->curr, i->end-i->curr,
				(uint32_t)i->imm, CTEXT);
			break;

		case 'g':
			i->curr += gsymoff(i->curr, i->end-i->curr,
				i->imm, CANY);
			break;

		case 'f':
			switch((i->w >> 8) & 0xF){
			case 10:
				bprint(i, "F");
				break;
			case 11:
				bprint(i, "D");
				break;
			}
			break;

		case 'F':
			switch(((i->w >> 15) & 0xE) + ((i->w >> 8) & 0x1)){
			case 0x0:
				bprint(i, ((i->w >> 7) & 0x1)? "WF" : "WF.U");
				break;
			case 0x1:
				bprint(i, ((i->w >> 7) & 0x1)? "WD" : "WD.U");
				break;
			case 0x8:
				bprint(i, "FW.U");
				break;
			case 0x9:
				bprint(i, "DW.U");
				break;
			case 0xA:
				bprint(i, "FW");
				break;
			case 0xB:
				bprint(i, "DW");
				break;
			case 0xE:
				bprint(i, "FD");
				break;
			case 0xF:
				bprint(i, "DF");
				break;
			}
			break;

		case 'R':
			if(((i->w >> 7) & 0x1) == 0)
				bprint(i, "R");
			break;

		case 'x':
			switch(i->rn){
			case 0:
				bprint(i, "FPSID");
				break;
			case 1:
				bprint(i, "FPSCR");
				break;
			case 2:
				bprint(i, "FPEXC");
				break;
			default:
				bprint(i, "FPS(%d)", i->rn);
				break;
			}
			break;

		case 'r':
			n = i->imm&0xffff;
			j = 0;
			k = 0;
			while(n) {
				m = j;
				while(n&0x1) {
					j++;
					n >>= 1;
				}
				if(j != m) {
					if(k)
						bprint(i, ",");
					if(j == m+1)
						bprint(i, "R%d", m);
					else
						bprint(i, "R%d-R%d", m, j-1);
					k = 1;
				}
				j++;
				n >>= 1;
			}
			break;

		case '\0':
			*i->curr++ = '%';
			return;

		default:
			bprint(i, "%%%c", *f);
			break;
		}
	}
	*i->curr = 0;
}

static int
printins(Map *map, uint64_t pc, int8_t *buf, int n)
{
	Instr i;

	i.curr = buf;
	i.end = buf+n-1;
	if(decode(map, pc, &i) < 0)
		return -1;

	(*opcodes[i.op].fmt)(&opcodes[i.op], &i);
	return 4;
}

static int
arminst(Map *map, uint64_t pc, int8_t modifier, int8_t *buf, int n)
{
	USED(modifier);
	return printins(map, pc, buf, n);
}

static int
armdas(Map *map, uint64_t pc, int8_t *buf, int n)
{
	Instr i;

	i.curr = buf;
	i.end = buf+n;
	if(decode(map, pc, &i) < 0)
		return -1;
	if(i.end-i.curr > 8)
		i.curr = _hexify(buf, i.w, 7);
	*i.curr = 0;
	return 4;
}

static int
arminstlen(Map *map, uint64_t pc)
{
	Instr i;

	if(decode(map, pc, &i) < 0)
		return -1;
	return 4;
}

static int
armfoll(Map *map, uint64_t pc, Rgetter rget, uint64_t *foll)
{
	uint64_t d;
	Instr i;

	if(decode(map, pc, &i) < 0)
		return -1;

	if(opcodes[i.op].foll) {
		d = (*opcodes[i.op].foll)(map, rget, &i, pc);
		if(d == -1)
			return -1;
	} else
		d = pc+4;

	foll[0] = d;
	return 1;
}
