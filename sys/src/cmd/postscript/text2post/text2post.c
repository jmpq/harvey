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
#include <ctype.h>
#include <bio.h>
#include <comments.h>
#include <path.h>

#define UNKNOWNCHAR	"/sys/lib/postscript/prologues/pjw.char.ps"

int8_t	*optnames = "a:c:f:l:m:n:o:p:s:t:x:y:P:";

double	aspectratio = 1.0;
Biobuf	inbuf, outbuf;
Biobuf	*bin, *bout;
int	char_no = 0;		/* character to be done on a line */
int	copies = 1;
int	formsperpage = 1;
int	in_string;		/* flag: we are inside a Postscript string */
int	landscape = 0;
int	line_no = 0;		/* line number on a page */
int	linesperpage = 66;
double	magnification = 1.0;
int	page_no = 0;		/* page number in a document */
int	pages_printed;
int8_t	*passthrough = 0;
int	pointsize = 10;
int	spaces = 0;
int	tabs = 0;
double	xoffset = .25;
double	yoffset = .25;

static int pplistmaxsize = 0;

uint8_t *pplist = 0;		/* bitmap list for storing pages to print */

struct strtab {
	int	size;
	int8_t	*str;
	int	used;
};

struct strtab charcode[256] = {
	{4, "\\000"}, {4, "\\001"}, {4, "\\002"}, {4, "\\003"},
	{4, "\\004"}, {4, "\\005"}, {4, "\\006"}, {4, "\\007"},
	{4, "\\010"}, {4, "\\011"}, {4, "\\012"}, {4, "\\013"},
	{4, "\\014"}, {4, "\\015"}, {4, "\\016"}, {4, "\\017"},
	{4, "\\020"}, {4, "\\021"}, {4, "\\022"}, {4, "\\023"},
	{4, "\\024"}, {4, "\\025"}, {4, "\\026"}, {4, "\\027"},
	{4, "\\030"}, {4, "\\031"}, {4, "\\032"}, {4, "\\033"},
	{4, "\\034"}, {4, "\\035"}, {4, "\\036"}, {4, "\\037"},
	{1, " "}, {1, "!"}, {1, "\""}, {1, "#"},
	{1, "$"}, {1, "%"}, {1, "&"}, {1, "'"},
	{2, "\\("}, {2, "\\)"}, {1, "*"}, {1, "+"},
	{1, ","}, {1, "-"}, {1, "."}, {1, "/"},
	{1, "0"}, {1, "1"}, {1, "2"}, {1, "3"},
	{1, "4"}, {1, "5"}, {1, "6"}, {1, "7"},
	{1, "8"}, {1, "9"}, {1, ":"}, {1, ";"},
	{1, "<"}, {1, "="}, {1, ">"}, {1, "?"},
	{1, "@"}, {1, "A"}, {1, "B"}, {1, "C"},
	{1, "D"}, {1, "E"}, {1, "F"}, {1, "G"},
	{1, "H"}, {1, "I"}, {1, "J"}, {1, "K"},
	{1, "L"}, {1, "M"}, {1, "N"}, {1, "O"},
	{1, "P"}, {1, "Q"}, {1, "R"}, {1, "S"},
	{1, "T"}, {1, "U"}, {1, "V"}, {1, "W"},
	{1, "X"}, {1, "Y"}, {1, "Z"}, {1, "["},
	{2, "\\\\"}, {1, "]"}, {1, "^"}, {1, "_"},
	{1, "`"}, {1, "a"}, {1, "b"}, {1, "c"},
	{1, "d"}, {1, "e"}, {1, "f"}, {1, "g"},
	{1, "h"}, {1, "i"}, {1, "j"}, {1, "k"},
	{1, "l"}, {1, "m"}, {1, "n"}, {1, "o"},
	{1, "p"}, {1, "q"}, {1, "r"}, {1, "s"},
	{1, "t"}, {1, "u"}, {1, "v"}, {1, "w"},
	{1, "x"}, {1, "y"}, {1, "z"}, {1, "{"},
	{1, "|"}, {1, "}"}, {1, "~"}, {4, "\\177"},
	{4, "\\200"}, {4, "\\201"}, {4, "\\202"}, {4, "\\203"},
	{4, "\\204"}, {4, "\\205"}, {4, "\\206"}, {4, "\\207"},
	{4, "\\210"}, {4, "\\211"}, {4, "\\212"}, {4, "\\213"},
	{4, "\\214"}, {4, "\\215"}, {4, "\\216"}, {4, "\\217"},
	{4, "\\220"}, {4, "\\221"}, {4, "\\222"}, {4, "\\223"},
	{4, "\\224"}, {4, "\\225"}, {4, "\\226"}, {4, "\\227"},
	{4, "\\230"}, {4, "\\231"}, {4, "\\232"}, {4, "\\233"},
	{4, "\\234"}, {4, "\\235"}, {4, "\\236"}, {4, "\\237"},
	{4, "\\240"}, {4, "\\241"}, {4, "\\242"}, {4, "\\243"},
	{4, "\\244"}, {4, "\\245"}, {4, "\\246"}, {4, "\\247"},
	{4, "\\250"}, {4, "\\251"}, {4, "\\252"}, {4, "\\253"},
	{4, "\\254"}, {4, "\\255"}, {4, "\\256"}, {4, "\\257"},
	{4, "\\260"}, {4, "\\261"}, {4, "\\262"}, {4, "\\263"},
	{4, "\\264"}, {4, "\\265"}, {4, "\\266"}, {4, "\\267"},
	{4, "\\270"}, {4, "\\271"}, {4, "\\272"}, {4, "\\273"},
	{4, "\\274"}, {4, "\\275"}, {4, "\\276"}, {4, "\\277"},
	{4, "\\300"}, {4, "\\301"}, {4, "\\302"}, {4, "\\303"},
	{4, "\\304"}, {4, "\\305"}, {4, "\\306"}, {4, "\\307"},
	{4, "\\310"}, {4, "\\311"}, {4, "\\312"}, {4, "\\313"},
	{4, "\\314"}, {4, "\\315"}, {4, "\\316"}, {4, "\\317"},
	{4, "\\320"}, {4, "\\321"}, {4, "\\322"}, {4, "\\323"},
	{4, "\\324"}, {4, "\\325"}, {4, "\\326"}, {4, "\\327"},
	{4, "\\330"}, {4, "\\331"}, {4, "\\332"}, {4, "\\333"},
	{4, "\\334"}, {4, "\\335"}, {4, "\\336"}, {4, "\\337"},
	{4, "\\340"}, {4, "\\341"}, {4, "\\342"}, {4, "\\343"},
	{4, "\\344"}, {4, "\\345"}, {4, "\\346"}, {4, "\\347"},
	{4, "\\350"}, {4, "\\351"}, {4, "\\352"}, {4, "\\353"},
	{4, "\\354"}, {4, "\\355"}, {4, "\\356"}, {4, "\\357"},
	{4, "\\360"}, {4, "\\361"}, {4, "\\362"}, {4, "\\363"},
	{4, "\\364"}, {4, "\\365"}, {4, "\\366"}, {4, "\\367"},
	{4, "\\370"}, {4, "\\371"}, {4, "\\372"}, {4, "\\373"},
	{4, "\\374"}, {4, "\\375"}, {4, "\\376"}, {4, "\\377"}
};

#define FONTABSIZE 0x27

struct strtab fontname[FONTABSIZE] = {
	{19, "LucidaSansUnicode00", 0},
	{19, "LucidaSansUnicode01", 0},
	{19, "LucidaSansUnicode02", 0},
	{19, "LucidaSansUnicode03", 0},
	{19, "LucidaSansUnicode04", 0},
	{19, "LucidaSansUnicode05", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{0, "", 0},
	{19, "LucidaSansUnicode20", 0},
	{19, "LucidaSansUnicode21", 0},
	{19, "LucidaSansUnicode22", 0},
	{0, "", 0},
	{19, "LucidaSansUnicode24", 0},
	{19, "LucidaSansUnicode25", 0},
	{7, "Courier", 0}
};

/* This was taken from postprint */

int
cat(int8_t *filename) {
	int n;
	int8_t buf[Bsize];
	Biobuf *bfile;

	if ((bfile = Bopen(filename, OREAD)) == nil)
		return(1);
	while ((n = Bread(bfile, buf, Bsize)) > 0)
		if (Bwrite(bout, buf, n) != n)
			break;
	Bterm(bfile);
	if (n != 0)
		return(1);
	return(0);
}

void
prologues(void) {
	int8_t *ts;
	int tabstop;

	Bprint(bout, "%s", CONFORMING);
	Bprint(bout, "%s %s\n", VERSION, PROGRAMVERSION);
	Bprint(bout, "%s %s\n", DOCUMENTFONTS, ATEND);
	Bprint(bout, "%s %s\n", PAGES, ATEND);
	Bprint(bout, "%s", ENDCOMMENTS);

	if (cat(POSTPRINT))
		sysfatal("can't read %s: %r", POSTPRINT);

	if (DOROUND)
		cat(ROUNDPAGE);

	tabstop = 0;
	ts = getenv("tabstop");
	if(ts != nil)
		tabstop = strtol(ts, nil, 0);
	if(tabstop == 0)
		tabstop = 8;
	Bprint(bout, "/f {findfont pointsize scalefont setfont} bind def\n");
	Bprint(bout, "/tabwidth /Courier f (");
	while(tabstop--)
		Bputc(bout, 'n');
	Bprint(bout, ") stringwidth pop def\n");
	Bprint(bout, "/tab {tabwidth 0 ne {currentpoint 3 1 roll exch tabwidth mul add tabwidth\n");
	Bprint(bout, "\tdiv truncate tabwidth mul exch moveto} if} bind def\n");
	Bprint(bout, "/spacewidth /%s f ( ) stringwidth pop def\n", fontname[0].str);
	Bprint(bout, "/sp {spacewidth mul 0 rmoveto} bind def\n");
	Bprint(bout, "%s", ENDPROLOG);
	Bprint(bout, "%s", BEGINSETUP);
	Bprint(bout, "mark\n");

	if (formsperpage > 1) {
		Bprint(bout, "%s %d\n", FORMSPERPAGE, formsperpage);
		Bprint(bout, "/formsperpage %d def\n", formsperpage);
	}
	if (aspectratio != 1) Bprint(bout, "/aspectratio %g def\n", aspectratio);
	if (copies != 1) Bprint(bout, "/#copies %d store\n", copies);
	if (landscape) Bprint(bout, "/landscape true def\n");
	if (magnification != 1) Bprint(bout, "/magnification %g def\n", magnification);
	if (pointsize != 10) Bprint(bout, "/pointsize %d def\n", pointsize);
	if (xoffset != .25) Bprint(bout, "/xoffset %g def\n", xoffset);
	if (yoffset != .25) Bprint(bout, "/yoffset %g def\n", yoffset);
	cat(ENCODINGDIR"/Latin1.enc");
	if (passthrough != 0) Bprint(bout, "%s\n", passthrough);
	Bprint(bout, "setup\n");
	if (formsperpage > 1) {
		cat(FORMFILE);
		Bprint(bout, "%d setupforms \n", formsperpage);
	}
	if (cat(UNKNOWNCHAR))
		fprint(2, "cannot open %s: %r\n", UNKNOWNCHAR);
	Bprint(bout, "%s", ENDSETUP);
}

int
pageon(void) {
	if (pplist == 0 && page_no != 0)
		return(1);	/* no page list, print all pages */
	if (page_no/8 < pplistmaxsize && (pplist[page_no/8] & 1<<(page_no%8)))
		return(1);
	else
		return(0);
}

void
startpage(void) {
	++char_no;
	++line_no;
	++page_no;
	if (pageon()) {
		++pages_printed;
		Bprint(bout, "%s %d %d\n", PAGE, page_no, pages_printed);
		Bprint(bout, "/saveobj save def\n");
		Bprint(bout, "mark\n");
		Bprint(bout, "%d pagesetup\n", pages_printed);
	}
}

void
endpage(void) {
	line_no = 0;
	char_no = 0;
	if (pageon()) {
		Bprint(bout, "cleartomark\n");
		Bprint(bout, "showpage\n");
		Bprint(bout, "saveobj restore\n");
		Bprint(bout, "%s %d %d\n", ENDPAGE, page_no, pages_printed);
	}
}

void
startstring(void) {
	if (!in_string) {
		if (pageon()) Bprint(bout, "(");
		in_string = 1;
	}
}

void
endstring(void) {
	if (in_string) {
		if (pageon()) Bprint(bout, ") show ");
		in_string = 0;
	}
}

void
prspace(void) {
	if (spaces) {
		endstring();
		if (pageon()) Bprint(bout, "%d sp ", spaces);
		spaces = 0;
	}
}

void
prtab(void) {
	if (tabs) {
		endstring();
		if (pageon()) Bprint(bout, "%d tab ", tabs);
		tabs = 0;
	}
}

void
txt2post(void) {
	int lastfont = -1;
	int lastchar = -1;
	int thisfont, thischar;
	int32_t r;

	in_string = 0;
	char_no = 0;
	line_no = 0;
	page_no = 0;
	spaces = 0;
	fontname[0].used++;
	while ((r = Bgetrune(bin)) >= 0) {
		thischar = r & 0xff;
		thisfont = (r>>8) & 0xff;

		if (line_no == 0 && char_no == 0)
			startpage();

		if (line_no == 1 && char_no == 1) {
			if (pageon()) Bprint(bout, " /%s f\n", fontname[thisfont].str);
			lastfont = thisfont;
		}

		switch (r) {
		case ' ':
			prtab();
			if (lastfont > 0) {
				spaces++;
				continue;
			}
			break;
		case '\n':
		case '\f':
			startstring();
			if (pageon()) Bprint(bout, ")l\n");
			char_no = 1;
			in_string = 0;
			spaces = 0;
			tabs = 0;
			if (++line_no > linesperpage || r == '\f')
				endpage();
			lastchar = -1;
			continue;
		case '\t':
			prspace();
			tabs++;
			char_no++;
			lastchar = -1;
			continue;
		case '\b':
			/* just toss out backspaces for now */
			if (lastchar != -1) {
				endstring();
				if (pageon()) Bprint(bout, "(%s) stringwidth pop neg 0 rmoveto ", charcode[lastchar].str);
			}
			char_no++;
			lastchar = -1;
			continue;
		}

		/* do something if font is out of table range */
		if (thisfont>=FONTABSIZE || fontname[thisfont].size == 0) {
			prspace();
			prtab();
			endstring();
			Bprint(bout, "pw ");
			char_no++;
			lastchar = -1;
			continue;
		}

		if (thisfont != lastfont) {
			endstring();
			if (pageon()) {
				Bprint(bout, "/%s f\n", fontname[thisfont].str);
			}
			fontname[thisfont].used++;
		}
		prspace();
		prtab();
		startstring();
		if (pageon()) Bprint(bout, "%s", charcode[thischar].str);
/*		if (pageon()) Bprint(bout, "%2.2x", thischar); /* try hex strings*/
		char_no++;
		lastchar = thischar;
		lastfont = thisfont;
	}
	if (line_no != 0 || char_no != 0) {
		if (char_no != 1) {
			fprint(2, "premature EOF: newline appended\n");
			startstring();
			if (pageon()) Bprint(bout, ")l\n");
		}
		endpage();
	}
}

void
pagelist(int8_t *list) {
	int8_t c;
	int n, state, start;
	unsigned m;

	if (list == 0)
		return;
	start = 0;
	state = 1;
	while ((c=*list) != '\0') {
		n = 0;
		while (isdigit(c)) {
			n = n * 10 + c - '0';
			c = *++list;
		}
		switch (state) {
		case 1:
			start = n;
			/* fall through */
		case 2:
			if (n/8+1 > pplistmaxsize) {
				pplistmaxsize = n/8+1;
				if ((pplist = realloc(pplist, n/8+1)) == 0)
					sysfatal("malloc");
			}
			for (m=start; m<=n; m++)
				pplist[m/8] |= 1<<(m%8);
			break;
		}
		switch (c) {
		case '-':
			state = 2;
			list++;
			break;
		case ',':
			state = 1;
			list++;
			break;
		case '\0':
			break;
		}
	}
}

void
finish(void) {
	int i;

	Bprint(bout, "%s", TRAILER);
	Bprint(bout, "done\n");
	Bprint(bout, "%s", DOCUMENTFONTS);

	for (i=0; i<FONTABSIZE; i++)
		if (fontname[i].used)
			Bprint(bout, " %s", fontname[i].str);
	Bprint(bout, "\n");

	Bprint(bout, "%s %d\n", PAGES, pages_printed);
}

void
main(int argc, char *argv[]) {
	int i;
	char *t;

	bin = &inbuf;
	bout = &outbuf;
	if (Binit(bout, 1, OWRITE) == Beof)
		sysfatal("Binit");

	ARGBEGIN{
	case 'a':			/* aspect ratio */
		aspectratio = atof(ARGF());
		break;
	case 'c':			/* copies */
		copies = atoi(ARGF());
		break;
	case 'f':			/* primary font, for now */
		t = ARGF();
		fontname[0].str = malloc(strlen(t)+1);
		strcpy(fontname[0].str, t);
		break;
	case 'l':			/* lines per page */
		linesperpage = atoi(ARGF());
		break;
	case 'm':			/* magnification */
		magnification = atof(ARGF());
		break;
	case 'n':			/* forms per page */
		formsperpage = atoi(ARGF());
		break;
	case 'o':			/* output page list */
		pagelist(ARGF());
		break;
	case 'p':			/* landscape or portrait mode */
		if ( ARGF()[0] == 'l' )
			landscape = 1;
		else
			landscape = 0;
		break;
	case 's':			/* point size */
		pointsize = atoi(ARGF());
		break;
	case 'x':			/* shift things horizontally */
		xoffset = atof(ARGF());
		break;

	case 'y':			/* and vertically on the page */
		yoffset = atof(ARGF());
		break;
	case 'P':			/* PostScript pass through */
		t = ARGF();
		i = strlen(t) + 1;
		passthrough = malloc(i);
		if (passthrough == 0)
			sysfatal("malloc");
		strncpy(passthrough, t, i);
		break;
	default:
		fprint(2, "unknown option %C\n", ARGC());
		break;
	}ARGEND;

	prologues();
	if (argc <= 0) {
		if (Binit(bin, 0, OREAD) == Beof)
			sysfatal("cannot Binit stdin");
		txt2post();
	}
	for (i=0; i<argc; i++) {
		bin = Bopen(argv[i], OREAD);
		if (bin == nil) {
			fprint(2, "cannot open %s: %r\n", argv[i]);
			continue;
		}
		txt2post();
		Bterm(bin);
	}
	finish();
	exits("");
}
