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
# include <sys/file.h>
#endif

#ifdef _WINDOWS
# include "NT/winServer.h"
#endif

#define SERVER
#include "version.h"
#include "config.h"
#include "serverconst.h"
#include "global.h"
#include "proto.h"
#include "map.h"
#include "bit.h"
#include "error.h"
#include "commonproto.h"

char map_version[] = VERSION;

#define GRAV_RANGE  10


#define STORE(T,P,N,M,V)						\
    if (N >= M && ((M <= 0)						\
	? (P = (T *) malloc((M = 1) * sizeof(*P)))			\
	: (P = (T *) realloc(P, (M += M) * sizeof(*P)))) == NULL) {	\
	warn("No memory");						\
	exit(1);							\
    } else								\
	(P[N++] = V)
/* !@# add a final realloc later to free wasted memory */
int max_asteroidconcs = 0, max_bases = 0, max_cannons = 0, max_checks = 0,
    max_fuels = 0, max_gravs = 0, max_itemconcs = 0,
    max_targets = 0, max_treasures = 0, max_wormholes = 0;

/*
 * Globals.
 */
World_map World;
bool is_polygon_map = FALSE;

static void Generate_random_map(void);

static void Find_base_order(void);

static void Reset_map_object_counters(void);

#ifdef DEBUG
static void Print_map(void)			/* Debugging only. */
{
    int x, y;

    if (is_polygon_map)
	return;

    for (y=World.y-1; y>=0; y--) {
	for (x=0; x<World.x; x++)
	    switch (World.block[x][y]) {
	    case SPACE:
		putchar(' ');
		break;
	    case BASE:
		putchar('_');
		break;
	    default:
		putchar('X');
		break;
	    }
	putchar('\n');
    }
}
#endif

#include <ctype.h>
void asciidump(void *p, size_t size)
{
    int i;
    unsigned char *up = p;
    char c;

    for (i = 0; i < size; i++) {
       if (!(i % 64))
           printf("\n%08x ", i);
       c = *(up + i);
       if (isprint(c))
           printf("%c", c);
       else
           printf(".");
    }
    printf("\n\n");
}


void hexdump(void *p, size_t size)
{
    int i;
    unsigned char *up = p;

    for (i = 0; i < size; i++) {
	if (!(i % 16))
	    printf("\n%08x ", i);
	printf("%02x ", *(up + i));
    }
    printf("\n\n");
}


void Map_place_cannon(int cx, int cy, int dir, int team)
{
    cannon_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    t.dir = dir;
    t.team = team;
    t.dead_time = 0;
    t.conn_mask = (unsigned)-1;
    STORE(cannon_t, World.cannon, World.NumCannons, max_cannons, t);
    Cannon_init(World.NumCannons - 1);
}

void Map_place_fuel(int cx, int cy, int team)
{
    fuel_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    t.fuel = START_STATION_FUEL;
    t.conn_mask = (unsigned)-1;
    t.last_change = frame_loops;
    t.team = team;
    STORE(fuel_t, World.fuel, World.NumFuels, max_fuels, t);
}

void Map_place_base(int cx, int cy, int dir, int team)
{
    base_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    /*
     * The direction of the base should be so that it points
     * up with respect to the gravity in the region.  This
     * is fixed in Find_base_dir() when the gravity has
     * been computed.
     */
    t.dir = dir;
    if (BIT(World.rules->mode, TEAM_PLAY)) {
	if (team < 0 || team >= MAX_TEAMS)
	    team = 0;
	t.team = team;
	World.teams[team].NumBases++;
	if (World.teams[team].NumBases == 1)
	    World.NumTeamBases++;
    } else {
	t.team = TEAM_NOT_SET;
    }
    STORE(base_t, World.base, World.NumBases, max_bases, t);
}

void Map_place_treasure(int cx, int cy, int team, bool empty)
{
    treasure_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    t.have = false;
    t.destroyed = 0;
    t.team = team;
    t.empty = empty;
    if (team != TEAM_NOT_SET) {
	World.teams[team].NumTreasures++;
	World.teams[team].TreasuresLeft++;
    }
    STORE(treasure_t, World.treasures, World.NumTreasures, max_treasures, t);
}

/*
 * note: for ng support, we need code in Polys_to_client in walls.c
 * and parse_new() in netclient.c
 */
void Map_place_target(int cx, int cy, int team)
{
    target_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    /*
     * Determining which team it belongs to is done later,
     * in Find_closest_team().
     */
    t.team = team;
    t.dead_time = 0;
    t.damage = TARGET_DAMAGE;
    t.conn_mask = (unsigned)-1;
    t.update_mask = 0;
    t.last_change = frame_loops;
    STORE(target_t, World.targets, World.NumTargets, max_targets, t);
}

void Map_place_wormhole(int cx, int cy, wormType type)
{
    wormhole_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    t.countdown = 0;
    t.lastdest = -1;
    t.temporary = 0;
    t.type = type;
    t.lastblock = SPACE;
    t.lastID = -1;
    STORE(wormhole_t, World.wormHoles, World.NumWormholes, max_wormholes, t);
}

/*
 * if 0 <= index < OLD_MAX_CHECKS, the checkpoint is directly inserted
 * into the check array and it is assumed it has been allocated earlier
 */
void Map_place_checkpoint(int cx, int cy, int index)
{
    cpos t;

    if (index >= 0 && index < OLD_MAX_CHECKS) {
	World.check[index].cx = cx;
	World.check[index].cy = cy;
	return;
    }

    t.cx = cx;
    t.cy = cy;
    STORE(cpos, World.check, World.NumChecks, max_checks, t);
}

void Map_place_item_concentrator(int cx, int cy)
{
    item_concentrator_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    STORE(item_concentrator_t, World.itemConcentrators,
	  World.NumItemConcentrators, max_itemconcs, t);
}

void Map_place_asteroid_concentrator(int cx, int cy)
{
    asteroid_concentrator_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    STORE(asteroid_concentrator_t, World.asteroidConcs,
	  World.NumAsteroidConcs, max_asteroidconcs, t);
}

void Map_place_grav(int cx, int cy, DFLOAT force)
{
    grav_t t;

    t.pos.cx = cx;
    t.pos.cy = cy;
    t.force = force;
    STORE(grav_t, World.grav, World.NumGravs, max_gravs, t);
}




static void Init_map(void)
{
    /*
     * note - when this is called, options have already been parsed,
     * so for exaple items are partially initialized
     */
    Reset_map_object_counters();
}

void Free_map(void)
{
    if (World.block) {
	free(World.block);
	World.block = NULL;
    }
    if (World.itemID) {
	free(World.itemID);
	World.itemID = NULL;
    }
    if (World.gravity) {
	free(World.gravity);
	World.gravity = NULL;
    }
    if (World.grav) {
	free(World.grav);
	World.grav = NULL;
    }
    if (World.base) {
	free(World.base);
	World.base = NULL;
    }
    if (World.cannon) {
	free(World.cannon);
	World.cannon = NULL;
    }
    if (World.check) {
	free(World.check);
	World.check = NULL;
    }
    if (World.fuel) {
	free(World.fuel);
	World.fuel = NULL;
    }
    if (World.wormHoles) {
	free(World.wormHoles);
	World.wormHoles = NULL;
    }
    if (World.itemConcentrators) {
	free(World.itemConcentrators);
	World.itemConcentrators = NULL;
    }
    if (World.asteroidConcs) {
	free(World.asteroidConcs);
	World.asteroidConcs = NULL;
    }
}


void Alloc_map(void)
{
    int x;

    if (World.block || World.gravity)
	Free_map();

    World.block =
	(unsigned char **)malloc(sizeof(unsigned char *)*World.x
				 + World.x*sizeof(unsigned char)*World.y);
    World.itemID =
	(unsigned short **)malloc(sizeof(unsigned short *)*World.x
				 + World.x*sizeof(unsigned short)*World.y);
    World.gravity =
	(vector **)malloc(sizeof(vector *)*World.x
			  + World.x*sizeof(vector)*World.y);
    World.grav = NULL;
    World.base = NULL;
    World.fuel = NULL;
    World.cannon = NULL;
    World.check = NULL;
    World.wormHoles = NULL;
    World.itemConcentrators = NULL;
    World.asteroidConcs = NULL;
    if (World.block == NULL || World.itemID == NULL || World.gravity == NULL) {
	Free_map();
	error("Couldn't allocate memory for map (%d bytes)",
	      World.x * (World.y * (sizeof(unsigned char) + sizeof(vector))
			 + sizeof(vector*)
			 + sizeof(unsigned char*)));
	exit(-1);
    } else {
	unsigned char *map_line;
	unsigned char **map_pointer;
	unsigned short *item_line;
	unsigned short **item_pointer;
	vector *grav_line;
	vector **grav_pointer;

	map_pointer = World.block;
	map_line = (unsigned char*) ((unsigned char**)map_pointer + World.x);
	item_pointer = World.itemID;
	item_line = (unsigned short*) ((unsigned short**)item_pointer + World.x);
	grav_pointer = World.gravity;
	grav_line = (vector*) ((vector**)grav_pointer + World.x);

	for (x=0; x<World.x; x++) {
	    *map_pointer = map_line;
	    map_pointer += 1;
	    map_line += World.y;
	    *item_pointer = item_line;
	    item_pointer += 1;
	    item_line += World.y;
	    *grav_pointer = grav_line;
	    grav_pointer += 1;
	    grav_line += World.y;
	}
    }
}

static void Map_extra_error(int line_num)
{
#ifndef SILENT
    static int prev_line_num, error_count;
    const int max_error = 5;

    if (line_num > prev_line_num) {
	prev_line_num = line_num;
	if (++error_count <= max_error) {
	    xpprintf("Map file contains extraneous characters on line %d\n",
		     line_num);
	}
	else if (error_count - max_error == 1) {
	    xpprintf("And so on...\n");
	}
    }
#endif
}


static void Map_missing_error(int line_num)
{
#ifndef SILENT
    static int prev_line_num, error_count;
    const int max_error = 5;

    if (line_num > prev_line_num) {
	prev_line_num = line_num;
	if (++error_count <= max_error) {
	    xpprintf("Not enough map data on map data line %d\n", line_num);
	}
	else if (error_count - max_error == 1) {
	    xpprintf("And so on...\n");
	}
    }
#endif
}

static bool Grok_map_old(void);

/*
 * This function can be called after the map options have been read.
 */
static bool Grok_map_size(void)
{
    bool bad = FALSE;

    if (!is_polygon_map) {
	mapWidth *= BLOCK_SZ;
	mapHeight *= BLOCK_SZ;
    }

    if (mapWidth < BLOCK_SZ) {
	warn("mapWidth too small, minimum is 1 block (%d pixels).\n",
	     BLOCK_SZ);
	bad = TRUE;
    }
    if (mapWidth > MAX_MAP_SIZE * BLOCK_SZ) {
	warn("mapWidth too big, maximum is %d blocks (%d pixels).\n",
	    MAX_MAP_SIZE, MAX_MAP_SIZE * BLOCK_SZ);
	bad = TRUE;
    }
    if (mapHeight < BLOCK_SZ) {
	warn("mapHeight too small, minimum is 1 block (%d pixels).\n",
	     BLOCK_SZ);
	bad = TRUE;
    }
    if (mapHeight > MAX_MAP_SIZE * BLOCK_SZ) {
	warn("mapWidth too big, maximum is %d blocks (%d pixels).\n",
	     MAX_MAP_SIZE, MAX_MAP_SIZE * BLOCK_SZ);
	bad = TRUE;
    }

    if (bad)
	return FALSE;

    /* pixel sizes */
    World.width = mapWidth;
    World.height = mapHeight;
    if (!is_polygon_map && extraBorder) {
	World.width += 2 * BLOCK_SZ;
	World.height += 2 * BLOCK_SZ;
    }
    World.hypotenuse = (int) LENGTH(World.width, World.height);

    /* click sizes */
    World.cwidth = World.width * CLICK;
    World.cheight = World.height * CLICK;

    /* block sizes */
    World.x = (World.width - 1) / BLOCK_SZ + 1; /* !@# */
    World.y = (World.height - 1) / BLOCK_SZ + 1;
    World.diagonal = (int) LENGTH(World.x, World.y);

    return TRUE;
}

bool Grok_map(void)
{
    if (!Grok_map_old())
	return FALSE;

    if (maxRobots == -1) {
	maxRobots = World.NumBases;
    }
    if (minRobots == -1) {
	minRobots = maxRobots;
    }
    if (BIT(World.rules->mode, TIMING)) {
	Find_base_order();
    }

#ifndef	SILENT
    xpprintf("World....: %s\nBases....: %d\nMapsize..: %dx%d pixels\n"
	     "Team play: %s\n", World.name, World.NumBases, World.width,
	     World.height, BIT(World.rules->mode, TEAM_PLAY) ? "on" : "off");
#endif

    D( Print_map() );

    return TRUE;
}

bool Grok_map_new(void)
{
    Init_map();

    if (!Grok_map_size())
	return FALSE;

    strlcpy(World.name, mapName, sizeof(World.name));
    strlcpy(World.author, mapAuthor, sizeof(World.author));
    strlcpy(World.dataURL, dataURL, sizeof(World.dataURL));

    Alloc_map();

    Set_world_rules();
    Set_world_items();

    if (BIT(World.rules->mode, TEAM_PLAY|TIMING) == (TEAM_PLAY|TIMING)) {
	error("Cannot teamplay while in race mode -- ignoring teamplay");
	CLR_BIT(World.rules->mode, TEAM_PLAY);
    }

    return TRUE;
}

/*
 * Grok block based map data.
 *
 * Create World.block using mapData.
 * Count objects on map.
 * Free mapData.
 */
static void Grok_map_data(void)
{
    int x, y, c;
    char *s;

    x = -1;
    y = World.y - 1;

    s = mapData;
    while (y >= 0) {

	x++;

	if (extraBorder && (x == 0 || x == World.x - 1
	    || y == 0 || y == World.y - 1)) {
	    if (x >= World.x) {
		x = -1;
		y--;
		continue;
	    } else {
		/* make extra border of solid rock */
		c = 'x';
	    }
	}
	else {
	    c = *s;
	    if (c == '\0' || c == EOF) {
		if (x < World.x) {
		    /* not enough map data on this line */
		    Map_missing_error(World.y - y);
		    c = ' ';
		} else {
		    c = '\n';
		}
	    } else {
		if (c == '\n' && x < World.x) {
		    /* not enough map data on this line */
		    Map_missing_error(World.y - y);
		    c = ' ';
		} else {
		    s++;
		}
	    }
	}
	if (x >= World.x || c == '\n') {
	    y--; x = -1;
	    if (c != '\n') {			/* Get rest of line */
		Map_extra_error(World.y - y);
		while (c != '\n' && c != EOF) {
		    c = *s++;
		}
	    }
	    continue;
	}

	switch (World.block[x][y] = c) {
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
	    if (BIT(World.rules->mode, TIMING))
		World.NumChecks++;
	    break;
	default:
	    break;
	}
    }

    free(mapData);
    mapData = NULL;
}


/*
 * Get space for special objects.
 *
 * Used for block based maps.
 */

void Allocate_map_objects(void)
{
    if ((World.check = (cpos *)
	    malloc(OLD_MAX_CHECKS * sizeof(cpos))) == NULL) {
	error("Out of memory - checks");
	exit(-1);
    }
#if 0
    /* kps - add the below warning somewhere else */
    if (World.NumBases > 0) {
	if ((World.base = (base_t *)
	    malloc(World.NumBases * sizeof(base_t))) == NULL) {
	    error("Out of memory - bases");
	    exit(-1);
	}
    } else {
	error("WARNING: map has no bases!");
    }
#endif
}

/* realloc hack */
static void shrink(void **pp, size_t size)
{
    void *p;

    p = realloc(*pp, size);
    if (!p) {
	warn("Out of memory");
	exit(1);
    }
    *pp = p;
}

#define SHRINK(P, N, T, M) { \
if ((M) > (N)) { \
  shrink((void **)&(P), (N) * sizeof(T)); \
  M = (N); \
} } \


static void Realloc_map_objects(void)
{
    SHRINK(World.cannon, World.NumCannons, cannon_t, max_cannons);
    SHRINK(World.fuel, World.NumFuels, fuel_t, max_fuels);
    SHRINK(World.grav, World.NumGravs, grav_t, max_gravs);
    SHRINK(World.wormHoles, World.NumWormholes, wormhole_t, max_wormholes);
    SHRINK(World.treasures, World.NumTreasures, treasure_t, max_treasures);
    SHRINK(World.targets, World.NumTargets, target_t, max_targets);
    SHRINK(World.base, World.NumBases, base_t, max_bases);
    SHRINK(World.itemConcentrators, World.NumItemConcentrators,
	   item_concentrator_t, max_itemconcs);
    SHRINK(World.asteroidConcs, World.NumAsteroidConcs,
	   asteroid_concentrator_t, max_asteroidconcs);
}


static void Reset_map_object_counters(void)
{
    int i;

    World.NumCannons = 0;
    World.NumFuels = 0;
    World.NumGravs = 0;
    World.NumWormholes = 0;
    World.NumTreasures = 0;
    World.NumTargets = 0;
    World.NumBases = 0;
    World.NumItemConcentrators = 0;
    World.NumAsteroidConcs = 0;

    for (i = 0; i < MAX_TEAMS; i++) {
	World.teams[i].NumMembers = 0;
	World.teams[i].NumRobots = 0;
	World.teams[i].NumBases = 0;
	World.teams[i].NumTreasures = 0;
	World.teams[i].NumEmptyTreasures = 0;
	World.teams[i].TreasuresDestroyed = 0;
	World.teams[i].TreasuresLeft = 0;
	World.teams[i].score = 0;
	World.teams[i].prev_score = 0;
	World.teams[i].SwapperId = NO_ID;
    }
}

static bool Grok_map_old(void)
{
    int i, x, y;

    if (is_polygon_map)
	return TRUE;

    Init_map();

    if (!Grok_map_size()) {
	if (mapData != NULL) {
	    free(mapData);
	    mapData = NULL;
	}
    }

    strlcpy(World.name, mapName, sizeof(World.name));
    strlcpy(World.author, mapAuthor, sizeof(World.author));

    if (!mapData) {
	errno = 0;
	error("Generating random map");
	Generate_random_map();
	if (!mapData) {
	    return FALSE;
	}
    }

    Alloc_map();

    Set_world_rules();
    Set_world_items();
    Set_world_asteroids();

    if (BIT(World.rules->mode, TEAM_PLAY|TIMING) == (TEAM_PLAY|TIMING)) {
	error("Cannot teamplay while in race mode -- ignoring teamplay");
	CLR_BIT(World.rules->mode, TEAM_PLAY);
    }

    Grok_map_data();

    Allocate_map_objects();

    /*
     * Now reset all counters since we will recount everything
     * and reuse these counters while inserting the objects
     * into structures.
     */
    Reset_map_object_counters();

    /*
     * Change read tags to internal data, create objects
     */
    {
	int	worm_in = 0,
		worm_out = 0,
		worm_norm = 0;
	wormType worm_type;

	for (x=0; x<World.x; x++) {
	    u_byte *line = World.block[x];
	    unsigned short *itemID = World.itemID[x];
	    int cx, cy;

	    cx = BLOCK_CENTER(x);

	    for (y=0; y<World.y; y++) {
		char c = line[y];

		cy = BLOCK_CENTER(y);
		itemID[y] = (unsigned short) -1;

		switch (c) {
		case ' ':
		case '.':
		default:
		    line[y] = SPACE;
		    break;

		case 'x':
		    line[y] = FILLED;
		    break;
		case 's':
		    line[y] = REC_LU;
		    break;
		case 'a':
		    line[y] = REC_RU;
		    break;
		case 'w':
		    line[y] = REC_LD;
		    break;
		case 'q':
		    line[y] = REC_RD;
		    break;

		case 'r':
		    line[y] = CANNON;
		    itemID[y] = World.NumCannons;
		    Map_place_cannon((x + 0.5) * BLOCK_CLICKS,
				     (y + 0.333) * BLOCK_CLICKS,
				     DIR_UP, TEAM_NOT_SET);
		    break;
		case 'd':
		    line[y] = CANNON;
		    itemID[y] = World.NumCannons;
		    Map_place_cannon((x + 0.667) * BLOCK_CLICKS,
				     (y + 0.5) * BLOCK_CLICKS,
				     DIR_LEFT, TEAM_NOT_SET);
		    break;
		case 'f':
		    line[y] = CANNON;
		    itemID[y] = World.NumCannons;
		    Map_place_cannon((x + 0.333) * BLOCK_CLICKS,
				     (y + 0.5) * BLOCK_CLICKS,
				     DIR_RIGHT, TEAM_NOT_SET);
		    break;
		case 'c':
		    line[y] = CANNON;
		    itemID[y] = World.NumCannons;
		    Map_place_cannon((x + 0.5) * BLOCK_CLICKS,
				     (y + 0.667) * BLOCK_CLICKS,
				     DIR_DOWN, TEAM_NOT_SET);
		    break;

		case '#':
		    line[y] = FUEL;
		    itemID[y] = World.NumFuels;
		    Map_place_fuel(cx, cy, TEAM_NOT_SET);
		    break;

		case '*':
		case '^':
		    line[y] = TREASURE;
		    itemID[y] = World.NumTreasures;
		    /*
		     * Determining which team it belongs to is done later,
		     * in Find_closest_team().
		     */
		    Map_place_treasure(cx, cy, TEAM_NOT_SET, (c == '^'));
		    break;
		case '!':
		    line[y] = TARGET;
		    itemID[y] = World.NumTargets;
		    /*
		     * Determining which team it belongs to is done later,
		     * in Find_closest_team().
		     */
		    Map_place_target(cx, cy, TEAM_NOT_SET);
		    break;
		case '%':
		    line[y] = ITEM_CONCENTRATOR;
		    itemID[y] = World.NumItemConcentrators;
		    Map_place_item_concentrator(cx, cy);
		    break;
		case '&':
		    line[y] = ASTEROID_CONCENTRATOR;
		    itemID[y] = World.NumAsteroidConcs;
		    Map_place_asteroid_concentrator(cx, cy);
		    break;
		case '$':
		    line[y] = BASE_ATTRACTOR;
		    break;
		case '_':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		    line[y] = BASE;
		    itemID[y] = World.NumBases;
		    /*
		     * The direction of the base should be so that it points
		     * up with respect to the gravity in the region.  This
		     * is fixed in Find_base_dir() when the gravity has
		     * been computed.
		     */
		    Map_place_base(cx, cy, DIR_UP, (int) (c - '0'));
		    break;

		case '+':
		    line[y] = POS_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, -GRAVS_POWER);
		    break;
		case '-':
		    line[y] = NEG_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, GRAVS_POWER);
		    break;
		case '>':
		    line[y]= CWISE_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, GRAVS_POWER);
		    break;
		case '<':
		    line[y] = ACWISE_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, -GRAVS_POWER);
		    break;
	        case 'i':
		    line[y] = UP_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, GRAVS_POWER);
		    break;
	        case 'm':
		    line[y] = DOWN_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, -GRAVS_POWER);
		    break;
	        case 'k':
		    line[y] = RIGHT_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, GRAVS_POWER);
		    break;
                case 'j':
		    line[y] = LEFT_GRAV;
		    itemID[y] = World.NumGravs;
		    Map_place_grav(cx, cy, -GRAVS_POWER);
		    break;

		case '@':
		case '(':
		case ')':
		    line[y] = WORMHOLE;
		    itemID[y] = World.NumWormholes;
		    if (c == '@') {
			worm_type = WORM_NORMAL;
			worm_norm++;
		    } else if (c == '(') {
			worm_type = WORM_IN;
			worm_in++;
		    } else {
			worm_type = WORM_OUT;
			worm_out++;
		    }
			
		    Map_place_wormhole(cx, cy, worm_type);
		    break;

		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
		case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
		case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
		case 'Y': case 'Z':
		    if (BIT(World.rules->mode, TIMING)) {
			Map_place_checkpoint(cx, cy, (int)(c-'A'));
			line[y] = CHECK;
		    } else {
			line[y] = SPACE;
		    }
		    break;

		case 'z':
		    line[y] = FRICTION;
		    break;

		case 'b':
		    line[y] = DECOR_FILLED;
		    break;
		case 'h':
		    line[y] = DECOR_LU;
		    break;
		case 'g':
		    line[y] = DECOR_RU;
		    break;
		case 'y':
		    line[y] = DECOR_LD;
		    break;
		case 't':
		    line[y] = DECOR_RD;
		    break;
		}
	    }
	}

	/*
	 * Verify that the wormholes are consistent, i.e. that if
	 * we have no 'out' wormholes, make sure that we don't have
	 * any 'in' wormholes, and (less critical) if we have no 'in'
	 * wormholes, make sure that we don't have any 'out' wormholes.
	 */
	if ((worm_norm) ? (worm_norm + worm_out < 2)
	    : (worm_in) ? (worm_out < 1)
	    : (worm_out > 0)) {

	    int i;

	    xpprintf("Inconsistent use of wormholes, removing them.\n");
	    for (i = 0; i < World.NumWormholes; i++) {
		int bx, by;
		
		bx = CLICK_TO_BLOCK(World.wormHoles[i].pos.cx);
		by = CLICK_TO_BLOCK(World.wormHoles[i].pos.cy);
		World.block[bx][by] = SPACE;
		World.itemID[bx][by] = (unsigned short) -1;
	    }
	    World.NumWormholes = 0;
	}

	if (!wormTime) {
	    for (i = 0; i < World.NumWormholes; i++) {
		int j = (int)(rfrac() * World.NumWormholes);
		while (World.wormHoles[j].type == WORM_IN)
		    j = (int)(rfrac() * World.NumWormholes);
		World.wormHoles[i].lastdest = j;
	    }
	}

	if (BIT(World.rules->mode, TIMING) && World.NumChecks == 0) {
	    xpprintf("No checkpoints found while race mode (timing) was set.\n");
	    xpprintf("Turning off race mode.\n");
	    CLR_BIT(World.rules->mode, TIMING);
	}

	/*
	 * Determine which team a treasure belongs to.
	 */
	if (BIT(World.rules->mode, TEAM_PLAY)) {
	    unsigned short team = TEAM_NOT_SET;
	    for (i = 0; i < World.NumTreasures; i++) {
		team = Find_closest_team(World.treasures[i].pos.cx,
					 World.treasures[i].pos.cy);
		World.treasures[i].team = team;
		if (team == TEAM_NOT_SET) {
		    error("Couldn't find a matching team for the treasure.");
		} else {
		    World.teams[team].NumTreasures++;
		    if (!World.treasures[i].empty) {
			World.teams[team].TreasuresLeft++;
		    } else {
			World.teams[team].NumEmptyTreasures++;
		    }
		}
	    }
	    for (i = 0; i < World.NumTargets; i++) {
		team = Find_closest_team(World.targets[i].pos.cx,
					 World.targets[i].pos.cy);
		if (team == TEAM_NOT_SET) {
		    error("Couldn't find a matching team for the target.");
		}
		World.targets[i].team = team;
	    }
	    if (teamCannons) {
		for (i = 0; i < World.NumCannons; i++) {
		    team = Find_closest_team(World.cannon[i].pos.cx,
					     World.cannon[i].pos.cy);
		    if (team == TEAM_NOT_SET) {
			error("Couldn't find a matching team for the cannon.");
		    }
		    World.cannon[i].team = team;
		}
	    }

	    for (i = 0; i < World.NumFuels; i++) {
		team = Find_closest_team(World.fuel[i].pos.cx,
					 World.fuel[i].pos.cy);
		if (team == TEAM_NOT_SET) {
		    error("Couldn't find a matching team for fuelstation.");
		}
		World.fuel[i].team = team;
	    }
	}
    }

    Realloc_map_objects();

    return TRUE;
}


/*
 * Use wildmap to generate a random map.
 */
/* kps - we need a poly random map generator */

static void Generate_random_map(void)
{
    edgeWrap = TRUE;
    mapWidth = 150;
    mapHeight = 150;

    Wildmap(mapWidth, mapHeight, World.name, World.author, &mapData,
	    &mapWidth, &mapHeight);
    Grok_map_size();
}


/*
 * Find the correct direction of the base, according to the gravity in
 * the base region.
 *
 * If a base attractor is adjacent to a base then the base will point
 * to the attractor.
 */
/* kps - ng does not want this */
void Find_base_direction(void)
{
    int	i;

    for (i = 0; i < World.NumBases; i++) {
	int	x = World.base[i].pos.cx / BLOCK_CLICKS,
		y = World.base[i].pos.cy / BLOCK_CLICKS,
		dir,
		att;
	double	dx = World.gravity[x][y].x,
		dy = World.gravity[x][y].y;

	if (dx == 0.0 && dy == 0.0) {	/* Undefined direction? */
	    dir = DIR_UP;	/* Should be set to direction of gravity! */
	} else {
	    dir = (int)findDir(-dx, -dy);
	    dir = ((dir + RES/8) / (RES/4)) * (RES/4);	/* round it */
	    dir = MOD2(dir, RES);
	}
	att = -1;
	/*BASES SNAP TO UPWARDS ATTRACTOR FIRST*/
        if (y == World.y - 1 && World.block[x][0] == BASE_ATTRACTOR
	    && BIT(World.rules->mode, WRAP_PLAY)) {  /*check wrapped*/
	    if (att == -1 || dir == DIR_UP) {
		att = DIR_UP;
	    }
	}
	if (y < World.y - 1 && World.block[x][y + 1] == BASE_ATTRACTOR) {
	    if (att == -1 || dir == DIR_UP) {
		att = DIR_UP;
	    }
	}
	/*THEN DOWNWARDS ATTRACTORS*/
        if (y == 0 && World.block[x][World.y-1] == BASE_ATTRACTOR
	    && BIT(World.rules->mode, WRAP_PLAY)) { /*check wrapped*/
	    if (att == -1 || dir == DIR_DOWN) {
		att = DIR_DOWN;
	    }
	}
	if (y > 0 && World.block[x][y - 1] == BASE_ATTRACTOR) {
	    if (att == -1 || dir == DIR_DOWN) {
		att = DIR_DOWN;
	    }
	}
	/*THEN RIGHTWARDS ATTRACTORS*/
	if (x == World.x - 1 && World.block[0][y] == BASE_ATTRACTOR
	    && BIT(World.rules->mode, WRAP_PLAY)) { /*check wrapped*/
	    if (att == -1 || dir == DIR_RIGHT) {
		att = DIR_RIGHT;
	    }
	}
	if (x < World.x - 1 && World.block[x + 1][y] == BASE_ATTRACTOR) {
	    if (att == -1 || dir == DIR_RIGHT) {
		att = DIR_RIGHT;
	    }
	}
	/*THEN LEFTWARDS ATTRACTORS*/
	if (x == 0 && World.block[World.x-1][y] == BASE_ATTRACTOR
	    && BIT(World.rules->mode, WRAP_PLAY)) { /*check wrapped*/
	    if (att == -1 || dir == DIR_LEFT) {
		att = DIR_LEFT;
	    }
	}
	if (x > 0 && World.block[x - 1][y] == BASE_ATTRACTOR) {
	    if (att == -1 || dir == DIR_LEFT) {
		att = DIR_LEFT;
	    }
	}
	if (att != -1) {
	    dir = att;
	}
	World.base[i].dir = dir;
    }
    for (i = 0; i < World.x; i++) {
	int j;
	for (j = 0; j < World.y; j++) {
	    if (World.block[i][j] == BASE_ATTRACTOR) {
		World.block[i][j] = SPACE;
	    }
	}
    }
}


/*
 * Return the team that is closest to this click position.
 */
unsigned short Find_closest_team(int cx, int cy)
{
    unsigned short team = TEAM_NOT_SET;
    int i;
    DFLOAT closest = FLT_MAX, l;

    for (i = 0; i < World.NumBases; i++) {
	if (World.base[i].team == TEAM_NOT_SET)
	    continue;

	l = Wrap_length((cx - World.base[i].pos.cx),
			(cy - World.base[i].pos.cy));

	if (l < closest) {
	    team = World.base[i].team;
	    closest = l;
	}
    }

    return team;
}


/*
 * Determine the order in which players are placed
 * on starting positions after race mode reset.
 */
/* kps - ng does not want this */
static void Find_base_order(void)
{
    int			i, j, k, n;
    DFLOAT		dist;
    int			cx, cy;

    if (!BIT(World.rules->mode, TIMING)) {
	World.baseorder = NULL;
	return;
    }
    if ((n = World.NumBases) <= 0) {
	error("Cannot support race mode in a map without bases");
	exit(-1);
    }

    if ((World.baseorder = (baseorder_t *)
	    malloc(n * sizeof(baseorder_t))) == NULL) {
	error("Out of memory - baseorder");
	exit(-1);
    }

    cx = World.check[0].cx;
    cy = World.check[0].cy;
    for (i = 0; i < n; i++) {
	dist = Wrap_length(World.base[i].pos.cx - cx,
			   World.base[i].pos.cy - cy) / CLICK;
	for (j = 0; j < i; j++) {
	    if (World.baseorder[j].dist > dist) {
		break;
	    }
	}
	for (k = i - 1; k >= j; k--) {
	    World.baseorder[k + 1] = World.baseorder[k];
	}
	World.baseorder[j].base_idx = i;
	World.baseorder[j].dist = dist;
    }
}


DFLOAT Wrap_findDir(DFLOAT dx, DFLOAT dy)
{
    dx = WRAP_DX(dx);
    dy = WRAP_DY(dy);
    return findDir(dx, dy);
}

DFLOAT Wrap_cfindDir(int dcx, int dcy)
{
    dcx = WRAP_DCX(dcx);
    dcy = WRAP_DCY(dcy);
    return findDir(dcx, dcy);
}

DFLOAT Wrap_length(int dcx, int dcy)
{
    dcx = WRAP_DCX(dcx);
    dcy = WRAP_DCY(dcy);
    return LENGTH(dcx, dcy);
}


static void Compute_global_gravity(void)
{
    int			xi, yi, dx, dy;
    DFLOAT		xforce, yforce, strength;
    double		theta;
    vector		*grav;


    if (gravityPointSource == false) {
	theta = (gravityAngle * PI) / 180.0;
	xforce = cos(theta) * Gravity;
	yforce = sin(theta) * Gravity;
	for (xi=0; xi<World.x; xi++) {
	    grav = World.gravity[xi];

	    for (yi=0; yi<World.y; yi++, grav++) {
		grav->x = xforce;
		grav->y = yforce;
	    }
	}
    } else {
	for (xi=0; xi<World.x; xi++) {
	    grav = World.gravity[xi];
	    dx = (xi - gravityPoint.x) * BLOCK_SZ;
	    dx = WRAP_DX(dx);

	    for (yi=0; yi<World.y; yi++, grav++) {
		dy = (yi - gravityPoint.y) * BLOCK_SZ;
		dy = WRAP_DX(dy);

		if (dx == 0 && dy == 0) {
		    grav->x = 0.0;
		    grav->y = 0.0;
		    continue;
		}
		strength = Gravity / LENGTH(dx, dy);
		if (gravityClockwise) {
		    grav->x =  dy * strength;
		    grav->y = -dx * strength;
		}
		else if (gravityAnticlockwise) {
		    grav->x = -dy * strength;
		    grav->y =  dx * strength;
		}
		else {
		    grav->x =  dx * strength;
		    grav->y =  dy * strength;
		}
	    }
	}
    }
}


static void Compute_grav_tab(vector grav_tab[GRAV_RANGE+1][GRAV_RANGE+1])
{
    int			x, y;
    double		strength;

    grav_tab[0][0].x = grav_tab[0][0].y = 0;
    for (x = 0; x < GRAV_RANGE+1; x++) {
	for (y = (x == 0); y < GRAV_RANGE+1; y++) {
	    strength = pow((double)(sqr(x) + sqr(y)), -1.5);
	    grav_tab[x][y].x = x * strength;
	    grav_tab[x][y].y = y * strength;
	}
    }
}


static void Compute_local_gravity(void)
{
    int			xi, yi, g, gx, gy, ax, ay, dx, dy, gtype;
    int			first_xi, last_xi, first_yi, last_yi, mod_xi, mod_yi;
    int			min_xi, max_xi, min_yi, max_yi;
    DFLOAT		force, fx, fy;
    vector		*v, *grav, *tab, grav_tab[GRAV_RANGE+1][GRAV_RANGE+1];


    Compute_grav_tab(grav_tab);

    min_xi = 0;
    max_xi = World.x - 1;
    min_yi = 0;
    max_yi = World.y - 1;
    if (BIT(World.rules->mode, WRAP_PLAY)) {
	min_xi -= MIN(GRAV_RANGE, World.x);
	max_xi += MIN(GRAV_RANGE, World.x);
	min_yi -= MIN(GRAV_RANGE, World.y);
	max_yi += MIN(GRAV_RANGE, World.y);
    }
    for (g=0; g<World.NumGravs; g++) {
	gx = CLICK_TO_BLOCK(World.grav[g].pos.cx);
	gy = CLICK_TO_BLOCK(World.grav[g].pos.cy);
	force = World.grav[g].force;

	if ((first_xi = gx - GRAV_RANGE) < min_xi) {
	    first_xi = min_xi;
	}
	if ((last_xi = gx + GRAV_RANGE) > max_xi) {
	    last_xi = max_xi;
	}
	if ((first_yi = gy - GRAV_RANGE) < min_yi) {
	    first_yi = min_yi;
	}
	if ((last_yi = gy + GRAV_RANGE) > max_yi) {
	    last_yi = max_yi;
	}

	/* kps - ng does not want this */
	gtype = World.block[gx][gy];

	mod_xi = (first_xi < 0) ? (first_xi + World.x) : first_xi;
	dx = gx - first_xi;
	fx = force;
	for (xi = first_xi; xi <= last_xi; xi++, dx--) {
	    if (dx < 0) {
		fx = -force;
		ax = -dx;
	    } else {
		ax = dx;
	    }
	    mod_yi = (first_yi < 0) ? (first_yi + World.y) : first_yi;
	    dy = gy - first_yi;
	    grav = &World.gravity[mod_xi][mod_yi];
	    tab = grav_tab[ax];
	    fy = force;
	    for (yi = first_yi; yi <= last_yi; yi++, dy--) {
		if (dx || dy) {
		    if (dy < 0) {
			fy = -force;
			ay = -dy;
		    } else {
			ay = dy;
		    }
		    v = &tab[ay];
		    if (gtype == CWISE_GRAV || gtype == ACWISE_GRAV) {
			grav->x -= fy * v->y;
			grav->y += fx * v->x;
		    } else if (gtype == UP_GRAV || gtype == DOWN_GRAV) {
			grav->y += force * v->x;
		    } else if (gtype == RIGHT_GRAV || gtype == LEFT_GRAV) {
			grav->x += force * v->y;
		    } else {
			grav->x += fx * v->x;
			grav->y += fy * v->y;
		    }
		}
		else {
		    if (gtype == UP_GRAV || gtype == DOWN_GRAV) {
			grav->y += force;
		    }
		    else if (gtype == LEFT_GRAV || gtype == RIGHT_GRAV) {
			grav->x += force;
		    }
		}
		mod_yi++;
		grav++;
		if (mod_yi >= World.y) {
		    mod_yi = 0;
		    grav = World.gravity[mod_xi];
		}
	    }
	    if (++mod_xi >= World.x) {
		mod_xi = 0;
	    }
	}
    }
    /*
     * We may want to free the World.gravity memory here
     * as it is not used anywhere else.
     * e.g.: free(World.gravity);
     *       World.gravity = NULL;
     *       World.NumGravs = 0;
     * Some of the more modern maps have quite a few gravity symbols.
     */
}


void Compute_gravity(void)
{
    Compute_global_gravity();
    Compute_local_gravity();
}


/* kps - ng does not want this */
void add_temp_wormholes(int xin, int yin, int xout, int yout)
{
    wormhole_t inhole, outhole, *wwhtemp;

    if ((wwhtemp = (wormhole_t *)realloc(World.wormHoles,
					 (World.NumWormholes + 2)
					 * sizeof(wormhole_t)))
	== NULL) {
	error("No memory for temporary wormholes.");
	return;
    }
    World.wormHoles = wwhtemp;

    inhole.pos.cx = BLOCK_CENTER(xin);
    inhole.pos.cy = BLOCK_CENTER(yin);
    outhole.pos.cx = BLOCK_CENTER(xout);
    outhole.pos.cy = BLOCK_CENTER(yout);
    inhole.countdown = outhole.countdown = wormTime;
    inhole.lastdest = World.NumWormholes + 1;
    inhole.temporary = outhole.temporary = 1;
    inhole.type = WORM_IN;
    outhole.type = WORM_OUT;
    inhole.lastblock = World.block[xin][yin];
    outhole.lastblock = World.block[xout][yout];
    inhole.lastID = World.itemID[xin][yin];
    outhole.lastID = World.itemID[xout][yout];
    World.wormHoles[World.NumWormholes] = inhole;
    World.wormHoles[World.NumWormholes + 1] = outhole;
    World.block[xin][yin] = World.block[xout][yout] = WORMHOLE;
    World.itemID[xin][yin] = World.NumWormholes;
    World.itemID[xout][yout] = World.NumWormholes + 1;
    World.NumWormholes += 2;
}

/* kps - ng does not want this */
void remove_temp_wormhole(int ind)
{
    wormhole_t hole;
    int bx, by;

    hole = World.wormHoles[ind];
    bx = CLICK_TO_BLOCK(hole.pos.cx);
    by = CLICK_TO_BLOCK(hole.pos.cy);
    World.block[bx][by] = hole.lastblock;
    World.itemID[bx][by] = hole.lastID;
    World.NumWormholes--;
    if (ind != World.NumWormholes) {
	World.wormHoles[ind] = World.wormHoles[World.NumWormholes];
    }
    World.wormHoles = (wormhole_t *)realloc(World.wormHoles,
					    World.NumWormholes
					    * sizeof(wormhole_t));
}

