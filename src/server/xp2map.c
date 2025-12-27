/* $Id: map.c,v 5.19 2002/01/18 22:34:26 kimiko Exp $
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

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
#include "bit.h"
#include "error.h"
#include "commonproto.h"

#include <expat.h>

char xp2map_version[] = VERSION;



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
	char id[100];
	int color = 0, texture_id = 0, defedge_id = 0, flags = 0;
	
	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strlcpy(id, *(attr + 1), sizeof(id));
	    if (!strcasecmp(*attr, "color"))
		color = strtol(*(attr + 1), NULL, 16);
	    if (!strcasecmp(*attr, "texture"))
		texture_id = P_get_bmp_id(*(attr + 1));
	    if (!strcasecmp(*attr, "defedge"))
		defedge_id = P_get_edge_id(*(attr + 1));
	    if (!strcasecmp(*attr, "flags"))
		flags = atoi(*(attr + 1)); /* names @!# */
	    attr += 2;
	}
	P_polystyle(id, color, texture_id, defedge_id, flags);
	return;
    }

    if (!strcasecmp(el, "Edgestyle")) {
	char id[100];
	int width = 0, color = 0, style = 0;

	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strlcpy(id, *(attr + 1), sizeof(estyles[0].id));
	    if (!strcasecmp(*attr, "width"))
		width = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "color"))
		color = strtol(*(attr + 1), NULL, 16);
	    if (!strcasecmp(*attr, "style")) /* !@# names later */
		style = atoi(*(attr + 1));
	    attr += 2;
	}
	P_edgestyle(id, width, color, style);
	return;
    }

    if (!strcasecmp(el, "Bmpstyle")) {
	char id[100];
	char filename[30];
	int flags = 0;

	/* add checks that these are filled !@# */
	while (*attr) {
	    if (!strcasecmp(*attr, "id"))
		strlcpy(id, *(attr + 1), sizeof(id));
	    if (!strcasecmp(*attr, "filename"))
		strlcpy(filename, *(attr + 1), sizeof(filename));
	    if (!strcasecmp(*attr, "scalable"))
		if (!strcasecmp(*(attr + 1), "yes"))
		    flags |= 1;
	    attr += 2;
	}
	P_bmpstyle(id, filename, flags);
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
	P_start_ballarea();
	return;
    }

    if (!strcasecmp(el, "BallTarget")) {
	int team = TEAM_NOT_SET;
	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    attr += 2;
	}
	P_start_balltarget(team);
	return;
    }

    if (!strcasecmp(el, "Decor")) {
	P_start_decor();
	return;
    }

    if (!strcasecmp(el, "Polygon")) {
	int cx = -1, cy = -1, style = -1;

	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "style"))
		style = P_get_poly_id(*(attr + 1));
	    attr += 2;
	}
	P_start_polygon(cx, cy, style);
	return;
    }

    if (!strcasecmp(el, "Check")) {
	int cx = -1, cy = -1;

	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	Map_place_checkpoint(cx, cy, -1);
	return;
    }

    if (!strcasecmp(el, "Fuel")) {
	int team = TEAM_NOT_SET, cx = -1, cy = -1;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	Map_place_fuel(cx, cy, team);
	return;
    }

    if (!strcasecmp(el, "Base")) {
	int team = TEAM_NOT_SET, cx = -1, cy = -1, dir = DIR_UP;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "dir"))
		dir = atoi(*(attr + 1));
	    attr += 2;
	}
	if (team < 0 || team >= MAX_TEAMS) {
	    warn("Illegal team number in base tag.\n");
	    exit(1);
	}
	Map_place_base(cx, cy, dir, team);
	return;
    }

    if (!strcasecmp(el, "Ball")) {
	int team = TEAM_NOT_SET, cx = -1, cy = -1;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	Map_place_treasure(cx, cy, team, false);
	return;
    }

    if (!strcasecmp(el, "Target")) {
	int team = TEAM_NOT_SET, cx = -1, cy = -1;

	while (*attr) {
	    if (!strcasecmp(*attr, "team"))
		team = atoi(*(attr + 1));
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    attr += 2;
	}
	Map_place_target(cx, cy, team);
	return;
    }

    if (!strcasecmp(el, "Option")) {
	const char *name = NULL, *value = NULL;
	while (*attr) {
	    if (!strcasecmp(*attr, "name"))
		name = *(attr + 1);
	    if (!strcasecmp(*attr, "value"))
		value = *(attr + 1);
	    attr += 2;
	}
	Option_set_value(name, value, 0, OPT_MAP);
	return;
    }

    if (!strcasecmp(el, "Offset")) {
	int cx = -1, cy = -1, edgestyle = -1;
	while (*attr) {
	    if (!strcasecmp(*attr, "x"))
		cx = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "y"))
		cy = atoi(*(attr + 1)) * scale;
	    if (!strcasecmp(*attr, "style"))
		edgestyle = P_get_edge_id(*(attr + 1));
	    attr += 2;
	}
	P_offset(cx, cy, edgestyle);
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
	P_end_decor();
    if (!strcasecmp(el, "BallArea"))
	P_end_ballarea();
    if (!strcasecmp(el, "BallTarget"))
	P_end_balltarget();
    if (!strcasecmp(el, "Polygon")) {
	P_end_polygon();
    }
    if (!strcasecmp(el, "GeneralOptions")) {
	/* ok, got to the end of options */
	Options_parse();
	Grok_map_new();
    }
    return;
}

/* kps - ugly hack */
bool isXp2MapFile(int fd)
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

bool parseXp2MapFile(int fd, optOrigin opt_origin)
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


