/* $Id: fileparser.c,v 5.5 2001/11/29 20:31:36 bertg Exp $
 *
 * XPilot, a multiplayer gravity war game.  Copyright (C) 1991-2001 by
 *
 *      Bjørn Stabell        <bjoern@xpilot.org>
 *      Ken Ronny Schouten   <ken@xpilot.org>
 *      Bert Gijsbers        <bert@xpilot.org>
 *      Dick Balaska         <dick@xpilot.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>

#ifndef _WINDOWS
# include <unistd.h>
#endif

#ifdef _WINDOWS
# include <windows.h>
# include <io.h>
# define read(__a, __b, __c)	_read(__a, __b, __c)
#endif

#define SERVER
#include "version.h"
#include "config.h"
#include "serverconst.h"
#include "global.h"
#include "proto.h"
#include "defaults.h"
#include "map.h"
#include "error.h"
#include "types.h"
#include "commonproto.h"


char fileparser_version[] = VERSION;



static char	*FileName;
static int	LineNumber;


/*
 * Skips to the end of the line.
 */
static void toeol(char **map_ptr)
{
    int		ich;

    while (**map_ptr) {
	ich = **map_ptr;
	(*map_ptr)++;
        if (ich == '\n') {
            ++LineNumber;
            return;
        }
    }
}


/*
 * Skips to the first non-whitespace character, returning that character.
 */
static int skipspace(char **map_ptr)
{
    int		ich;

    while (**map_ptr) {
	ich = **map_ptr;
	(*map_ptr)++;
        if (ich == '\n') {
            ++LineNumber;
            return ich;
        }
        if (!isascii(ich) || !isspace(ich))
            return ich;
    }
    return '\0';
}


/*
 * Read in a multiline value.
 */
static char *getMultilineValue(char **map_ptr, char *delimiter)
{
    char       *s = (char *)malloc(32768);
    int         i = 0;
    int         slen = 32768;
    char       *bol;
    int         ich;

    bol = s;
    for (;;) {
	ich = **map_ptr;
	(*map_ptr)++;
	if (ich == '\0') {
	    s = (char *)realloc(s, i + 1);
	    s[i] = '\0';
	    return s;
	}
	if (i == slen) {
	    char       *t = s;

	    slen += (slen / 2) + 8192;
	    s = (char *)realloc(s, slen);
	    bol += s - t;
	}
	if (ich == '\n') {
	    s[i] = 0;
	    if (delimiter && !strcmp(bol, delimiter)) {
		char       *t = s;

		s = (char *)realloc(s, bol - s + 1);
		s[bol - t] = '\0';
		return s;
	    }
	    bol = &s[i + 1];
	    ++LineNumber;
	}
	s[i++] = ich;
    }
}


/*
 * Parse a standard line from a defaults file, in the form Name: value.
 * Name must start at the beginning of the line with an alphabetic character.
 * Whitespace within name is ignored. Value may contain any character other
 * than # or newline, but leading and trailing whitespace are discarded.
 *
 * Characters after a # are ignored - this can be used for comments.
 *
 * If value begins with \override:, the override flag is set when
 * Option_set_value is called, so that this value will override
 * an existing value.
 *
 * If value starts with \multiline:, then the rest of the line is used
 * as a delimiter, and subsequent lines are read and saved as the value
 * until the delimiter is encountered.   No interpretation is done on
 * the text in the multiline sequence, so # does not serve as a comment
 * character there, and newlines and whitespace are not discarded.
 *
 * Lines can also be in the form "define: name value".
 * And similarly "define: name \multiline: TAG".
 *
 * Names can be macros which are expanded inplace with the keyword expand like:
 * expand: name
 *
 */
#define EXPAND					\
    if (i == slen) {				\
	s = (char *)realloc(s, slen *= 2);	\
    }
static void parseLine(char **map_ptr, optOrigin opt_origin)
{
    int		ich;
    char       *value,
	       *head,
	       *name,
	       *s = (char *)malloc(128);
    char       *p;
    int         slen = 128;
    int         i = 0;
    int         override = 0;
    int         multiline = 0;

    ich = **map_ptr;
    (*map_ptr)++;

    /* Skip blank lines... */
    if (ich == '\n') {
	++LineNumber;
	free(s);
	return;
    }
    /* Skip leading space... */
    if (isascii(ich) && isspace(ich)) {
	ich = skipspace(map_ptr);
	if (ich == '\n') {
	    free(s);
	    return;
	}
    }
    /* Skip lines that start with comment character... */
    if (ich == '#') {
	toeol(map_ptr);
	free(s);
	return;
    }
    /* Skip lines that start with the end of the file... :') */
    if (ich == '\0') {
	free(s);
	return;
    }
    /* *** I18nize? *** */
    if (!isascii(ich) || !isalpha(ich)) {
	error("%s line %d: Names must start with an alphabetic.\n",
	      FileName, LineNumber);
	toeol(map_ptr);
	free(s);
	return;
    }
    s[i++] = ich;
    do {
	ich = **map_ptr;
	(*map_ptr)++;

	if (ich == '\n' || ich == '#' || ich == '\0') {
	    error("%s line %d: No colon found on line.\n",
		  FileName, LineNumber);
	    if (ich == '#')
		toeol(map_ptr);
	    else
		++LineNumber;
	    free(s);
	    return;
	}
	if (isascii(ich) && isspace(ich))
	    continue;
	if (ich == ':')
	    break;
	EXPAND;
	s[i++] = ich;
    } while (1);

    ich = skipspace(map_ptr);

    EXPAND;
    s[i++] = '\0';
    name = s;

    s = (char *)malloc(slen = 128);
    i = 0;
    do {
	EXPAND;
	s[i++] = ich;
	ich = **map_ptr;
	(*map_ptr)++;
    } while (ich != '\0' && ich != '#' && ich != '\n');

    if (ich == '\n')
	++LineNumber;

    if (ich == '#')
	toeol(map_ptr);

    EXPAND;
    s[i++] = 0;
    head = value = s;
    s = value + strlen(value) - 1;
    while (s >= value && isascii(*s) && isspace(*s))
	--s;
    *++s = 0;

    /* Deal with `define: MACRO \multiline: TAG'. */
    if (strcmp(name, "define") == 0) {
	p = value;
	while (*p && isascii(*p) && !isspace(*p)) {
	    p++;
	}
	*p++ = '\0';

	/* name becomes value */
	free(name);
	name = (char *)malloc(p - value + 1);
	memcpy(name, value, p - value);
	name[p - value] = '\0';

	/* Move value to \multiline */
	while (*p && isspace(*p)) {
	    p++;
	}
	value = p;
    }

    if (!strncmp(value, "\\override:", 10)) {
	override = 1;
	value += 10;
    }
    while (*value && isascii(*value) && isspace(*value))
	++value;
    if (!strncmp(value, "\\multiline:", 11)) {
	multiline = 1;
	value += 11;
    }
    while (*value && isascii(*value) && isspace(*value))
	++value;
    if (!*value) {
	error("%s line %d: no value specified.\n",
	      FileName, LineNumber);
	free(name);
	free(head);
	return;
    }
    if (multiline) {
	value = getMultilineValue(map_ptr, value);
    }

    /* Deal with `expand: MACRO'. */
    if (strcmp(name, "expand") == 0) {
	expandKeyword(value);
    }
#ifdef REGIONS /* not yet */
    /* Deal with `region: \multiline: TAG'. */
    else if (strcmp(name, "region") == 0) {
	if (!multiline) { /* Must be multiline. */
	    error("regions must use `\\multiline:'.\n");
	    free(name);
	    free(head);
	    return;
	}
	p = value;
	while (*p) {
	    parseLine(&p, opt_origin);
	}
    }
#endif
    else {
	Option_set_value(name, value, override, opt_origin);
    }

    /*
     * if (multiline) free (value);
     */
    if (multiline) free (value);
    free(name);
    free(head);
    return;
}
#undef EXPAND

/*
 * Parse a file containing defaults (and possibly a map).
 */
static bool parseOpenFile(FILE *ifile, optOrigin opt_origin)
{
    int		fd, map_offset, map_size, n;
    char       *map_buf;

    LineNumber = 1;

    fd = fileno(ifile);

    /* kps - first try the new map format */
    if (isXp2MapFile(fd)) {
	is_polygon_map = TRUE;
	return parseXp2MapFile(fd, opt_origin);
    }

    /* Using a 200 map sample, the average map size is 37k.
       This chunk size could be increased to avoid lots of
       reallocs. */
#define MAP_CHUNK_SIZE	8192

    map_offset = 0;
    map_size = 2*MAP_CHUNK_SIZE;
    map_buf = (char *) malloc(map_size + 1);
    if (!map_buf) {
	error("Not enough memory to read the map!");
	return false;
    }

    for (;;) {
	n = read(fd, &map_buf[map_offset], map_size - map_offset);
	if (n < 0) {
	    error("Error reading map!");
	    free(map_buf);
	    return false;
	}
	if (n == 0) {
	    break;
	}
	map_offset += n;

	if (map_size - map_offset < MAP_CHUNK_SIZE) {
	    map_size += (map_size / 2) + MAP_CHUNK_SIZE;
	    map_buf = (char *) realloc(map_buf, map_size + 1);
	    if (!map_buf) {
		error("Not enough memory to read the map!");
		return false;
	    }
	}
    }

    map_buf = (char *) realloc(map_buf, map_offset + 1);
    map_buf[map_offset] = '\0'; /* EOF */

    if (isdigit(*map_buf)) {
	errno = 0;
	error("%s is in old (v1.x) format, please convert it with mapmapper",
	      FileName);
	free(map_buf);
	return false;
    } else {
	/* Parse all the lines in the file. */
	char *map_ptr = map_buf;
	while (*map_ptr) {
	    parseLine(&map_ptr, opt_origin);
	}
    }

    free(map_buf);

    return true;
}


static int copyFilename(const char *file)
{
    if (FileName) {
	free(FileName);
    }
    FileName = xp_strdup(file);
    return (FileName != 0);
}


static FILE *fileOpen(const char *file)
{
    FILE *fp = fopen(file, "r");
    if (fp ) {
	if (!copyFilename(file)) {
	    fclose(fp);
	    fp = NULL;
	}
    }
    return fp;
}


static void fileClose(FILE *fp)
{
    fclose(fp);
    if (FileName) {
	free(FileName);
	FileName = NULL;
    }
}


/*
 * Test if filename has the XPilot map extension.
 */
static int hasMapExtension(const char *filename)
{
    int fnlen = strlen(filename);
    if (fnlen > 4 && !strcmp(&filename[fnlen - 4], ".xp2")){ 
	return 1;
    }
    if (fnlen > 3 && !strcmp(&filename[fnlen - 3], ".xp")){ 
	return 1;
    }
    if (fnlen > 4 && !strcmp(&filename[fnlen - 4], ".map")){ 
	return 1;
    }
    return 0;
}


/*
 * Test if filename has a directory component.
 */
static int hasDirectoryPrefix(const char *filename)
{
    static const char	sep = '/';
    return (strchr(filename, sep) != NULL);
}


/*
 * Combine a directory and a file.
 * Returns new path as dynamically allocated memory.
 */
static char *fileJoin(const char *dir, const char *file)
{
    static const char	sep = '/';
    char		*path;

    path = (char *) malloc(strlen(dir) + 1 + strlen(file) + 1);
    if (path) {
	sprintf(path, "%s%c%s", dir, sep, file);
    }
    return path;
}


/*
 * Combine a file and a filename extension.
 * Returns new path as dynamically allocated memory.
 */
static char *fileAddExtension(const char *file, const char *ext)
{
    char		*path;

    path = (char *) malloc(strlen(file) + strlen(ext) + 1);
    if (path) {
	sprintf(path, "%s%s", file, ext);
    }
    return path;
}


#if defined(COMPRESSED_MAPS)
static int	usePclose;


static int isCompressed(const char *filename)
{
    int fnlen = strlen(filename);
    int celen = strlen(Conf_zcat_ext());
    if (fnlen > celen && !strcmp(&filename[fnlen - celen], Conf_zcat_ext())) {
	return 1;
    }
    return 0;
}


static void closeCompressedFile(FILE *fp)
{
    if (usePclose) {
	pclose(fp);
	usePclose = 0;
	if (FileName) {
	    free(FileName);
	    FileName = NULL;
	}
    } else {
	fileClose(fp);
    }
}


static FILE *openCompressedFile(const char *filename)
{
    FILE		*fp = NULL;
    char		*cmdline = NULL;
    char		*newname = NULL;

    usePclose = 0;
    if (!isCompressed(filename)) {
	if (access(filename, 4) == 0) {
	    return fileOpen(filename);
	}
	newname = fileAddExtension(filename, Conf_zcat_ext());
	if (!newname) {
	    return NULL;
	}
	filename = newname;
    }
    if (access(filename, 4) == 0) {
	cmdline = (char *) malloc(strlen(Conf_zcat_format()) + strlen(filename) + 1);
	if (cmdline) {
	    sprintf(cmdline, Conf_zcat_format(), filename);
	    fp = popen(cmdline, "r");
	    if (fp) {
		usePclose = 1;
		if (!copyFilename(filename)) {
		    closeCompressedFile(fp);
		    fp = NULL;
		}
	    }
	}
    }
    if (newname) free(newname);
    if (cmdline) free(cmdline);
    return fp;
}

#else

static int isCompressed(const char *filename)
{
    return 0;
}

static void closeCompressedFile(FILE *fp)
{
    fileClose(fp);
}

static FILE *openCompressedFile(const char *filename)
{
    return fileOpen(filename);
}
#endif

/*
 * Open a map file.
 * Filename argument need not contain map filename extension
 * or compress filename extension.
 * The search order should be:
 *      filename
 *      filename.gz              if COMPRESSED_MAPS is true
 *      filename.xp2
 *      filename.xp2.gz          if COMPRESSED_MAPS is true
 *      filename.xp
 *      filename.xp.gz           if COMPRESSED_MAPS is true
 *      filename.map
 *      filename.map.gz          if COMPRESSED_MAPS is true
 *      MAPDIR filename
 *      MAPDIR filename.gz       if COMPRESSED_MAPS is true
 *      MAPDIR filename.xp2
 *      MAPDIR filename.xp2.gz   if COMPRESSED_MAPS is true
 *      MAPDIR filename.xp
 *      MAPDIR filename.xp.gz    if COMPRESSED_MAPS is true
 *      MAPDIR filename.map
 *      MAPDIR filename.map.gz   if COMPRESSED_MAPS is true
 */
static FILE *openMapFile(const char *filename)
{
    FILE		*fp = NULL;
    char		*newname;
    char		*newpath;

    fp = openCompressedFile(filename);
    if (fp) {
	return fp;
    }
    if (!isCompressed(filename)) {
	if (!hasMapExtension(filename)) {
	    newname = fileAddExtension(filename, ".xp2");
	    fp = openCompressedFile(newname);
	    free(newname);
	    if (fp) {
		return fp;
	    }
	    newname = fileAddExtension(filename, ".xp");
	    fp = openCompressedFile(newname);
	    free(newname);
	    if (fp) {
		return fp;
	    }
	    newname = fileAddExtension(filename, ".map");
	    fp = openCompressedFile(newname);
	    free(newname);
	    if (fp) {
		return fp;
	    }
	}
    }
    if (!hasDirectoryPrefix(filename)) {
	newpath = fileJoin(Conf_mapdir(), filename);
	if (!newpath) {
	    return NULL;
	}
	if (hasDirectoryPrefix(newpath)) {
	    /* call recursively. */
	    fp = openMapFile(newpath);
	}
	free(newpath);
	if (fp) {
	    return fp;
	}
    }
    return NULL;
}


static void closeMapFile(FILE *fp)
{
    closeCompressedFile(fp);
}


static FILE *openDefaultsFile(const char *filename)
{
    return fileOpen(filename);
}


static void closeDefaultsFile(FILE *fp)
{
    fileClose(fp);
}


/*
 * Parse a file containing defaults.
 */
bool parseDefaultsFile(const char *filename)
{
    FILE       *ifile;
    bool	result;

    if ((ifile = openDefaultsFile(filename)) == NULL) {
	return false;
    }
    result = parseOpenFile(ifile, OPT_DEFAULTS);
    closeDefaultsFile(ifile);

    return true;
}


/*
 * Parse a file containing password.
 */
bool parsePasswordFile(const char *filename)
{
    FILE       *ifile;
    bool	result;

    if ((ifile = openDefaultsFile(filename)) == NULL) {
	return false;
    }
    result = parseOpenFile(ifile, OPT_PASSWORD);
    closeDefaultsFile(ifile);

    return true;
}


/*
 * Parse a file containing a map.
 */
bool parseMapFile(const char *filename)
{
    FILE       *ifile;
    bool	result;

    if ((ifile = openMapFile(filename)) == NULL) {
	return false;
    }
    result = parseOpenFile(ifile, OPT_MAP);
    closeMapFile(ifile);

    return true;
}


void expandKeyword(const char *keyword)
{
    optOrigin	expand_origin;
    char	*p;

    p = Option_get_value(keyword, &expand_origin);
    if (p == NULL) {
	warn("Can't expand `%s' because it has not been defined.\n",
	      keyword);
    }
    else {
	while (*p) {
	    parseLine(&p, expand_origin);
	}
    }
}


 



/* polygon map format related stuff */
static char	*FileName;


/* kps - if this is too small, the server will probably say
 * "xpilots: Polygon 2501 (4 points) doesn't start and end at the same place"
 */
static int edg[20000 * 2]; /* !@# change pointers in poly_t when realloc poss.*/
extern int polyc;
extern int num_groups;

int *edges = edg;
int *estyleptr;
int ptscount, ecount;
char *mapd;

struct polystyle pstyles[256];
struct edgestyle estyles[256] =
{{"internal", 0, 0, 0}};	/* Style 0 is always this special style */
struct bmpstyle  bstyles[256];
poly_t *pdata;

int num_pstyles, num_bstyles, num_estyles = 1; /* "Internal" edgestyle */
int max_bases, max_balls, max_polys,max_echanges; /* !@# make static after testing done */
static int current_estyle, current_group, is_decor;

#define STORE(T,P,N,M,V)						\
    if (N >= M && ((M <= 0)						\
	? (P = (T *) malloc((M = 1) * sizeof(*P)))			\
	: (P = (T *) realloc(P, (M += M) * sizeof(*P)))) == NULL) {	\
	warn("No memory");						\
	exit(1);							\
    } else								\
	(P[N++] = V)
/* !@# add a final realloc later to free wasted memory */
#define POLYGON_MAX_OFFSET 30000


void P_edgestyle(char *id, int width, int color, int style)
{
    strlcpy(estyles[num_estyles].id, id, sizeof(estyles[0].id));
    estyles[num_estyles].color = color;
    estyles[num_estyles].width = width;
    estyles[num_estyles].style = style;
    num_estyles++;
}

void P_polystyle(char *id, int color, int texture_id, int defedge_id,
		   int flags)
{
    /* kps - add sanity checks ??? */
    if (defedge_id == 0) {
	warn("Polygon default edgestyle cannot be omitted or set "
	     "to 'internal'!");
	exit(1);
    }

    strlcpy(pstyles[num_pstyles].id, id, sizeof(pstyles[0].id));
    pstyles[num_pstyles].color = color;
    pstyles[num_pstyles].texture_id = texture_id;
    pstyles[num_pstyles].defedge_id = defedge_id;
    pstyles[num_pstyles].flags = flags;
    num_pstyles++;
}


void P_bmpstyle(char *id, char *filename, int flags)
{
    strlcpy(bstyles[num_bstyles].id, id, sizeof(bstyles[0].id));
    strlcpy(bstyles[num_bstyles].filename, filename,
	    sizeof(bstyles[0].filename));
    bstyles[num_bstyles].flags = flags;
    num_bstyles++;
}

/* current vertex */
cpos P_cv;

void P_start_polygon(int cx, int cy, int style)
{
    poly_t t;

    if (cx < 0 || cx >= World.cwidth || cy < 0 || cy > World.cheight) {
	warn("Polygon start point (%d, %d) is not inside the map"
	     "(0 <= x < %d, 0 <= y < %d)",
	     cx, cy, World.cwidth, World.cheight);
	exit(1);
    }
    if (style == -1) {
	warn("Currently you must give polygon style, no default");
	exit(1);
    }

    ptscount = 0;
    P_cv.cx = cx;
    P_cv.cy = cy;
    t.x = cx;
    t.y = cy;
    t.group = current_group;
    t.edges = edges;
    t.style = style;
    t.estyles_start = ecount;
    t.is_decor = is_decor;
    current_estyle = pstyles[style].defedge_id;
    STORE(poly_t, pdata, polyc, max_polys, t);
}


void P_offset(int offcx, int offcy, int edgestyle)
{
    if (ABS(offcx) > POLYGON_MAX_OFFSET || ABS(offcy) > POLYGON_MAX_OFFSET) {
	warn("Offset component absolute value exceeds %d (x=%d, y=%d)",
	     POLYGON_MAX_OFFSET, offcx, offcy);
	exit(1);
    }

    *edges++ = offcx;
    *edges++ = offcy;
    if (edgestyle != -1 && edgestyle != current_estyle) {
	STORE(int, estyleptr, ecount, max_echanges, ptscount);
	STORE(int, estyleptr, ecount, max_echanges, edgestyle);
	current_estyle = edgestyle;
    }
    ptscount++;
    P_cv.cx += offcx;
    P_cv.cy += offcy;
}

void P_vertex(int cx, int cy, int edgestyle)
{
    int offcx, offcy;

    offcx = cx - P_cv.cx;
    offcy = cy - P_cv.cy;

    if (offcx == 0 && offcy == 0)
	return;

    P_offset(offcx, offcy, edgestyle);
}

void P_end_polygon(void)
{
    pdata[polyc - 1].num_points = ptscount;
    pdata[polyc - 1].num_echanges = ecount -pdata[polyc - 1].estyles_start;
    STORE(int, estyleptr, ecount, max_echanges, INT_MAX);
}

void P_start_ballarea(void)
{
    current_group = ++num_groups;
    groups[current_group].type = TREASURE;
    groups[current_group].team = TEAM_NOT_SET;
    groups[current_group].hit_mask = BALL_BIT;
}

void P_end_ballarea(void)
{
    current_group = 0; 
}

void P_start_balltarget(int team)
{
    current_group = ++num_groups;
    groups[current_group].type = TREASURE;
    groups[current_group].team = team;
    groups[current_group].hit_mask
	= NONBALL_BIT | (((NOTEAM_BIT << 1) - 1) & ~(1 << team)); 
}

void P_end_balltarget(void)
{
    current_group = 0; 
}

void P_start_decor(void)
{
    is_decor = 1;
}

void P_end_decor(void)
{
    is_decor = 0;
}

int P_get_bmp_id(const char *s)
{
    int i;

    for (i = 0; i < num_bstyles; i++)
	if (!strcmp(bstyles[i].id, s))
	    return i;
    warn("Undeclared bmpstyle %s", s);
    return 0; /* kps - what if i was 0 ?, change to -1 ? */
}


int P_get_edge_id(const char *s)
{
    int i;

    for (i = 0; i < num_estyles; i++)
	if (!strcmp(estyles[i].id, s))
	    return i;
    warn("Undeclared edgestyle %s", s);
    return -1;
}


int P_get_poly_id(const char *s)
{
    int i;

    for (i = 0; i < num_pstyles; i++)
	if (!strcmp(pstyles[i].id, s))
	    return i;
    warn("Undeclared polystyle %s", s);
    return 0; /* kps - what if i was 0 ?, change to -1 ? */
}






/*
 * Add a wall polygon
 *
 * The polygon consists of a start block and and endblock and possibly
 * some full wall/fuel blocks in between. A total number of numblocks
 * blocks are part of the polygon and must be 1 or more. If numblocks
 * is one, the startblock and endblock are the same block.
 *
 * The block coordinates of the first block is (bx, by)
 *
 * The polygon will have 3 or 4 vertices.
 *
 * Idea: first assume the polygon is a rectangle, then move
 * the vertices depending on the start and end blocks.
 *
 * The vertex index:
 * 0: upper left vertex
 * 1: lower left vertex
 * 2: lower right vertex
 * 3: upper right vertex
 * 4: upper left vertex, second time
 */

void Add_wall_poly(int bx, int by, char startblock,
		   char endblock, int numblocks,
		   int polystyle, int edgestyle)
{
    int i;
    cpos pos[5]; /* positions of vertices */

    if (numblocks < 1)
	return;

    /* first assume we have a rectangle */
    /* kps - use -1 if you don't wan't the polygon corners to
     * overlap other polygons
     */
    pos[0].cx = bx * BLOCK_CLICKS;
    pos[0].cy = (by + 1) * BLOCK_CLICKS /*- 1*/;
    pos[1].cx = bx * BLOCK_CLICKS;
    pos[1].cy = by * BLOCK_CLICKS;
    pos[2].cx = (bx + numblocks) * BLOCK_CLICKS /*- 1*/;
    pos[2].cy = by * BLOCK_CLICKS;
    pos[3].cx = (bx + numblocks) * BLOCK_CLICKS /*- 1*/;
    pos[3].cy = (by + 1) * BLOCK_CLICKS /*- 1*/;
    
    /* move the vertices depending on the startblock and endblock */
    switch (startblock) {
    case FILLED:
    case REC_LU:
    case REC_LD:
    case FUEL:
	/* no need to move the leftmost 2 vertices */
	break;
    case REC_RU:
	/* move lower left vertex to the right */
	pos[1].cx += BLOCK_CLICKS;
	break;
    case REC_RD:
	/* move upper left vertex to the right */
	pos[0].cx += BLOCK_CLICKS;
	break;
    default:
	return;
    }
    
    switch (endblock) {
    case FILLED:
    case FUEL:
    case REC_RU:
    case REC_RD:
	/* no need to move the rightmost 2 vertices */
	break;
    case REC_LU:
	pos[2].cx -= BLOCK_CLICKS;
	break;
    case REC_LD:
	pos[3].cx -= BLOCK_CLICKS;
	break;
    default:
	return;
    }

    /*
     * Since we want to form a closed loop of line segments, the
     * last vertex must equal the first.
     */
    pos[4].cx = pos[0].cx;
    pos[4].cy = pos[0].cy;

    P_start_polygon(pos[0].cx, pos[0].cy, polystyle);
    for (i = 1; i <= 4; i++)
	P_vertex(pos[i].cx, pos[i].cy, edgestyle); 
    P_end_polygon();
}

/* number of vertices in polygon */
#define N (2 + 12)
void Add_treasure_polygon(treasure_t *tp, int polystyle, int edgestyle)
{
    int cx, cy, i, r, n;
    double angle;
    cpos pos[N + 1];

    /*printf(__FUNCTION__ ": team = %d\n", tp->team);*/
    cx = tp->pos.cx - BLOCK_CLICKS / 2;
    cy = tp->pos.cy - BLOCK_CLICKS / 2;

    pos[0].cx = cx;
    pos[0].cy = cy;
    pos[1].cx = cx + BLOCK_CLICKS;
    pos[1].cy = cy;

    cx = tp->pos.cx;
    cy = tp->pos.cy;
    r = BLOCK_CLICKS / 2;
    /* number of points in half circle */
    n = N - 2;

    for (i = 0; i < n; i++) {
	angle = (((double)i)/(n - 1)) * PI;
	pos[i + 2].cx = cx + r * cos(angle);
	pos[i + 2].cy = cy + r * sin(angle);
    }

    pos[N] = pos[0];

    /* create balltarget */
    P_start_balltarget(tp->team);
    P_start_polygon(pos[0].cx, pos[0].cy, polystyle);
    for (i = 1; i <= N; i++)
	P_vertex(pos[i].cx, pos[i].cy, edgestyle); 
    P_end_polygon();
    P_end_balltarget();

    /* create ballarea */
    P_start_ballarea();
    P_start_polygon(pos[0].cx, pos[0].cy, polystyle);
    for (i = 1; i <= N; i++)
	P_vertex(pos[i].cx, pos[i].cy, edgestyle); 
    P_end_polygon();
    P_end_ballarea();
}
#undef N

void Blocks_to_polygons(void)
{
    int x, y, x0 = 0;
    int i;
    int numblocks = 0;
    int inside = false;
    char startblock = 0, endblock = 0, block;
    int maxblocks = POLYGON_MAX_OFFSET / BLOCK_CLICKS;
    int es, ps;

    /* create edgestyle and polystyle */
    P_edgestyle("es", -1, 0x00FF00, 0);
    es = P_get_edge_id("es");
    P_polystyle("ps", 0x0000FF, 0, es, 0);
    ps = P_get_poly_id("ps");
    
    /*
     * x, FILLED = solid wall
     * s, REC_LU = wall triangle pointing left and up 
     * a, REC_RU = wall triangle pointing right and up 
     * w, REC_LD = wall triangle pointing left and down
     * q, REC_RD = wall triangle pointing right and down
     * #, FUEL   = fuel block
     */

    for (y = World.y - 1; y >= 0; y--) {
	for (x = 0; x < World.x; x++) {
	    block = World.block[x][y];

	    if (!inside) {
		switch (block) {
		case FILLED:
		case REC_RU:
		case REC_RD:
		case FUEL:
		    x0 = x;
		    startblock = endblock = block;
		    inside = true;
		    numblocks = 1;
		    break;

		case REC_LU:
		case REC_LD:
		    Add_wall_poly(x, y, block, block, 1, ps, es);
		    break;
		default:
		    break;
		}
	    } else {

		switch (block) {
		case FILLED:
		case FUEL:
		    numblocks++;
		    endblock = block;
		    break;

		case REC_RU:
		case REC_RD:
		    /* old polygon ends */
		    Add_wall_poly(x0, y, startblock, endblock,
				  numblocks, ps, es);
		    /* and a new one starts */
		    x0 = x;
		    startblock = endblock = block;
		    numblocks = 1;
		    break;

		case REC_LU:
		case REC_LD:
		    numblocks++;
		    endblock = block;
		    Add_wall_poly(x0, y, startblock, endblock,
				  numblocks, ps, es);
		    inside = false;
		    break;

		default:
		    /* none of the above, polygon ends */
		    Add_wall_poly(x0, y, startblock, endblock,
				  numblocks, ps, es);
		    inside = false;
		    break;
		}
	    }

	    /*
	     * We don't want the polygon to have offsets
	     * that is too big
	     */
	    if (inside && numblocks == maxblocks) {
		Add_wall_poly(x0, y, startblock, endblock,
			      numblocks, ps, es);
		inside = false;
	    }

	}

	/* end of row */
	if (inside) {
	    Add_wall_poly(x0, y, startblock, endblock,
			  numblocks, ps, es);
	    inside = false;
	}
    }

    /* kps - if you want to see the polygons in the client (use the polygon
     * protocol , do this */
    /*is_polygon_map = 1;*/
    xpprintf("Created %d polygons.\n", polyc);

    /* now handle nonwall stuff */
    for (i = 0; i < World.NumTreasures; i++) {
	Add_treasure_polygon(&World.treasures[i], ps, es);
    }


}


