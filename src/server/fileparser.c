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

static bool isXp2MapFile(int fd);
static bool parseXp2MapFile(int fd, optOrigin opt_origin);

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

#include <expat.h>

static int edg[5000 * 2]; /* !@# change pointers in poly_t when realloc poss.*/
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
int max_bases, max_balls, max_fuels, max_checks, max_polys,max_echanges; /* !@# make static after testing done */
static int current_estyle, current_group, is_decor;

static int get_bmp_id(const char *s)
{
    int i;

    for (i = 0; i < num_bstyles; i++)
	if (!strcmp(bstyles[i].id, s))
	    return i;
    warn("Undeclared bmpstyle %s", s);
    return 0;
}


static int get_edge_id(const char *s)
{
    int i;

    for (i = 0; i < num_estyles; i++)
	if (!strcmp(estyles[i].id, s))
	    return i;
    warn("Undeclared edgestyle %s", s);
    return -1;
}


static int get_poly_id(const char *s)
{
    int i;

    for (i = 0; i < num_pstyles; i++)
	if (!strcmp(pstyles[i].id, s))
	    return i;
    warn("Undeclared polystyle %s", s);
    return 0;
}


#define STORE(T,P,N,M,V)						\
    if (N >= M && ((M <= 0)						\
	? (P = (T *) malloc((M = 1) * sizeof(*P)))			\
	: (P = (T *) realloc(P, (M += M) * sizeof(*P)))) == NULL) {	\
	warn("No memory");						\
	exit(1);							\
    } else								\
	(P[N++] = V)
/* !@# add a final realloc later to free wasted memory */


static void tagstart(void *data, const char *el, const char **attr)
{
    static double scale = 1;
    static int xptag = 0;

    if (!strcasecmp(el, "XPilotMap")) {
	double version = 0;
	while (*attr) {
	    if (!strcasecmp(*attr, "version"))
		version = atof(*(attr + 1));
	    attr += 2;
	}
	if (version == 0) {
	    warn("Old(?) map file with no version number");
	    warn("Not guaranteed to work");
	}
	else if (version < 1)
	    warn("Impossible version in map file");
	else if (version > 1) {
	    warn("Map file has newer version than this server recognizes.");
	    warn("The map file might use unsupported features.");
	}
	xptag = 1;
	return;
    }

    if (!xptag) {
	fatal("This doesn't look like a map file "
	      " (XPilotMap must be first tag).");
	return; /* not reached */
    }

    if (!strcasecmp(el, "Polystyle")) {
	pstyles[num_pstyles].id[sizeof(pstyles[0].id) - 1] = 0;
	pstyles[num_pstyles].color = 0;
	pstyles[num_pstyles].texture_id = 0;
	pstyles[num_pstyles].defedge_id = 0;
	pstyles[num_pstyles].flags = 0;

	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strncpy(pstyles[num_pstyles].id, *(attr + 1),
			sizeof(pstyles[0].id) - 1);
	    if (!strcasecmp(*attr, "color"))
		pstyles[num_pstyles].color = strtol(*(attr + 1), NULL, 16);
	    if (!strcasecmp(*attr, "texture"))
		pstyles[num_pstyles].texture_id = get_bmp_id(*(attr + 1));
	    if (!strcasecmp(*attr, "defedge"))
		pstyles[num_pstyles].defedge_id = get_edge_id(*(attr + 1));
	    if (!strcasecmp(*attr, "flags"))
		pstyles[num_pstyles].flags = atoi(*(attr + 1)); /* names @!# */
	    attr += 2;
	}
	if (pstyles[num_pstyles].defedge_id == 0) {
	    warn("Polygon default edgestyle cannot be omitted or set "
		  "to 'internal'!");
	    exit(1);
	}
	num_pstyles++;
	return;
    }

    if (!strcasecmp(el, "Edgestyle")) {
	estyles[num_estyles].id[sizeof(estyles[0].id) - 1] = 0;
	estyles[num_estyles].width = 0;
	estyles[num_estyles].color = 0;
	estyles[num_estyles].style = 0;
	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strncpy(estyles[num_estyles].id, *(attr + 1),
			sizeof(estyles[0].id) - 1);
	    if (!strcasecmp(*attr, "width"))
		estyles[num_estyles].width = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "color"))
		estyles[num_estyles].color = strtol(*(attr + 1), NULL, 16);
	    if (!strcasecmp(*attr, "style")) /* !@# names later */
		estyles[num_estyles].style = atoi(*(attr + 1));
	    attr += 2;
	}
	num_estyles++;
	return;
    }

    if (!strcasecmp(el, "Bmpstyle")) {
	bstyles[num_bstyles].flags = 0;
	bstyles[num_bstyles].filename[sizeof(bstyles[0].filename) - 1] = 0;
	bstyles[num_bstyles].id[sizeof(bstyles[0].id) - 1] = 0;
/* add checks that these are filled !@# */

	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strncpy(bstyles[num_bstyles].id, *(attr + 1),
			sizeof(bstyles[0].id) - 1);
	    if (!strcasecmp(*attr, "filename"))
		strncpy(bstyles[num_bstyles].filename, *(attr + 1),
			sizeof(bstyles[0].filename) - 1);
	    if (!strcasecmp(*attr, "scalable"))
		if (!strcasecmp(*(attr + 1), "yes"))
		    bstyles[num_bstyles].flags |= 1;
	    attr += 2;
	}
	num_bstyles++;
	return;
    }

    if (!strcasecmp(el, "Scale")) { /* "Undocumented feature" */
	if (!*attr || strcasecmp(*attr, "value"))
	    warn("Invalid Scale");
	else
	    scale = atof(*(attr + 1));
	return;
    }

    if (!strcasecmp(el, "BallArea")) {
	current_group = ++num_groups;
	groups[current_group].type = TREASURE;
	groups[current_group].team = TEAM_NOT_SET;
	groups[current_group].hit_mask = BALL_BIT;
	return;
    }

    if (!strcasecmp(el, "BallTarget")) {
	int team;
	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    attr += 2;
	}
	current_group = ++num_groups;
	groups[current_group].type = TREASURE;
	groups[current_group].team = team;
	groups[current_group].hit_mask = NONBALL_BIT | (((NOTEAM_BIT << 1) - 1) & ~(1 << team));
	return;
    }

    if (!strcasecmp(el, "Decor")) {
	is_decor = 1;
	return;
    }

    if (!strcasecmp(el, "Polygon")) {
	int x, y, style = -1;
	poly_t t;

	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "style"))
		style = get_poly_id(*(attr + 1));
	    attr += 2;
	}
	if (x < 0 || x >= World.cwidth || y < 0 || y > World.cheight) {
	    warn("Polygon start point (%d, %d) is not inside the map"
		  "(0 <= x < %d, 0 <= y < %d)",
		  x, y, World.cwidth, World.cheight);
	    exit(1);
	}
	if (style == -1) {
	    warn("Currently you must give polygon style, no default");
	    exit(1);
	}
	ptscount = 0;
	t.x = x;
	t.y = y;
	t.group = current_group;
	t.edges = edges;
	t.style = style;
	t.estyles_start = ecount;
	t.is_decor = is_decor;
	current_estyle = pstyles[style].defedge_id;
	STORE(poly_t, pdata, polyc, max_polys, t);
	return;
    }

    if (!strcasecmp(el, "Check")) {
	ipos t;
	int x, y;

	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	t.x = x;
	t.y = y;
	STORE(ipos, World.check, World.NumChecks, max_checks, t);
	return;
    }

    if (!strcasecmp(el, "Fuel")) {
	fuel_t t;
	int team, x, y;

	team = TEAM_NOT_SET;
	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	t.clk_pos.x = x;
	t.clk_pos.y = y;
	t.fuel = START_STATION_FUEL;
	t.conn_mask = (unsigned)-1;
	t.last_change = frame_loops;
	t.team = team;
	STORE(fuel_t, World.fuel, World.NumFuels, max_fuels, t);
	return;
    }

    if (!strcasecmp(el, "Base")) {
	base_t	t;
	int	team, x, y, dir;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "dir"))
		dir = atoi(*(attr + 1));
	    attr += 2;
	}
	if (team < 0 || team >= MAX_TEAMS) {
	    warn("Illegal team number in base tag.\n");
	    exit(1);
	}

	t.pos.x = x;
	t.pos.y = y;
	t.dir = dir;
	if (BIT(World.rules->mode, TEAM_PLAY)) {
	    t.team = team;
	    World.teams[team].NumBases++;
	    if (World.teams[team].NumBases == 1)
		World.NumTeamBases++;
	} else {
	    t.team = TEAM_NOT_SET;
	}
	STORE(base_t, World.base, World.NumBases, max_bases, t);
	return;
    }

    if (!strcasecmp(el, "Ball")) {
    	treasure_t t;
	int team, x, y;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	t.pos.x = x;
	t.pos.y = y;
	t.have = false;
	t.destroyed = 0;
	t.team = team;
	t.empty = false; /* kps addition */
	World.teams[team].NumTreasures++;
	World.teams[team].TreasuresLeft++;
	STORE(treasure_t, World.treasures, World.NumTreasures, max_balls, t);
	return;
    }

    if (!strcasecmp(el, "Option")) {
	const char *name, *value;
	while (*attr) {
	    if (!strcasecmp(*attr, "name"))
		name = *(attr + 1);
	    if (!strcasecmp(*attr, "value"))
		value = *(attr + 1);
	    attr += 2;
	}

	/*addOption(name, value, 0, NULL, OPT_MAP);*/
	Option_set_value(name, value, 0, OPT_MAP);

	return;
    }

    if (!strcasecmp(el, "Offset")) {
	int x, y, edgestyle = -1;
	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		x = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		y = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "style"))
		edgestyle = get_edge_id(*(attr + 1));
	    attr += 2;
	}
	if (ABS(x) > 30000 || ABS(y) > 30000) {
	    warn("Offset component absolute value exceeds 30000 (x=%d, y=%d)",
		  x, y);
	    exit(1);
	}
	*edges++ = x;
	*edges++ = y;
	if (edgestyle != -1 && edgestyle != current_estyle) {
	    STORE(int, estyleptr, ecount, max_echanges, ptscount);
	    STORE(int, estyleptr, ecount, max_echanges, edgestyle);
	    current_estyle = edgestyle;
	}
	ptscount++;
	return;
    }

    if (!strcasecmp(el, "GeneralOptions"))
	return;

    warn("Unknown map tag: \"%s\"", el);
    return;
}


static void tagend(void *data, const char *el)
{
    void cmdhack(void);
    if (!strcasecmp(el, "Decor"))
	is_decor = 0;
    if (!strcasecmp(el, "BallArea") || !strcasecmp(el, "BallTarget"))
	current_group = 0;
    if (!strcasecmp(el, "Polygon")) {
	pdata[polyc - 1].num_points = ptscount;
	pdata[polyc - 1].num_echanges = ecount -pdata[polyc - 1].estyles_start;
	STORE(int, estyleptr, ecount, max_echanges, INT_MAX);
    }
    if (!strcasecmp(el, "GeneralOptions")) {
	/*cmdhack(); - kps ng wants this */ /* !@# */
	Options_parse();
	Grok_polygon_map();
    }
    return;
}

#if 0
/* kps - ng */
void cmdhack(void)
{
    int j;
    for (j = 0; j < NELEM(options); j++)
	addOption(options[j].name, options[j].defaultValue, 0, &options[j],
		  OPT_DEFAULT);
}
#endif


/* kps - ugly hack */
static bool isXp2MapFile(int fd)
{
    char start[] = "<XPilotMap";
    char buf[16];
    int n;

    n = read(fd, buf, sizeof(buf));
    if (n < 0) {
	error("Error reading map!");
	return false;
    }
    if (n == 0)
	return false;

    /* assume this works */
    (void)lseek(fd, 0, SEEK_SET);
    if (!strncmp(start, buf, strlen(start)))
	return true;
    return false;
}

static bool parseXp2MapFile(int fd, optOrigin opt_origin)
{
    char buff[8192];
    int len;
    XML_Parser p = XML_ParserCreate(NULL);

    if (!p) {
	warn("Creating Expat instance for map parsing failed.\n");
	/*exit(1);*/
	return false;
    }
    XML_SetElementHandler(p, tagstart, tagend);
    do {
	len = read(fd, buff, 8192);
	if (len < 0) {
	    error("Error reading map!");
	    return false;
	}
	if (!XML_Parse(p, buff, len, !len)) {
	    warn("Parse error reading map at line %d:\n%s\n",
		  XML_GetCurrentLineNumber(p),
		  XML_ErrorString(XML_GetErrorCode(p)));
	    /*exit(1);*/
	    return false;
	}
    } while (len);
    return true;
}
