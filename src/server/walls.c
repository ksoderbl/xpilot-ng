/* $Id: walls.c,v 5.27 2002/04/21 09:31:18 bertg Exp $
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
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <time.h>

#ifndef _WINDOWS
# include <sys/types.h>  /* freebsd for in.h to work */
# include <sys/socket.h> /* freebsd for in.h to work */
# include <netinet/in.h>
#endif

#ifdef _WINDOWS
# include "NT/winServer.h"
# include "../common/NT/winNet.h"
#endif

#define SERVER
#include "version.h"
#include "config.h"
#include "serverconst.h"
#include "global.h"
#include "proto.h"
#include "score.h"
#include "saudio.h"
#include "item.h"
#include "error.h"
#include "walls.h"
#include "click.h"
#include "objpos.h"

#include "const.h"
#include "map.h"
#include "netserver.h"
#include "pack.h"
#include "srecord.h"

char walls_version[] = VERSION;

#define WALLDIST_MASK	\
	(FILLED_BIT | REC_LU_BIT | REC_LD_BIT | REC_RU_BIT | REC_RD_BIT \
	| FUEL_BIT | CANNON_BIT | TREASURE_BIT | TARGET_BIT \
	| CHECK_BIT | WORMHOLE_BIT)

unsigned SPACE_BLOCKS = ( 
	SPACE_BIT | BASE_BIT | WORMHOLE_BIT | 
	POS_GRAV_BIT | NEG_GRAV_BIT | CWISE_GRAV_BIT | ACWISE_GRAV_BIT | 
	UP_GRAV_BIT | DOWN_GRAV_BIT | RIGHT_GRAV_BIT | LEFT_GRAV_BIT | 
	DECOR_LU_BIT | DECOR_LD_BIT | DECOR_RU_BIT | DECOR_RD_BIT | 
	DECOR_FILLED_BIT | CHECK_BIT | ITEM_CONCENTRATOR_BIT |
	FRICTION_BIT | ASTEROID_CONCENTRATOR_BIT
    );

static struct move_parameters mp;
static DFLOAT wallBounceExplosionMult;
static char msg[MSG_LEN];

/*
 * Two dimensional array giving for each point the distance
 * to the nearest wall.  Measured in blocks times 2.
 */
static unsigned char **walldist;

/* kps compatibility hacks - plz remove if you can */
static void Walls_init_old(void);
static void Walls_init_new(void);
static void Move_object_old(object *obj);
static void Move_object_new(object *obj);
static void Move_player_old(int ind);
static void Move_player_new(int ind);
static void Turn_player_old(int ind);
static void Turn_player_new(int ind);


/* polygon map related stuff */

/* start */

/* Maximum line length 32767-B_CLICKS, 30000 used in checks
   There's a minimum map size to avoid "too much wrapping". A bit smaller
   than that would cause rare errors for fast-moving things. I haven't
   bothered to figure out what the limit is. 80k x 80k clicks should
   be more than enough (probably...). */
#define B_SHIFT 11
#define B_CLICKS (1 << B_SHIFT)
#define B_MASK (B_CLICKS - 1)
#define CUTOFF (2 * BLOCK_CLICKS) /* Not sure about the optimum value */
#define MAX_MOVE 32000
#define SEPARATION_DIST 64
/* This must be increased if the ship corners are allowed to go farther
   when turning! */
#define MAX_SHAPE_OFFSET (15 * CLICK)

#if ((-3) / 2 != -1) || ((-3) % 2 != -1)
#error "This code assumes that negative numbers round upwards."
#endif

struct collans {
    int line;
    int point;
    clvec moved;
};

struct tl2 {
    int base;
    int x;
    int y;
};

struct move {
    clvec start;
    clvec delta;
    int hit_mask;
};

struct bline {
    clvec start;
    clvec delta;
    DFLOAT c;
    DFLOAT s;
    short group;
};

struct blockinfo {
    unsigned short distance;
    unsigned short *lines;
    unsigned short *points;
};

struct inside_block {
    short *y;
    short *lines;
    struct inside_block *next;
    short group;
    char base_value;
};

struct inside_block *inside_table;

struct test {
    double distance;
    int inside;
    struct tempy *y;
    struct templine *lines;
};

struct test *temparray;

shipobj ball_wire;

#define LINEY(X, Y, BASE, ARG)  (((Y)*(ARG)+(BASE))/(X))
#define SIDE(X, Y, LINE) (linet[(LINE)].delta.cy * (X) - linet[(LINE)].delta.cx * (Y))
#define SIGN(X) ((X) >= 0 ? 1 : -1)

struct bline *linet;
#define S_LINES 100 /* stupid hack */

/* kps - dynamic creation of groups asap! */
struct group groups[1000] = { /* !@# */
    {0, 0},
    {0, 0},
    {0, 0}};

struct blockinfo *blockline;

unsigned short *llist;

unsigned short *plist;

int linec = 0;

int num_polys = 0;

int num_groups = 0;

int mapx, mapy;


void Walls_init(void)
{
    /*
     * Always do the walls_init_new(), since we treat the
     * block map as a polygon map.
     */
    if (!is_polygon_map)
	Walls_init_old();
    Walls_init_new();
}

void Move_init(void)
{
    mp.click_width = PIXEL_TO_CLICK(World.width);
    mp.click_height = PIXEL_TO_CLICK(World.height);

    LIMIT(maxObjectWallBounceSpeed, 0, World.hypotenuse);
    LIMIT(maxShieldedWallBounceSpeed, 0, World.hypotenuse);
    LIMIT(maxUnshieldedWallBounceSpeed, 0, World.hypotenuse);

    /* kps - ng does not want the following 2 */
    LIMIT(maxShieldedWallBounceAngle, 0, 180);
    LIMIT(maxUnshieldedWallBounceAngle, 0, 180);

    LIMIT(playerWallBrakeFactor, 0, 1);
    LIMIT(objectWallBrakeFactor, 0, 1);
    LIMIT(objectWallBounceLifeFactor, 0, 1);
    LIMIT(wallBounceFuelDrainMult, 0, 1000);
    wallBounceExplosionMult = sqrt(wallBounceFuelDrainMult);

    /* kps - ng does not want the following 2 */
    mp.max_shielded_angle = (int)(maxShieldedWallBounceAngle * RES / 360);
    mp.max_unshielded_angle = (int)(maxUnshieldedWallBounceAngle * RES / 360);

    mp.obj_bounce_mask = 0;
    if (sparksWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_SPARK);
    }
    if (debrisWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_DEBRIS);
    }
    if (shotsWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_SHOT|OBJ_CANNON_SHOT);
    }
    if (itemsWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_ITEM);
    }
    if (missilesWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_SMART_SHOT|OBJ_TORPEDO|OBJ_HEAT_SHOT);
    }
    if (minesWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_MINE);
    }
    if (ballsWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_BALL);
    }
    if (asteroidsWallBounce) {
	SET_BIT(mp.obj_bounce_mask, OBJ_ASTEROID);
    }

    mp.obj_cannon_mask = (KILLING_SHOTS) | OBJ_MINE | OBJ_SHOT | OBJ_PULSE |
			OBJ_SMART_SHOT | OBJ_TORPEDO | OBJ_HEAT_SHOT |
			OBJ_ASTEROID;
    if (cannonsUseItems)
	mp.obj_cannon_mask |= OBJ_ITEM;
    mp.obj_target_mask = mp.obj_cannon_mask | OBJ_BALL | OBJ_SPARK;
    mp.obj_treasure_mask = mp.obj_bounce_mask | OBJ_BALL | OBJ_PULSE;
}

void Move_object(object *obj)
{
    if (is_polygon_map || !useOldCode)
	Move_object_new(obj);
    else
	Move_object_old(obj);
}

void Move_player(int ind)
{
    if (is_polygon_map || !useOldCode)
	Move_player_new(ind);
    else
	Move_player_old(ind);
}

void Turn_player(int ind)
{
    if (is_polygon_map || !useOldCode)
	Turn_player_new(ind);
    else
	Turn_player_old(ind);
}




/*
 * Allocate memory for the two dimensional "walldist" array.
 */
static void Walldist_alloc(void)
{
    int			x;
    unsigned char	*wall_line;
    unsigned char	**wall_ptr;

    walldist = (unsigned char **)malloc(
		World.x * sizeof(unsigned char *) + World.x * World.y);
    if (!walldist) {
	error("No memory for walldist");
	exit(1);
    }
    wall_ptr = walldist;
    wall_line = (unsigned char *)(wall_ptr + World.x);
    for (x = 0; x < World.x; x++) {
	*wall_ptr = wall_line;
	wall_ptr += 1;
	wall_line += World.y;
    }
}

/*
 * Dump the "walldist" array to file as a Portable PixMap.
 * Mainly used for debugging purposes.
 */
static void Walldist_dump(void)
{
#ifdef DEVELOPMENT
    char		name[1024];
    FILE		*fp;
    int			x, y;
    unsigned char	*line;

    if (!getenv("WALLDISTDUMP")) {
	return;
    }

    sprintf(name, "walldist.ppm");
    fp = fopen(name, "w");
    if (!fp) {
	error("%s", name);
	return;
    }
    line = (unsigned char *)malloc(3 * World.x);
    if (!line) {
	error("No memory for walldist dump");
	fclose(fp);
	return;
    }
    fprintf(fp, "P6\n");
    fprintf(fp, "%d %d\n", World.x, World.y);
    fprintf(fp, "%d\n", 255);
    for (y = World.y - 1; y >= 0; y--) {
	for (x = 0; x < World.x; x++) {
	    if (walldist[x][y] == 0) {
		line[x * 3 + 0] = 255;
		line[x * 3 + 1] = 0;
		line[x * 3 + 2] = 0;
	    }
	    else if (walldist[x][y] == 2) {
		line[x * 3 + 0] = 0;
		line[x * 3 + 1] = 255;
		line[x * 3 + 2] = 0;
	    }
	    else if (walldist[x][y] == 3) {
		line[x * 3 + 0] = 0;
		line[x * 3 + 1] = 0;
		line[x * 3 + 2] = 255;
	    }
	    else {
		line[x * 3 + 0] = walldist[x][y];
		line[x * 3 + 1] = walldist[x][y];
		line[x * 3 + 2] = walldist[x][y];
	    }
	}
	fwrite(line, World.x, 3, fp);
    }
    free(line);
    fclose(fp);

    printf("Walldist dumped to %s\n", name);
#endif
}

static void Walldist_init(void)
{
    int			x, y, dx, dy, wx, wy;
    int			dist;
    int			mindist;
    int			maxdist = 2 * MIN(World.x, World.y);
    int			newdist;

    typedef struct Qelmt { short x, y; } Qelmt_t;
    Qelmt_t		*q;
    int			qfront = 0, qback = 0;

    if (maxdist > 255) {
	maxdist = 255;
    }
    q = (Qelmt_t *)malloc(World.x * World.y * sizeof(Qelmt_t));
    if (!q) {
	error("No memory for walldist init");
	exit(1);
    }
    for (x = 0; x < World.x; x++) {
	for (y = 0; y < World.y; y++) {
	    if (BIT((1 << World.block[x][y]), WALLDIST_MASK)
		&& (World.block[x][y] != WORMHOLE
		    || World.wormHoles[wormXY(x, y)].type != WORM_OUT)) {
		walldist[x][y] = 0;
		q[qback].x = x;
		q[qback].y = y;
		qback++;
	    } else {
		walldist[x][y] = maxdist;
	    }
	}
    }
    if (!BIT(World.rules->mode, WRAP_PLAY)) {
	for (x = 0; x < World.x; x++) {
	    for (y = 0; y < World.y; y += (!x || x == World.x - 1)
					? 1 : (World.y - (World.y > 1))) {
		if (walldist[x][y] > 1) {
		    walldist[x][y] = 2;
		    q[qback].x = x;
		    q[qback].y = y;
		    qback++;
		}
	    }
	}
    }
    while (qfront != qback) {
	x = q[qfront].x;
	y = q[qfront].y;
	if (++qfront == World.x * World.y) {
	    qfront = 0;
	}
	dist = walldist[x][y];
	mindist = dist + 2;
	if (mindist >= 255) {
	    continue;
	}
	for (dx = -1; dx <= 1; dx++) {
	    if (BIT(World.rules->mode, WRAP_PLAY)
		|| (x + dx >= 0 && x + dx < World.x)) {
		wx = WRAP_XBLOCK(x + dx);
		for (dy = -1; dy <= 1; dy++) {
		    if (BIT(World.rules->mode, WRAP_PLAY)
			|| (y + dy >= 0 && y + dy < World.y)) {
			wy = WRAP_YBLOCK(y + dy);
			if (walldist[wx][wy] > mindist) {
			    newdist = mindist;
			    if (dist == 0) {
				if (World.block[x][y] == REC_LD) {
				    if (dx == +1 && dy == +1) {
					newdist = mindist + 1;
				    }
				}
				else if (World.block[x][y] == REC_RD) {
				    if (dx == -1 && dy == +1) {
					newdist = mindist + 1;
				    }
				}
				else if (World.block[x][y] == REC_LU) {
				    if (dx == +1 && dy == -1) {
					newdist = mindist + 1;
				    }
				}
				else if (World.block[x][y] == REC_RU) {
				    if (dx == -1 && dy == -1) {
					newdist = mindist + 1;
				    }
				}
			    }
			    if (newdist < walldist[wx][wy]) {
				walldist[wx][wy] = newdist;
				q[qback].x = wx;
				q[qback].y = wy;
				if (++qback == World.x * World.y) {
				    qback = 0;
				}
			    }
			}
		    }
		}
	    }
	}
    }
    free(q);
    Walldist_dump();
}

static void Walls_init_old(void)
{
    Walldist_alloc();
    Walldist_init();
}

void Treasure_init(void)
{
    int i;
    for (i = 0; i < World.NumTreasures; i++) {
	Make_treasure_ball(i);
    }
}


static void Bounce_edge(move_state_t *ms, move_bounce_t bounce)
{
    if (bounce == BounceHorLo) {
	if (ms->mip->edge_bounce) {
	    ms->todo.cx = -ms->todo.cx;
	    ms->vel.x = -ms->vel.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES / 2 - ms->dir, RES);
	    }
	}
	else {
	    ms->todo.cx = 0;
	    ms->vel.x = 0;
	    if (!ms->mip->pl) {
		ms->dir = (ms->vel.y < 0) ? (3*RES/4) : RES/4;
	    }
	}
    }
    else if (bounce == BounceHorHi) {
	if (ms->mip->edge_bounce) {
	    ms->todo.cx = -ms->todo.cx;
	    ms->vel.x = -ms->vel.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES / 2 - ms->dir, RES);
	    }
	}
	else {
	    ms->todo.cx = 0;
	    ms->vel.x = 0;
	    if (!ms->mip->pl) {
		ms->dir = (ms->vel.y < 0) ? (3*RES/4) : RES/4;
	    }
	}
    }
    else if (bounce == BounceVerLo) {
	if (ms->mip->edge_bounce) {
	    ms->todo.cy = -ms->todo.cy;
	    ms->vel.y = -ms->vel.y;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES - ms->dir, RES);
	    }
	}
	else {
	    ms->todo.cy = 0;
	    ms->vel.y = 0;
	    if (!ms->mip->pl) {
		ms->dir = (ms->vel.x < 0) ? (RES/2) : 0;
	    }
	}
    }
    else if (bounce == BounceVerHi) {
	if (ms->mip->edge_bounce) {
	    ms->todo.cy = -ms->todo.cy;
	    ms->vel.y = -ms->vel.y;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES - ms->dir, RES);
	    }
	}
	else {
	    ms->todo.cy = 0;
	    ms->vel.y = 0;
	    if (!ms->mip->pl) {
		ms->dir = (ms->vel.x < 0) ? (RES/2) : 0;
	    }
	}
    }
    ms->bounce = BounceEdge;
}

static void Bounce_wall(move_state_t *ms, move_bounce_t bounce)
{
    if (!ms->mip->wall_bounce) {
	ms->crash = CrashWall;
	return;
    }
    if (bounce == BounceHorLo) {
	ms->todo.cx = -ms->todo.cx;
	ms->vel.x = -ms->vel.x;
	if (!ms->mip->pl) {
	    ms->dir = MOD2(RES/2 - ms->dir, RES);
	}
    }
    else if (bounce == BounceHorHi) {
	ms->todo.cx = -ms->todo.cx;
	ms->vel.x = -ms->vel.x;
	if (!ms->mip->pl) {
	    ms->dir = MOD2(RES/2 - ms->dir, RES);
	}
    }
    else if (bounce == BounceVerLo) {
	ms->todo.cy = -ms->todo.cy;
	ms->vel.y = -ms->vel.y;
	if (!ms->mip->pl) {
	    ms->dir = MOD2(RES - ms->dir, RES);
	}
    }
    else if (bounce == BounceVerHi) {
	ms->todo.cy = -ms->todo.cy;
	ms->vel.y = -ms->vel.y;
	if (!ms->mip->pl) {
	    ms->dir = MOD2(RES - ms->dir, RES);
	}
    }
    else {
	clvec t = ms->todo;
	vector v = ms->vel;
	if (bounce == BounceLeftDown) {
	    ms->todo.cx = -t.cy;
	    ms->todo.cy = -t.cx;
	    ms->vel.x = -v.y;
	    ms->vel.y = -v.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(3*RES/4 - ms->dir, RES);
	    }
	}
	else if (bounce == BounceLeftUp) {
	    ms->todo.cx = t.cy;
	    ms->todo.cy = t.cx;
	    ms->vel.x = v.y;
	    ms->vel.y = v.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES/4 - ms->dir, RES);
	    }
	}
	else if (bounce == BounceRightDown) {
	    ms->todo.cx = t.cy;
	    ms->todo.cy = t.cx;
	    ms->vel.x = v.y;
	    ms->vel.y = v.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(RES/4 - ms->dir, RES);
	    }
	}
	else if (bounce == BounceRightUp) {
	    ms->todo.cx = -t.cy;
	    ms->todo.cy = -t.cx;
	    ms->vel.x = -v.y;
	    ms->vel.y = -v.x;
	    if (!ms->mip->pl) {
		ms->dir = MOD2(3*RES/4 - ms->dir, RES);
	    }
	}
    }
    ms->bounce = bounce;
}

/*
 * Move a point through one block and detect
 * wall collisions or bounces within that block.
 * Complications arise when the point starts at
 * the edge of a block.  E.g., if a point is on the edge
 * of a block to which block does it belong to?
 *
 * The caller supplies a set of input parameters and expects
 * the following output:
 *  - the number of pixels moved within this block.  (ms->done)
 *  - the number of pixels that still remain to be traversed. (ms->todo)
 *  - whether a crash happened, in which case no pixels will have been
 *    traversed. (ms->crash)
 *  - some extra optional output parameters depending upon the type
 *    of the crash. (ms->cannon, ms->wormhole, ms->target, ms->treasure)
 *  - whether the point bounced, in which case no pixels will have been
 *    traversed, only a change in direction. (ms->bounce, ms->vel, ms->todo)
 */
void Move_segment_old(move_state_t *ms)
{
    int			i;
    int			block_type;	/* type of block we're going through */
    int			inside;		/* inside the block or else on edge */
    int			need_adjust;	/* other param (x or y) needs recalc */
    unsigned		wall_bounce;	/* are we bouncing? what direction? */
    ipos		block;		/* block index */
    ipos		blk2;		/* new block index */
    ivec		sign;		/* sign (-1 or 1) of direction */
    clpos		delta;		/* delta position in clicks */
    clpos		enter;		/* enter block position in clicks */
    clpos		leave;		/* leave block position in clicks */
    clpos		offset;		/* offset within block in clicks */
    clpos		off2;		/* last offset in block in clicks */
    clpos		mid;		/* the mean of (offset+off2)/2 */
    const move_info_t	*const mi = ms->mip;	/* alias */
    int			hole;		/* which wormhole */
    ballobject		*ball;

    /*
     * Fill in default return values.
     */
    ms->crash = NotACrash;
    ms->bounce = NotABounce;
    ms->done.cx = 0;
    ms->done.cy = 0;

    enter = ms->pos;
    if (enter.cx < 0 || enter.cx >= mp.click_width
	|| enter.cy < 0 || enter.cy >= mp.click_height) {

	if (!mi->edge_wrap) {
	    ms->crash = CrashUniverse;
	    return;
	}
	if (enter.cx < 0) {
	    enter.cx += mp.click_width;
	    if (enter.cx < 0) {
		ms->crash = CrashUniverse;
		return;
	    }
	}
	else if (enter.cx >= mp.click_width) {
	    enter.cx -= mp.click_width;
	    if (enter.cx >= mp.click_width) {
		ms->crash = CrashUniverse;
		return;
	    }
	}
	if (enter.cy < 0) {
	    enter.cy += mp.click_height;
	    if (enter.cy < 0) {
		ms->crash = CrashUniverse;
		return;
	    }
	}
	else if (enter.cy >= mp.click_height) {
	    enter.cy -= mp.click_height;
	    if (enter.cy >= mp.click_height) {
		ms->crash = CrashUniverse;
		return;
	    }
	}
	ms->pos = enter;
    }

    sign.x = (ms->vel.x < 0) ? -1 : 1;
    sign.y = (ms->vel.y < 0) ? -1 : 1;
    block.x = enter.cx / BLOCK_CLICKS;
    block.y = enter.cy / BLOCK_CLICKS;
    if (walldist[block.x][block.y] > 2) {
	int maxcl = ((walldist[block.x][block.y] - 2) * BLOCK_CLICKS) >> 1;
	if (maxcl >= sign.x * ms->todo.cx && maxcl >= sign.y * ms->todo.cy) {
	    /* entire movement is possible. */
	    ms->done.cx = ms->todo.cx;
	    ms->done.cy = ms->todo.cy;
	}
	else if (sign.x * ms->todo.cx > sign.y * ms->todo.cy) {
	    /* horizontal movement. */
	    ms->done.cx = sign.x * maxcl;
	    ms->done.cy = ms->todo.cy * maxcl / (sign.x * ms->todo.cx);
	}
	else {
	    /* vertical movement. */
	    ms->done.cx = ms->todo.cx * maxcl / (sign.y * ms->todo.cy);
	    ms->done.cy = sign.y * maxcl;
	}
	ms->todo.cx -= ms->done.cx;
	ms->todo.cy -= ms->done.cy;
	return;
    }

    offset.cx = enter.cx - block.x * BLOCK_CLICKS;
    offset.cy = enter.cy - block.y * BLOCK_CLICKS;
    inside = 1;
    if (offset.cx == 0) {
	inside = 0;
	if (sign.x == -1 && (offset.cx = BLOCK_CLICKS, --block.x < 0)) {
	    if (mi->edge_wrap) {
		block.x += World.x;
	    }
	    else {
		Bounce_edge(ms, BounceHorLo);
		return;
	    }
	}
    }
    else if (enter.cx == mp.click_width - 1
	     && !mi->edge_wrap
	     && ms->vel.x > 0) {
	Bounce_edge(ms, BounceHorHi);
	return;
    }
    if (offset.cy == 0) {
	inside = 0;
	if (sign.y == -1 && (offset.cy = BLOCK_CLICKS, --block.y < 0)) {
	    if (mi->edge_wrap) {
		block.y += World.y;
	    }
	    else {
		Bounce_edge(ms, BounceVerLo);
		return;
	    }
	}
    }
    else if (enter.cy == mp.click_height - 1
	     && !mi->edge_wrap
	     && ms->vel.y > 0) {
	Bounce_edge(ms, BounceVerHi);
	return;
    }

    need_adjust = 0;
    if (sign.x == -1) {
	if (offset.cx + ms->todo.cx < 0) {
	    leave.cx = enter.cx - offset.cx;
	    need_adjust = 1;
	}
	else {
	    leave.cx = enter.cx + ms->todo.cx;
	}
    }
    else {
	if (offset.cx + ms->todo.cx > BLOCK_CLICKS) {
	    leave.cx = enter.cx + BLOCK_CLICKS - offset.cx;
	    need_adjust = 1;
	}
	else {
	    leave.cx = enter.cx + ms->todo.cx;
	}
	if (leave.cx == mp.click_width && !mi->edge_wrap) {
	    leave.cx--;
	    need_adjust = 1;
	}
    }
    if (sign.y == -1) {
	if (offset.cy + ms->todo.cy < 0) {
	    leave.cy = enter.cy - offset.cy;
	    need_adjust = 1;
	}
	else {
	    leave.cy = enter.cy + ms->todo.cy;
	}
    }
    else {
	if (offset.cy + ms->todo.cy > BLOCK_CLICKS) {
	    leave.cy = enter.cy + BLOCK_CLICKS - offset.cy;
	    need_adjust = 1;
	}
	else {
	    leave.cy = enter.cy + ms->todo.cy;
	}
	if (leave.cy == mp.click_height && !mi->edge_wrap) {
	    leave.cy--;
	    need_adjust = 1;
	}
    }
    if (need_adjust && ms->todo.cy && ms->todo.cx) {
	double wx = (double)(leave.cx - enter.cx) / ms->todo.cx;
	double wy = (double)(leave.cy - enter.cy) / ms->todo.cy;
	if (wx > wy) {
	    double x = ms->todo.cx * wy;
	    leave.cx = enter.cx + DOUBLE_TO_INT(x);
	}
	else if (wx < wy) {
	    double y = ms->todo.cy * wx;
	    leave.cy = enter.cy + DOUBLE_TO_INT(y);
	}
    }

    delta.cx = leave.cx - enter.cx;
    delta.cy = leave.cy - enter.cy;

    block_type = World.block[block.x][block.y];

    /*
     * We test for several different bouncing directions against the wall.
     * Sometimes there is more than one bounce possible if the point
     * starts at the corner of a block.
     * Therefore we maintain a bit mask for the bouncing possibilities
     * and later we will determine which bounce is appropriate.
     */
    wall_bounce = 0;

    if (!mi->phased) {

    switch (block_type) {

    default:
	break;

    case WORMHOLE:
	if (!mi->wormhole_warps) {
	    break;
	}
	hole = wormXY(block.x, block.y);
	if (World.wormHoles[hole].type == WORM_OUT) {
	    break;
	}
	if (mi->pl) {
	    blk2.x = OBJ_X_IN_BLOCKS(mi->pl);
	    blk2.y = OBJ_Y_IN_BLOCKS(mi->pl);
	    if (BIT(mi->pl->status, WARPED)) {
		if (World.block[blk2.x][blk2.y] == WORMHOLE) {
		    int oldhole = wormXY(blk2.x, blk2.y);
		    if (World.wormHoles[oldhole].type == WORM_NORMAL
			&& mi->pl->wormHoleDest == oldhole) {
			/*
			 * Don't warp again if we are still on the
			 * same wormhole we have just been warped to.
			 */
			break;
		    }
		}
		CLR_BIT(mi->pl->status, WARPED);
	    }
	    if (blk2.x == block.x && blk2.y == block.y) {
		ms->wormhole = hole;
		ms->crash = CrashWormHole;
		return;
	    }
	}
	else {
	    /*
	     * Warp object if this wormhole has ever warped a player.
	     * Warp the object to the same destination as the
	     * player has been warped to.
	     */
	    int last = World.wormHoles[hole].lastdest;
	    if (last >= 0
		&& (World.wormHoles[hole].countdown > 0 || !wormTime)
		&& last < World.NumWormholes
		&& World.wormHoles[last].type != WORM_IN
		&& last != hole
		&& (OBJ_X_IN_BLOCKS(mi->obj) != block.x
		 || OBJ_Y_IN_BLOCKS(mi->obj) != block.y) ) {
		ms->done.cx += (World.wormHoles[last].pos.cx
			       - World.wormHoles[hole].pos.cx);
		ms->done.cy += (World.wormHoles[last].pos.cy
			       - World.wormHoles[hole].pos.cy);
		break;
	    }
	}
	break;

    case CANNON:
	if (!mi->cannon_crashes) {
	    break;
	}
	if (BIT(mi->obj->status, FROMCANNON)
	    && !BIT(World.rules->mode, TEAM_PLAY)) {
	    break;
	}
	for (i = 0; ; i++) {
	    int bx, by;

	    bx = CLICK_TO_BLOCK(World.cannon[i].pos.cx);
	    by = CLICK_TO_BLOCK(World.cannon[i].pos.cy);
	    if (bx == block.x && by == block.y) {
		break;
	    }
	}
	ms->cannon = i;

	if (BIT(World.cannon[i].used, HAS_PHASING_DEVICE)) {
	    break;
	}
	
	if (BIT(World.rules->mode, TEAM_PLAY)
	    && (teamImmunity
		|| BIT(mi->obj->status, FROMCANNON))
	    && mi->obj->team == World.cannon[i].team) {
	    break;
	}
	{
	    /*
	     * Calculate how far the point can travel in the cannon block
	     * before hitting the cannon.
	     * To reduce duplicate code we first transform all the
	     * different cannon types into one by matrix multiplications.
	     * Later we transform the result back to the real type.
	     */

	    ivec mx, my, dir;
	    clpos mirx, miry, start, end, todo, done, diff, a, b;
	    double d, w;

	    mirx.cx = 0;
	    mirx.cy = 0;
	    miry.cx = 0;
	    miry.cy = 0;
	    switch (World.cannon[i].dir) {
	    case DIR_UP:
		mx.x = 1; mx.y = 0;
		my.x = 0; my.y = 1;
		break;
	    case DIR_DOWN:
		mx.x = 1; mx.y = 0;
		my.x = 0; my.y = -1;
		miry.cy = BLOCK_CLICKS;
		break;
	    case DIR_RIGHT:
		mx.x = 0; mx.y = 1;
		my.x = -1; my.y = 0;
		miry.cx = BLOCK_CLICKS;
		break;
	    case DIR_LEFT:
		mx.x = 0; mx.y = -1;
		my.x = 1; my.y = 0;
		mirx.cy = BLOCK_CLICKS;
		break;
	    }
	    start.cx = mirx.cx + mx.x * offset.cx + miry.cx + my.x * offset.cy;
	    start.cy = mirx.cy + mx.y * offset.cx + miry.cy + my.y * offset.cy;
	    diff.cx  =          mx.x * delta.cx           + my.x * delta.cy;
	    diff.cy  =          mx.y * delta.cx           + my.y * delta.cy;
	    dir.x   =          mx.x * sign.x            + my.x * sign.y;
	    dir.y   =          mx.y * sign.x            + my.y * sign.y;
	    todo.cx  =          mx.x * ms->todo.cx       + my.x * ms->todo.cy;
	    todo.cy  =          mx.y * ms->todo.cx       + my.y * ms->todo.cy;

	    end.cx = start.cx + diff.cx;
	    end.cy = start.cy + diff.cy;

	    if (start.cx <= BLOCK_CLICKS/2) {
		if (3 * start.cy <= 2 * start.cx) {
		    ms->crash = CrashCannon;
		    return;
		}
		if (end.cx <= BLOCK_CLICKS/2) {
		    if (3 * end.cy > 2 * end.cx) {
			break;
		    }
		}
	    }
	    else {
		if (3 * start.cy <= 2 * (BLOCK_CLICKS - start.cx)) {
		    ms->crash = CrashCannon;
		    return;
		}
		if (end.cx > BLOCK_CLICKS/2) {
		    if (3 * end.cy > 2 * (BLOCK_CLICKS - end.cx)) {
			break;
		    }
		}
	    }

	    done = diff;

	    /* is direction x-major? */
	    if (dir.x * diff.cx >= dir.y * diff.cy) {
		/* x-major */
		w = (double) todo.cy / todo.cx;
		if (3 * todo.cy != 2 * todo.cx) {
		    d = (3 * start.cy - 2 * start.cx) / (2 - 3 * w);
		    a.cx = DOUBLE_TO_INT(d);
		    a.cy = (int)(a.cx * w);
		    if (dir.x * a.cx < dir.x * done.cx && dir.x * a.cx >= 0) {
			if (start.cy + a.cy <= BLOCK_CLICKS/3) {
			    done = a;
			    if (!(done.cx | done.cy)) {
				ms->crash = CrashCannon;
				return;
			    }
			}
		    }
		}
		if (-3 * todo.cy != 2 * todo.cx) {
		    d = (2 * BLOCK_CLICKS - 2 * start.cx - 3 * start.cy) /
			(2 + 3 * w);
		    b.cx = DOUBLE_TO_INT(d);
		    b.cy = (int)(b.cx * w);
		    if (dir.x * b.cx < dir.x * done.cx && dir.x * b.cx >= 0) {
			if (start.cy + b.cy <= BLOCK_CLICKS/3) {
			    done = b;
			    if (!(done.cx | done.cy)) {
				ms->crash = CrashCannon;
				return;
			    }
			}
		    }
		}
	    } else {
		/* y-major */
		w = (double) todo.cx / todo.cy;
		d = (2 * start.cx - 3 * start.cy) / (3 - 2 * w);
		a.cy = DOUBLE_TO_INT(d);
		a.cx = (int)(a.cy * w);
		if (dir.y * a.cy < dir.y * done.cy && dir.y * a.cy >= 0) {
		    if (start.cy + a.cy <= BLOCK_CLICKS/3) {
			done = a;
			if (!(done.cx | done.cy)) {
			    ms->crash = CrashCannon;
			    return;
			}
		    }
		}
		d = (2 * BLOCK_CLICKS - 2 * start.cx - 3 * start.cy) /
		    (3 + 2 * w);
		b.cy = DOUBLE_TO_INT(d);
		b.cx = (int)(b.cy * w);
		if (dir.y * b.cy < dir.y * done.cy && dir.y * b.cy >= 0) {
		    if (start.cy + b.cy <= BLOCK_CLICKS/3) {
			done = b;
			if (!(done.cx | done.cy)) {
			    ms->crash = CrashCannon;
			    return;
			}
		    }
		}
	    }

	    delta.cx = mx.x * done.cx + mx.y * done.cy;
	    delta.cy = my.x * done.cx + my.y * done.cy;
	}
	break;

    case TREASURE:
	if (block_type == TREASURE) {
	    if (mi->treasure_crashes) {
		/*
		 * Test if the movement is within the upper half of
		 * the treasure, which is the upper half of a circle.
		 * If this is the case then we test if 3 samples
		 * are not hitting the treasure.
		 */
		const DFLOAT r = 0.5f * BLOCK_CLICKS;
		off2.cx = offset.cx + delta.cx;
		off2.cy = offset.cy + delta.cy;
		mid.cx = (offset.cx + off2.cx) / 2;
		mid.cy = (offset.cy + off2.cy) / 2;
		if (offset.cy > r
		    && off2.cy > r
		    && sqr(mid.cx - r) + sqr(mid.cy - r) > sqr(r)
		    && sqr(off2.cx - r) + sqr(off2.cy - r) > sqr(r)
		    && sqr(offset.cx - r) + sqr(offset.cy - r) > sqr(r)) {
		    break;
		}

		for (i = 0; ; i++) {
		    if (World.treasures[i].pos.cx / BLOCK_CLICKS == block.x &&
			World.treasures[i].pos.cy / BLOCK_CLICKS == block.y) {
			break;
		    }
		}
		ms->treasure = i;
		ms->crash = CrashTreasure;

		/*
		 * We handle balls here, because the reaction
		 * depends on which team the treasure and the ball
		 * belong to.
		 */
		if (mi->obj->type != OBJ_BALL) {
		    return;
		}

		ball = BALL_PTR(mi->obj);
		if (ms->treasure == ball->treasure) {
		    /*
		     * Ball has been replaced back in the hoop from whence
		     * it came.  If the player is on the same team as the
		     * hoop, then it should be replaced into the hoop without
		     * exploding and gets the player some points.  Otherwise
		     * nothing interesting happens.
		     */
		    player	*pl = NULL;
		    treasure_t	*tt = &World.treasures[ms->treasure];

		    if (ball->owner != NO_ID)
			pl = Players[GetInd[ball->owner]];

		    if (!BIT(World.rules->mode, TEAM_PLAY)
			|| !pl
			|| (pl->team !=
			    World.treasures[ball->treasure].team)) {
			ball->life = LONG_MAX;
			ms->crash = NotACrash;
			break;
		    }

		    Ball_is_replaced(ball, tt, pl);
		    break;
		}
		if (ball->owner == NO_ID) {
		    ball->life = 0;
		    return;
		}
		if (BIT(World.rules->mode, TEAM_PLAY)
		    && World.treasures[ms->treasure].team ==
		       Players[GetInd[ball->owner]]->team) {
		    Ball_is_destroyed(ball);
		    if (captureTheFlag
			&& !World.treasures[ms->treasure].have
			&& !World.treasures[ms->treasure].empty) {
			strcpy(msg, "Your treasure must be safe before you "
			       "can cash an opponent's!");
			Set_player_message(Players[GetInd[ball->owner]], msg);
		    } else if (Punish_team(GetInd[ball->owner],
					   ball->treasure,
					   ball->pos.cx, ball->pos.cy))
			CLR_BIT(ball->status, RECREATE);
		}
		ball->life = 0;
		return;
	    }
	}
	/*FALLTHROUGH*/

    case TARGET:
	if (block_type == TARGET) {
	    if (mi->target_crashes) {
		/*-BA This can be slow for large number of targets.
		 *     added itemID array for extra speed, (at cost of some memory.)
		 *     
		 *for (i = 0; ; i++) {
		 *    if (World.targets[i].pos.cx / BLOCK_CLICKS == block.x
		 *	&& World.targets[i].pos.cy / BLOCK_CLICKS == block.y) {
		 *	break;
		 *     }
		 * }
		 *
		 * ms->target = i;
		 */
		ms->target = i = World.itemID[block.x][block.y];

		if (!targetTeamCollision) {
		    int team;
		    if (mi->pl) {
			team = mi->pl->team;
		    }
		    else if (BIT(mi->obj->type, OBJ_BALL)) {
			ballobject *ball = BALL_PTR(mi->obj);
			if (ball->owner != NO_ID) {
			    team = Players[GetInd[ball->owner]]->team;
			} else {
			    team = TEAM_NOT_SET;
			}
		    }
		    else {
			team = mi->obj->team;
		    }
		    if (team == World.targets[i].team) {
			break;
		    }
		}
		if (!mi->pl) {
		    ms->crash = CrashTarget;
		    return;
		}
	    }
	}
	/*FALLTHROUGH*/

    case FUEL:
    case FILLED:
	if (inside) {
	    /* Could happen for targets reappearing and in case of bugs. */
	    ms->crash = CrashWall;
	    return;
	}
	if (offset.cx == 0) {
	    if (ms->vel.x > 0) {
		wall_bounce |= BounceHorLo;
	    }
	}
	else if (offset.cx == BLOCK_CLICKS) {
	    if (ms->vel.x < 0) {
		wall_bounce |= BounceHorHi;
	    }
	}
	if (offset.cy == 0) {
	    if (ms->vel.y > 0) {
		wall_bounce |= BounceVerLo;
	    }
	}
	else if (offset.cy == BLOCK_CLICKS) {
	    if (ms->vel.y < 0) {
		wall_bounce |= BounceVerHi;
	    }
	}
	if (wall_bounce) {
	    break;
	}
	if (!(ms->todo.cx | ms->todo.cy)) {
	    /* no bouncing possible and no movement.  OK. */
	    break;
	}
	if (!ms->todo.cx && (offset.cx == 0 || offset.cx == BLOCK_CLICKS)) {
	    /* tricky */
	    break;
	}
	if (!ms->todo.cy && (offset.cy == 0 || offset.cy == BLOCK_CLICKS)) {
	    /* tricky */
	    break;
	}
	/* what happened? we should never reach this */
	ms->crash = CrashWall;
	return;

    case REC_LD:
	/* test for bounces first. */
	if (offset.cx == 0) {
	    if (ms->vel.x > 0) {
		wall_bounce |= BounceHorLo;
	    }
	    if (offset.cy == BLOCK_CLICKS && ms->vel.x + ms->vel.y < 0) {
		wall_bounce |= BounceLeftDown;
	    }
	}
	if (offset.cy == 0) {
	    if (ms->vel.y > 0) {
		wall_bounce |= BounceVerLo;
	    }
	    if (offset.cx == BLOCK_CLICKS && ms->vel.x + ms->vel.y < 0) {
		wall_bounce |= BounceLeftDown;
	    }
	}
	if (wall_bounce) {
	    break;
	}
	if (offset.cx + offset.cy < BLOCK_CLICKS) {
	    ms->crash = CrashWall;
	    return;
	}
	if (offset.cx + delta.cx + offset.cy + delta.cy >= BLOCK_CLICKS) {
	    /* movement is entirely within the space part of the block. */
	    break;
	}
	/*
	 * Find out where we bounce exactly
	 * and how far we can move before bouncing.
	 */
	if (sign.x * ms->todo.cx >= sign.y * ms->todo.cy) {
	    double w = (double) ms->todo.cy / ms->todo.cx;
	    delta.cx = (int)((BLOCK_CLICKS - offset.cx - offset.cy) / (1 + w));
	    delta.cy = (int)(delta.cx * w);
	    if (offset.cx + delta.cx + offset.cy + delta.cy < BLOCK_CLICKS) {
		delta.cx++;
		delta.cy = (int)(delta.cx * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cx) {
		wall_bounce |= BounceLeftDown;
		break;
	    }
	}
	else {
	    double w = (double) ms->todo.cx / ms->todo.cy;
	    delta.cy = (int)((BLOCK_CLICKS - offset.cx - offset.cy) / (1 + w));
	    delta.cx = (int)(delta.cy * w);
	    if (offset.cx + delta.cx + offset.cy + delta.cy < BLOCK_CLICKS) {
		delta.cy++;
		delta.cx = (int)(delta.cy * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cy) {
		wall_bounce |= BounceLeftDown;
		break;
	    }
	}
	break;

    case REC_LU:
	if (offset.cx == 0) {
	    if (ms->vel.x > 0) {
		wall_bounce |= BounceHorLo;
	    }
	    if (offset.cy == 0 && ms->vel.x < ms->vel.y) {
		wall_bounce |= BounceLeftUp;
	    }
	}
	if (offset.cy == BLOCK_CLICKS) {
	    if (ms->vel.y < 0) {
		wall_bounce |= BounceVerHi;
	    }
	    if (offset.cx == BLOCK_CLICKS && ms->vel.x < ms->vel.y) {
		wall_bounce |= BounceLeftUp;
	    }
	}
	if (wall_bounce) {
	    break;
	}
	if (offset.cx < offset.cy) {
	    ms->crash = CrashWall;
	    return;
	}
	if (offset.cx + delta.cx >= offset.cy + delta.cy) {
	    break;
	}
	if (sign.x * ms->todo.cx >= sign.y * ms->todo.cy) {
	    double w = (double) ms->todo.cy / ms->todo.cx;
	    delta.cx = (int)((offset.cy - offset.cx) / (1 - w));
	    delta.cy = (int)(delta.cx * w);
	    if (offset.cx + delta.cx < offset.cy + delta.cy) {
		delta.cx++;
		delta.cy = (int)(delta.cx * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cx) {
		wall_bounce |= BounceLeftUp;
		break;
	    }
	}
	else {
	    double w = (double) ms->todo.cx / ms->todo.cy;
	    delta.cy = (int)((offset.cx - offset.cy) / (1 - w));
	    delta.cx = (int)(delta.cy * w);
	    if (offset.cx + delta.cx < offset.cy + delta.cy) {
		delta.cy--;
		delta.cx = (int)(delta.cy * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cy) {
		wall_bounce |= BounceLeftUp;
		break;
	    }
	}
	break;

    case REC_RD:
	if (offset.cx == BLOCK_CLICKS) {
	    if (ms->vel.x < 0) {
		wall_bounce |= BounceHorHi;
	    }
	    if (offset.cy == BLOCK_CLICKS && ms->vel.x > ms->vel.y) {
		wall_bounce |= BounceRightDown;
	    }
	}
	if (offset.cy == 0) {
	    if (ms->vel.y > 0) {
		wall_bounce |= BounceVerLo;
	    }
	    if (offset.cx == 0 && ms->vel.x > ms->vel.y) {
		wall_bounce |= BounceRightDown;
	    }
	}
	if (wall_bounce) {
	    break;
	}
	if (offset.cx > offset.cy) {
	    ms->crash = CrashWall;
	    return;
	}
	if (offset.cx + delta.cx <= offset.cy + delta.cy) {
	    break;
	}
	if (sign.x * ms->todo.cx >= sign.y * ms->todo.cy) {
	    double w = (double) ms->todo.cy / ms->todo.cx;
	    delta.cx = (int)((offset.cy - offset.cx) / (1 - w));
	    delta.cy = (int)(delta.cx * w);
	    if (offset.cx + delta.cx > offset.cy + delta.cy) {
		delta.cx--;
		delta.cy = (int)(delta.cx * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cx) {
		wall_bounce |= BounceRightDown;
		break;
	    }
	}
	else {
	    double w = (double) ms->todo.cx / ms->todo.cy;
	    delta.cy = (int)((offset.cx - offset.cy) / (1 - w));
	    delta.cx = (int)(delta.cy * w);
	    if (offset.cx + delta.cx > offset.cy + delta.cy) {
		delta.cy++;
		delta.cx = (int)(delta.cy * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cy) {
		wall_bounce |= BounceRightDown;
		break;
	    }
	}
	break;

    case REC_RU:
	if (offset.cx == BLOCK_CLICKS) {
	    if (ms->vel.x < 0) {
		wall_bounce |= BounceHorHi;
	    }
	    if (offset.cy == 0 && ms->vel.x + ms->vel.y > 0) {
		wall_bounce |= BounceRightUp;
	    }
	}
	if (offset.cy == BLOCK_CLICKS) {
	    if (ms->vel.y < 0) {
		wall_bounce |= BounceVerHi;
	    }
	    if (offset.cx == 0 && ms->vel.x + ms->vel.y > 0) {
		wall_bounce |= BounceRightUp;
	    }
	}
	if (wall_bounce) {
	    break;
	}
	if (offset.cx + offset.cy > BLOCK_CLICKS) {
	    ms->crash = CrashWall;
	    return;
	}
	if (offset.cx + delta.cx + offset.cy + delta.cy <= BLOCK_CLICKS) {
	    break;
	}
	if (sign.x * ms->todo.cx >= sign.y * ms->todo.cy) {
	    double w = (double) ms->todo.cy / ms->todo.cx;
	    delta.cx = (int)((BLOCK_CLICKS - offset.cx - offset.cy) / (1 + w));
	    delta.cy = (int)(delta.cx * w);
	    if (offset.cx + delta.cx + offset.cy + delta.cy > BLOCK_CLICKS) {
		delta.cx--;
		delta.cy = (int)(delta.cx * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cx) {
		wall_bounce |= BounceRightUp;
		break;
	    }
	}
	else {
	    double w = (double) ms->todo.cx / ms->todo.cy;
	    delta.cy = (int)((BLOCK_CLICKS - offset.cx - offset.cy) / (1 + w));
	    delta.cx = (int)(delta.cy * w);
	    if (offset.cx + delta.cx + offset.cy + delta.cy > BLOCK_CLICKS) {
		delta.cy--;
		delta.cx = (int)(delta.cy * w);
	    }
	    leave.cx = enter.cx + delta.cx;
	    leave.cy = enter.cy + delta.cy;
	    if (!delta.cy) {
		wall_bounce |= BounceRightUp;
		break;
	    }
	}
	break;
    }

    if (wall_bounce) {
	/*
	 * Bouncing.  As there may be more than one possible bounce
	 * test which bounce is not feasible because of adjacent walls.
	 * If there still is more than one possible then pick one randomly.
	 * Else if it turns out that none is feasible then we must have
	 * been trapped inbetween two blocks.  This happened in the early
	 * stages of this code.
	 */
	int count = 0;
	unsigned bit;
	unsigned save_wall_bounce = wall_bounce;
	unsigned block_mask = FILLED_BIT | FUEL_BIT;

	if (!mi->target_crashes) {
	    block_mask |= TARGET_BIT;
	}
	if (!mi->treasure_crashes) {
	    block_mask |= TREASURE_BIT;
	}
	for (bit = 1; bit <= wall_bounce; bit <<= 1) {
	    if (!(wall_bounce & bit)) {
		continue;
	    }

	    CLR_BIT(wall_bounce, bit);
	    switch (bit) {

	    case BounceHorLo:
		blk2.x = block.x - 1;
		if (blk2.x < 0) {
		    if (!mi->edge_wrap) {
			continue;
		    }
		    blk2.x += World.x;
		}
		blk2.y = block.y;
		if (BIT(1 << World.block[blk2.x][blk2.y],
			block_mask|REC_RU_BIT|REC_RD_BIT)) {
		    continue;
		}
		break;

	    case BounceHorHi:
		blk2.x = block.x + 1;
		if (blk2.x >= World.x) {
		    if (!mi->edge_wrap) {
			continue;
		    }
		    blk2.x -= World.x;
		}
		blk2.y = block.y;
		if (BIT(1 << World.block[blk2.x][blk2.y],
			block_mask|REC_LU_BIT|REC_LD_BIT)) {
		    continue;
		}
		break;

	    case BounceVerLo:
		blk2.x = block.x;
		blk2.y = block.y - 1;
		if (blk2.y < 0) {
		    if (!mi->edge_wrap) {
			continue;
		    }
		    blk2.y += World.y;
		}
		if (BIT(1 << World.block[blk2.x][blk2.y],
			block_mask|REC_RU_BIT|REC_LU_BIT)) {
		    continue;
		}
		break;

	    case BounceVerHi:
		blk2.x = block.x;
		blk2.y = block.y + 1;
		if (blk2.y >= World.y) {
		    if (!mi->edge_wrap) {
			continue;
		    }
		    blk2.y -= World.y;
		}
		if (BIT(1 << World.block[blk2.x][blk2.y],
			block_mask|REC_RD_BIT|REC_LD_BIT)) {
		    continue;
		}
		break;
	    }

	    SET_BIT(wall_bounce, bit);
	    count++;
	}

	if (!count) {
	    wall_bounce = save_wall_bounce;
	    switch (wall_bounce) {
	    case BounceHorLo|BounceVerLo:
		wall_bounce = BounceLeftDown;
		break;
	    case BounceHorLo|BounceVerHi:
		wall_bounce = BounceLeftUp;
		break;
	    case BounceHorHi|BounceVerLo:
		wall_bounce = BounceRightDown;
		break;
	    case BounceHorHi|BounceVerHi:
		wall_bounce = BounceRightUp;
		break;
	    default:
		switch (block_type) {
		case REC_LD:
		    if ((offset.cx == 0) ? (offset.cy == BLOCK_CLICKS)
			: (offset.cx == BLOCK_CLICKS && offset.cy == 0)
			&& ms->vel.x + ms->vel.y >= 0) {
			wall_bounce = 0;
		    }
		    break;
		case REC_LU:
		    if ((offset.cx == 0) ? (offset.cy == 0)
			: (offset.cx == BLOCK_CLICKS && offset.cy == BLOCK_CLICKS)
			&& ms->vel.x >= ms->vel.y) {
			wall_bounce = 0;
		    }
		    break;
		case REC_RD:
		    if ((offset.cx == 0) ? (offset.cy == 0)
			: (offset.cx == BLOCK_CLICKS && offset.cy == BLOCK_CLICKS)
			&& ms->vel.x <= ms->vel.y) {
			wall_bounce = 0;
		    }
		    break;
		case REC_RU:
		    if ((offset.cx == 0) ? (offset.cy == BLOCK_CLICKS)
			: (offset.cx == BLOCK_CLICKS && offset.cy == 0)
			&& ms->vel.x + ms->vel.y <= 0) {
			wall_bounce = 0;
		    }
		    break;
		}
		if (wall_bounce) {
		    ms->crash = CrashWall;
		    return;
		}
	    }
	}
	else if (count > 1) {
	    /*
	     * More than one bounce possible.
	     * Pick one randomly.
	     */
	    count = (int)(rfrac() * count);
	    for (bit = 1; bit <= wall_bounce; bit <<= 1) {
		if (wall_bounce & bit) {
		    if (count == 0) {
			wall_bounce = bit;
			break;
		    } else {
			count--;
		    }
		}
	    }
	}
    }

    } /* phased */

    if (wall_bounce) {
	Bounce_wall(ms, (move_bounce_t) wall_bounce);
    }
    else {
	ms->done.cx += delta.cx;
	ms->done.cy += delta.cy;
	ms->todo.cx -= delta.cx;
	ms->todo.cy -= delta.cy;
    }
}



static void Cannon_dies(int ind, player *pl)
{
    cannon_t		*cannon = &World.cannon[ind];
    int			cx = cannon->pos.cx;
    int			cy = cannon->pos.cy;
    int			killer = -1;

    cannon->dead_time = cannonDeadTime * TIME_FACT;
    cannon->conn_mask = 0;
    World.block[CLICK_TO_BLOCK(cx)][CLICK_TO_BLOCK(cy)] = SPACE;
    Cannon_throw_items(ind);
    Cannon_init(ind);
    sound_play_sensors(cx, cy, CANNON_EXPLOSION_SOUND);
    Make_debris(
	/* pos.cx, pos.cy   */ cx, cy,
	/* vel.x, vel.y   */ 0.0, 0.0,
	/* owner id       */ NO_ID,
	/* owner team	  */ cannon->team,
	/* kind           */ OBJ_DEBRIS,
	/* mass           */ 4.5,
	/* status         */ GRAVITY,
	/* color          */ RED,
	/* radius         */ 6,
	/* num debris     */ 20 + 20 * rfrac(),
	/* min,max dir    */ (int)(cannon->dir - (RES * 0.2)), (int)(cannon->dir + (RES * 0.2)),
	/* min,max speed  */ 20, 50,
	/* min,max life   */ 8 * TIME_FACT, 68 * TIME_FACT
	);
    Make_wreckage(
	/* pos.cx, pos.cy   */ cx, cy,
	/* vel.x, vel.y   */ 0.0, 0.0,
	/* owner id       */ NO_ID,
	/* owner team	  */ cannon->team,
	/* min,max mass   */ 3.5, 23,
	/* total mass     */ 28,
	/* status         */ GRAVITY,
	/* color          */ WHITE,
	/* max wreckage   */ 10,
	/* min,max dir    */ (int)(cannon->dir - (RES * 0.2)), (int)(cannon->dir + (RES * 0.2)),
	/* min,max speed  */ 10, 25,
	/* min,max life   */ 8 * TIME_FACT, 68 * TIME_FACT
	);

    if (pl) {
	killer = GetInd[pl->id];
	if (cannonPoints > 0) {
	    if (BIT(World.rules->mode, TEAM_PLAY)
		&& teamCannons) {
		TEAM_SCORE(cannon->team, -cannonPoints);
	    }
	    if (pl->score <= cannonMaxScore
		&& !(BIT(World.rules->mode, TEAM_PLAY)
		     && pl->team == cannon->team)) {
		SCORE(killer, cannonPoints, cannon->pos.cx,
					    cannon->pos.cy, "");
	    }
	}
    }
}

static void Cannon_dies_old(move_state_t *ms)
{
    int ind = ms->cannon;
    player *pl = NULL;

    if (!ms->mip->pl) {
	if (ms->mip->obj->id != NO_ID) {
	    pl = Players[GetInd[ms->mip->obj->id]];
	}
    } else if (BIT(ms->mip->pl->used, HAS_SHIELD|HAS_EMERGENCY_SHIELD)
	       == (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
	pl = ms->mip->pl;
    }

    Cannon_dies(ind, pl);
}

static void Object_hits_target(target_t *targ, object *obj,
			       long player_cost)
{
    int			j,
			x, y,
			killer;
    DFLOAT		sc, por,
			win_score = 0,
			lose_score = 0;
    int			win_team_members = 0,
			lose_team_members = 0,
			somebody_flag = 0,
			targets_remaining = 0,
			targets_total = 0;
    DFLOAT 		drainfactor;

    /* a normal shot or a direct mine hit work, cannons don't */
    /* KK: should shots/mines by cannons of opposing teams work? */
    /* also players suiciding on target will cause damage */
    if (!BIT(obj->type, KILLING_SHOTS|OBJ_MINE|OBJ_PULSE|OBJ_PLAYER)) {
	return;
    }
    if (obj->id <= 0) {
	return;
    }
    killer = GetInd[obj->id];
    if (targ->team == obj->team) {
	return;
    }

    switch(obj->type) {
    case OBJ_SHOT:
	if (shotHitFuelDrainUsesKineticEnergy) {
	    drainfactor = VECTOR_LENGTH(obj->vel);
	    drainfactor = (drainfactor * drainfactor * ABS(obj->mass))
			  / (ShotsSpeed * ShotsSpeed * ShotsMass);
	} else {
	    drainfactor = 1.0f;
	}
	targ->damage += (int)(ED_SHOT_HIT * drainfactor * SHOT_MULT(obj));
	break;
    case OBJ_PULSE:
	targ->damage += (int)(ED_LASER_HIT);
	break;
    case OBJ_SMART_SHOT:
    case OBJ_TORPEDO:
    case OBJ_HEAT_SHOT:
	if (!obj->mass) {
	    /* happens at end of round reset. */
	    return;
	}
	if (BIT(obj->mods.nuclear, NUCLEAR)) {
	    targ->damage = 0;
	}
	else {
	    targ->damage += (int)(ED_SMART_SHOT_HIT / (obj->mods.mini + 1));
	}
	break;
    case OBJ_MINE:
	if (!obj->mass) {
	    /* happens at end of round reset. */
	    return;
	}
	targ->damage -= TARGET_DAMAGE / (obj->mods.mini + 1);
	break;
    case OBJ_PLAYER:
	if (player_cost <= 0 || player_cost > TARGET_DAMAGE / 4)
	    player_cost = TARGET_DAMAGE / 4;
	targ->damage -= player_cost;
	break;

    default:
	/*???*/
	break;
    }

    targ->conn_mask = 0;
    targ->last_change = frame_loops;
    if (targ->damage > 0)
	return;

    targ->update_mask = (unsigned) -1;
    targ->damage = TARGET_DAMAGE;
    targ->dead_time = targetDeadTime * TIME_FACT;

    /*
     * Destroy target.
     * Turn it into a space to simplify other calculations.
     */
    x = targ->pos.cx / BLOCK_CLICKS;
    y = targ->pos.cy / BLOCK_CLICKS;
    World.block[x][y] = SPACE;

    Make_debris(
	/* pos.cx, pos.cy   */ targ->pos.cx, targ->pos.cy,
	/* vel.x, vel.y   */ 0.0, 0.0,
	/* owner id       */ NO_ID,
	/* owner team	  */ targ->team,
	/* kind           */ OBJ_DEBRIS,
	/* mass           */ 4.5,
	/* status         */ GRAVITY,
	/* color          */ RED,
	/* radius         */ 6,
	/* num debris     */ 75 + 75 * rfrac(),
	/* min,max dir    */ 0, RES-1,
	/* min,max speed  */ 20, 70,
	/* min,max life   */ 10 * TIME_FACT, 100 * TIME_FACT
	);

    if (BIT(World.rules->mode, TEAM_PLAY)) {
	for (j = 0; j < NumPlayers; j++) {
	    if (IS_TANK_IND(j)
		|| (BIT(Players[j]->status, PAUSE)
		    && Players[j]->count <= 0)
		|| (BIT(Players[j]->status, GAME_OVER)
		    && Players[j]->mychar == 'W'
		    && Players[j]->score == 0)) {
		continue;
	    }
	    if (Players[j]->team == targ->team) {
		lose_score += Players[j]->score;
		lose_team_members++;
		if (BIT(Players[j]->status, GAME_OVER) == 0) {
		    somebody_flag = 1;
		}
	    }
	    else if (Players[j]->team == Players[killer]->team) {
		win_score += Players[j]->score;
		win_team_members++;
	    }
	}
    }
    if (somebody_flag) {
	for (j = 0; j < World.NumTargets; j++) {
	    if (World.targets[j].team == targ->team) {
		targets_total++;
		if (World.targets[j].dead_time == 0) {
		    targets_remaining++;
		}
	    }
	}
    }
    if (!somebody_flag) {
	return;
    }

    sound_play_sensors(PIXEL_TO_CLICK(x), PIXEL_TO_CLICK(y),
		       DESTROY_TARGET_SOUND);

    if (targets_remaining > 0) {
	sc = Rate(Players[killer]->score, CANNON_SCORE)/4;
	sc = sc * (targets_total - targets_remaining) / (targets_total + 1);
	if (sc >= 0.01) {
	    SCORE(killer, sc, targ->pos.cx, targ->pos.cy, "Target: ");
	}
	/*
	 * If players can't collide with their own targets, we
	 * assume there are many used as shields.  Don't litter
	 * the game with the message below.
	 */
	if (targetTeamCollision && targets_total < 10) {
	    sprintf(msg, "%s blew up one of team %d's targets.",
		    Players[killer]->name, (int) targ->team);
	    Set_message(msg);
	}
	return;
    }

    sprintf(msg, "%s blew up team %d's %starget.",
	    Players[killer]->name,
	    (int) targ->team,
	    (targets_total > 1) ? "last " : "");
    Set_message(msg);

    if (targetKillTeam) {
	Rank_AddTargetKill(Players[killer]);
    }

    sc  = Rate(win_score, lose_score);
    por = (sc*lose_team_members)/win_team_members;

    for (j = 0; j < NumPlayers; j++) {
	if (IS_TANK_IND(j)
	    || (BIT(Players[j]->status, PAUSE)
		&& Players[j]->count <= 0)
	    || (BIT(Players[j]->status, GAME_OVER)
		&& Players[j]->mychar == 'W'
		&& Players[j]->score == 0)) {
	    continue;
	}
	if (Players[j]->team == targ->team) {
	    if (targetKillTeam
		&& targets_remaining == 0
		&& !BIT(Players[j]->status, KILLED|PAUSE|GAME_OVER))
		SET_BIT(Players[j]->status, KILLED);
	    SCORE(j, -sc, targ->pos.cx, targ->pos.cy, "Target: ");
	}
	else if (Players[j]->team == Players[killer]->team &&
		 (Players[j]->team != TEAM_NOT_SET || j == killer)) {
	    SCORE(j, por, targ->pos.cx, targ->pos.cy, "Target: ");
	}
    }
}


static void Object_hits_target_old(move_state_t *ms, long player_cost)
{
    target_t		*targ = &World.targets[ms->target];
    object		*obj = ms->mip->obj;

    Object_hits_target(targ, obj, player_cost);
}



static void Object_crash_old(move_state_t *ms)
{
    object		*obj = ms->mip->obj;

    switch (ms->crash) {

    case CrashWormHole:
    default:
	break;

    case CrashTreasure:
	/*
	 * Ball type has already been handled.
	 */
	if (obj->type == OBJ_BALL) {
	    break;
	}
	obj->life = 0;
	break;

    case CrashTarget:
	obj->life = 0;
	/*Object_hits_target_old(ms, -1);*/
	Object_hits_target(&World.targets[ms->target], obj, -1);
	break;

    case CrashWall:
	obj->life = 0;
#if 0
/* KK: - Added sparks to wallcrashes for objects != OBJ_SPARK|OBJ_DEBRIS.
**       I'm not sure of the amount of sparks or the direction.
*/
	if (!BIT(obj->type, OBJ_SPARK | OBJ_DEBRIS)) {
	    Make_debris(ms->pos.cx, ms->pos.cy,
			0, 0,
			obj->owner,
			obj->team,
			OBJ_SPARK,
			(obj->mass * VECTOR_LENGTH(obj->vel)) / 3,
			GRAVITY,
			RED,
			1,
			5 + 5 * rfrac(),
			MOD2(ms->dir - RES/4, RES), MOD2(ms->dir + RES/4, RES),
			15, 25,
			5 * TIME_FACT, 15 * TIME_FACT);
	}
#endif
	break;

    case CrashUniverse:
	obj->life = 0;
	break;

    case CrashCannon:
	obj->life = 0;
	if (BIT(obj->type, OBJ_ITEM)) {
	    Cannon_add_item(ms->cannon, obj->info, obj->count);
	} else {
	    if (!BIT(World.cannon[ms->cannon].used, HAS_EMERGENCY_SHIELD)) {
		if (World.cannon[ms->cannon].item[ITEM_ARMOR] > 0)
		    World.cannon[ms->cannon].item[ITEM_ARMOR]--;
		else
		    Cannon_dies_old(ms);
	    }
	}
	break;

    case CrashUnknown:
	obj->life = 0;
	break;
    }
}

void Move_object_old(object *obj)
{
    int			nothing_done = 0;
    int			dist;
    move_info_t		mi;
    move_state_t	ms;
    bool		pos_update = false;

    Object_position_remember(obj);

    dist = walldist[obj->pos.bx][obj->pos.by];
    if (dist > 2) {
	int max = ((dist - 2) * BLOCK_SZ) >> 1;
	if (sqr(max) >= sqr(obj->vel.x) + sqr(obj->vel.y)) {
	    /*
	     * kps - comment the two timestep factors and the dodgers
	     * laser wrap problem is removed
	     */
	    int cx = obj->pos.cx + FLOAT_TO_CLICK(obj->vel.x * timeStep2);
	    int cy = obj->pos.cy + FLOAT_TO_CLICK(obj->vel.y * timeStep2);
	    cx = WRAP_XCLICK(cx);
	    cy = WRAP_YCLICK(cy);
	    Object_position_set_clicks(obj, cx, cy);
	    Cell_add_object(obj);
	    return;
	}
    }

    mi.pl = NULL;
    mi.obj = obj;
    mi.edge_wrap = BIT(World.rules->mode, WRAP_PLAY);
    mi.edge_bounce = edgeBounce;
    mi.wall_bounce = BIT(mp.obj_bounce_mask, obj->type);
    mi.cannon_crashes = BIT(mp.obj_cannon_mask, obj->type);
    mi.target_crashes = BIT(mp.obj_target_mask, obj->type);
    mi.treasure_crashes = BIT(mp.obj_treasure_mask, obj->type);
    mi.wormhole_warps = true;
    if (BIT(obj->type, OBJ_BALL) && obj->id != NO_ID) {
	mi.phased = BIT(Players[GetInd[obj->id]]->used, HAS_PHASING_DEVICE);
    } else {
	mi.phased = 0;
    }

    ms.pos.cx = obj->pos.cx;
    ms.pos.cy = obj->pos.cy;
    ms.vel = obj->vel;
    ms.todo.cx = FLOAT_TO_CLICK(ms.vel.x * timeStep2);
    ms.todo.cy = FLOAT_TO_CLICK(ms.vel.y * timeStep2);
    ms.dir = obj->missile_dir;
    ms.mip = &mi;

    for (;;) {
	Move_segment_old(&ms);
	if (!(ms.done.cx | ms.done.cy)) {
	    pos_update |= (ms.crash | ms.bounce);
	    if (ms.crash) {
		break;
	    }
	    if (ms.bounce && ms.bounce != BounceEdge) {
		if (obj->type != OBJ_BALL)
		    obj->life = (long)(obj->life * objectWallBounceLifeFactor);
		if (obj->life <= 0) {
		    break;
		}
		/*
		 * Any bouncing sparks are no longer owner immune to give
		 * "reactive" thrust.  This is exactly like ground effect
		 * in the real world.  Very useful for stopping against walls.
		 *
		 * If the FROMBOUNCE bit is set the spark was caused by
		 * the player bouncing of a wall and thus although the spark
		 * should bounce, it is not reactive thrust otherwise wall
		 * bouncing would cause acceleration of the player.
		 */
		if (!BIT(obj->status, FROMBOUNCE) && BIT(obj->type, OBJ_SPARK))
		    CLR_BIT(obj->status, OWNERIMMUNE);
		if (sqr(ms.vel.x) + sqr(ms.vel.y) > sqr(maxObjectWallBounceSpeed)) {
		    obj->life = 0;
		    break;
		}
		ms.vel.x *= objectWallBrakeFactor;
		ms.vel.y *= objectWallBrakeFactor;
		ms.todo.cx = (int)(ms.todo.cx * objectWallBrakeFactor);
		ms.todo.cy = (int)(ms.todo.cy * objectWallBrakeFactor);
	    }
	    if (++nothing_done >= 5) {
		ms.crash = CrashUnknown;
		break;
	    }
	} else {
	    ms.pos.cx += ms.done.cx;
	    ms.pos.cy += ms.done.cy;
	    nothing_done = 0;
	}
	if (!(ms.todo.cx | ms.todo.cy)) {
	    break;
	}
    }
    if (mi.edge_wrap) {
	if (ms.pos.cx < 0) {
	    ms.pos.cx += mp.click_width;
	}
	if (ms.pos.cx >= mp.click_width) {
	    ms.pos.cx -= mp.click_width;
	}
	if (ms.pos.cy < 0) {
	    ms.pos.cy += mp.click_height;
	}
	if (ms.pos.cy >= mp.click_height) {
	    ms.pos.cy -= mp.click_height;
	}
    }
    Object_position_set_clicks(obj, ms.pos.cx, ms.pos.cy);
    obj->vel = ms.vel;
    obj->missile_dir = ms.dir;
    if (ms.crash) {
	Object_crash_old(&ms);
    }
    if (pos_update) {
	Object_position_remember(obj);
    }
    Cell_add_object(obj);
}


static void Player_crash(player *pl, struct move *move, int crashtype,
			 int item_id, int pt)
{
    int			ind = GetInd[pl->id];
    const char		*howfmt = NULL;
    const char          *hudmsg = NULL;

    msg[0] = '\0';

    switch (crashtype) {

    default:
    case NotACrash:
	warn("Unrecognized crash %d", crashtype);
	break;

    case CrashWormHole:
	SET_BIT(pl->status, WARPING);
	pl->wormHoleHit = item_id;
	break;

    case CrashWall:
	howfmt = "%s crashed%s against a wall";
	hudmsg = "[Wall]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashWallSpeed:
	howfmt = "%s smashed%s against a wall";
	hudmsg = "[Wall]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashWallNoFuel:
	howfmt = "%s smacked%s against a wall";
	hudmsg = "[Wall]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashWallAngle:
	howfmt = "%s was trashed%s against a wall";
	hudmsg = "[Wall]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashTarget:
	howfmt = "%s smashed%s against a target";
	hudmsg = "[Target]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	/*Object_hits_target_old(ms, -1);*/
	Object_hits_target(&World.targets[item_id], (object *)pl, -1);
	break;

    case CrashTreasure:
	howfmt = "%s smashed%s against a treasure";
	hudmsg = "[Treasure]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashCannon:
	if (BIT(pl->used, HAS_SHIELD|HAS_EMERGENCY_SHIELD)
	    != (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
	    howfmt = "%s smashed%s against a cannon";
	    hudmsg = "[Cannon]";
	    sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_CANNON_SOUND);
	}
	if (!BIT(World.cannon[item_id].used, HAS_EMERGENCY_SHIELD)) {
	    /*Cannon_dies_old(ms);*/
	    Cannon_dies(item_id, pl);
	}
	break;

    case CrashUniverse:
	howfmt = "%s left the known universe%s";
	hudmsg = "[Universe]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;

    case CrashUnknown:
	howfmt = "%s slammed%s into a programming error";
	hudmsg = "[Bug]";
	sound_play_sensors(pl->pos.cx, pl->pos.cy, PLAYER_HIT_WALL_SOUND);
	break;
    }

    if (howfmt && hudmsg) {
	player		*pushers[MAX_RECORDED_SHOVES];
	int		cnt[MAX_RECORDED_SHOVES];
	int		num_pushers = 0;
	int		total_pusher_count = 0;
	DFLOAT		total_pusher_score = 0;
	int		i, j;
	DFLOAT		sc;

	SET_BIT(pl->status, KILLED);
	move->delta.cx = 0;
	move->delta.cy = 0;
	sprintf(msg, howfmt, pl->name, (!pt) ? " head first" : "");

	/* get a list of who pushed me */
	for (i = 0; i < MAX_RECORDED_SHOVES; i++) {
	    shove_t *shove = &pl->shove_record[i];
	    if (shove->pusher_id == NO_ID) {
		continue;
	    }
	    if (shove->time < frame_loops - 20) {
		continue;
	    }
	    for (j = 0; j < num_pushers; j++) {
		if (shove->pusher_id == pushers[j]->id) {
		    cnt[j]++;
		    break;
		}
	    }
	    if (j == num_pushers) {
		pushers[num_pushers++] = Players[GetInd[shove->pusher_id]];
		cnt[j] = 1;
	    }
	    total_pusher_count++;
	    total_pusher_score += pushers[j]->score;
	}
	if (num_pushers == 0) {
	    sc = Rate(WALL_SCORE, pl->score);
	    SCORE(ind, -sc, pl->pos.cx, pl->pos.cy, hudmsg);
	    strcat(msg, ".");
	    Set_message(msg);
	}
	else {
	    int		msg_len = strlen(msg);
	    char	*msg_ptr = &msg[msg_len];
	    int		average_pusher_score = total_pusher_score
						/ total_pusher_count;

	    for (i = 0; i < num_pushers; i++) {
		player		*pusher = pushers[i];
		const char	*sep = (!i) ? " with help from "
					    : (i < num_pushers - 1) ? ", "
					    : " and ";
		int		sep_len = strlen(sep);
		int		name_len = strlen(pusher->name);

		if (msg_len + sep_len + name_len + 2 < sizeof msg) {
		    strcpy(msg_ptr, sep);
		    msg_len += sep_len;
		    msg_ptr += sep_len;
		    strcpy(msg_ptr, pusher->name);
		    msg_len += name_len;
		    msg_ptr += name_len;
		}
		sc = cnt[i] * Rate(pusher->score, pl->score)
				    * shoveKillScoreMult / total_pusher_count;
		SCORE(GetInd[pusher->id], sc,
		      pl->pos.cx, pl->pos.cy, pl->name);
		if (i >= num_pushers - 1) {
		    pusher->kills++;
		}

	    }
	    sc = Rate(average_pusher_score, pl->score)
		       * shoveKillScoreMult;
	    SCORE(ind, -sc, pl->pos.cx, pl->pos.cy, "[Shove]");

	    strcpy(msg_ptr, ".");
	    Set_message(msg);

	    /* Robots will declare war on anyone who shoves them. */
	    i = (int)(rfrac() * num_pushers);
	    Robot_war(ind, GetInd[pushers[i]->id]);
	}
    }

    if (BIT(pl->status, KILLED)
	&& pl->score < 0
	&& IS_ROBOT_PTR(pl)) {
	pl->home_base = 0;
	Pick_startpos(ind);
    }
}

static void Player_crash_old(move_state_t *ms, int pt)
{
    player *pl = ms->mip->pl;
    int crashtype = ms->crash;
    int item_id = -1;
    struct move move;

    switch (crashtype) {
    default:
	break;
    case CrashWormHole:
	item_id = ms->wormhole;
	break;
    case CrashCannon:
	item_id = ms->cannon;
	break;
    case CrashTarget:
	item_id = ms->target;
	break;
    }

    Player_crash(pl, &move, crashtype, item_id, pt);
}



static void Move_player_old(int ind)
{
    player		*pl = Players[ind];
    int			nothing_done = 0;
    int			i;
    int			dist;
    move_info_t		mi;
    move_state_t	ms[RES];
    int			worst = 0;
    int			crash;
    int			bounce;
    int			moves_made = 0;
    int			cor_res;
    clpos		pos;
    clvec		todo;
    clvec		done;
    vector		vel;
    vector		r[RES];
    ivec		sign;		/* sign (-1 or 1) of direction */
    ipos		block;		/* block index */
    bool		pos_update = false;
    DFLOAT		fric;
    DFLOAT		oldvx, oldvy;


    if (BIT(pl->status, PLAYING|PAUSE|GAME_OVER|KILLED) != PLAYING) {
	if (!BIT(pl->status, KILLED|PAUSE)) {
	    pos.cx = pl->pos.cx + FLOAT_TO_CLICK(pl->vel.x * timeStep2);
	    pos.cy = pl->pos.cy + FLOAT_TO_CLICK(pl->vel.y * timeStep2);
	    pos.cx = WRAP_XCLICK(pos.cx);
	    pos.cy = WRAP_YCLICK(pos.cy);
	    if (pos.cx != pl->pos.cx || pos.cy != pl->pos.cy) {
		Player_position_remember(pl);
		Player_position_set_clicks(pl, pos.cx, pos.cy);
	    }
	}
	pl->velocity = VECTOR_LENGTH(pl->vel);
	return;
    }

/* Figure out which friction to use. */
    if (BIT(pl->used, HAS_PHASING_DEVICE)) {
	fric = friction;
    }
    else {
	switch (World.block[pl->pos.bx][pl->pos.by]) {
	case FRICTION:
	    fric = blockFriction;
	    break;
	default:
	    fric = friction;
	    break;
	}
    }

    cor_res = MOD2(coriolis * RES / 360, RES);
    oldvx = pl->vel.x;
    oldvy = pl->vel.y;
    pl->vel.x = (1.0f - fric) * (oldvx * tcos(cor_res) + oldvy * tsin(cor_res));
    pl->vel.y = (1.0f - fric) * (oldvy * tcos(cor_res) - oldvx * tsin(cor_res));

    Player_position_remember(pl);

    dist = walldist[pl->pos.bx][pl->pos.by];
    if (dist > 3) {
	int max = ((dist - 3) * BLOCK_SZ) >> 1;
	if (max >= pl->velocity) {
	    pos.cx = pl->pos.cx + FLOAT_TO_CLICK(pl->vel.x * timeStep2);
	    pos.cy = pl->pos.cy + FLOAT_TO_CLICK(pl->vel.y * timeStep2);
	    pos.cx = WRAP_XCLICK(pos.cx);
	    pos.cy = WRAP_YCLICK(pos.cy);
	    Player_position_set_clicks(pl, pos.cx, pos.cy);
	    pl->velocity = VECTOR_LENGTH(pl->vel);
	    return;
	}
    }

    mi.pl = pl;
    mi.obj = (object *) pl;
    mi.edge_wrap = BIT(World.rules->mode, WRAP_PLAY);
    mi.edge_bounce = edgeBounce;
    mi.wall_bounce = true;
    mi.cannon_crashes = true;
    mi.treasure_crashes = true;
    mi.target_crashes = true;
    mi.wormhole_warps = true;
    mi.phased = BIT(pl->used, HAS_PHASING_DEVICE);

    vel = pl->vel;
    todo.cx = FLOAT_TO_CLICK(vel.x * timeStep2);
    todo.cy = FLOAT_TO_CLICK(vel.y * timeStep2);
    for (i = 0; i < pl->ship->num_points; i++) {
	ms[i].pos.cx = pl->pos.cx + pl->ship->pts[i][pl->dir].cx;
	ms[i].pos.cy = pl->pos.cy + pl->ship->pts[i][pl->dir].cy;
	ms[i].vel = vel;
	ms[i].todo = todo;
	ms[i].dir = pl->dir;
	ms[i].mip = &mi;
	ms[i].target = -1;
    }

    for (;; moves_made++) {

	pos.cx = ms[0].pos.cx - pl->ship->pts[0][ms[0].dir].cx;
	pos.cy = ms[0].pos.cy - pl->ship->pts[0][ms[0].dir].cy;
	pos.cx = WRAP_XCLICK(pos.cx);
	pos.cy = WRAP_YCLICK(pos.cy);
	block.x = pos.cx / BLOCK_CLICKS;
	block.y = pos.cy / BLOCK_CLICKS;

	if (walldist[block.x][block.y] > 3) {
	    int maxcl = ((walldist[block.x][block.y] - 3) * BLOCK_CLICKS) >> 1;
	    todo = ms[0].todo;
	    sign.x = (todo.cx < 0) ? -1 : 1;
	    sign.y = (todo.cy < 0) ? -1 : 1;
	    if (maxcl >= sign.x * todo.cx && maxcl >= sign.y * todo.cy) {
		/* entire movement is possible. */
		done.cx = todo.cx;
		done.cy = todo.cy;
	    }
	    else if (sign.x * todo.cx > sign.y * todo.cy) {
		/* horizontal movement. */
		done.cx = sign.x * maxcl;
		done.cy = todo.cy * maxcl / (sign.x * todo.cx);
	    }
	    else {
		/* vertical movement. */
		done.cx = todo.cx * maxcl / (sign.y * todo.cy);
		done.cy = sign.y * maxcl;
	    }
	    todo.cx -= done.cx;
	    todo.cy -= done.cy;
	    for (i = 0; i < pl->ship->num_points; i++) {
		ms[i].pos.cx += done.cx;
		ms[i].pos.cy += done.cy;
		ms[i].todo = todo;
		ms[i].crash = NotACrash;
		ms[i].bounce = NotABounce;
		if (mi.edge_wrap) {
		    if (ms[i].pos.cx < 0) {
			ms[i].pos.cx += mp.click_width;
		    }
		    else if (ms[i].pos.cx >= mp.click_width) {
			ms[i].pos.cx -= mp.click_width;
		    }
		    if (ms[i].pos.cy < 0) {
			ms[i].pos.cy += mp.click_height;
		    }
		    else if (ms[i].pos.cy >= mp.click_height) {
			ms[i].pos.cy -= mp.click_height;
		    }
		}
	    }
	    nothing_done = 0;
	    if (!(todo.cx | todo.cy)) {
		break;
	    }
	    else {
		continue;
	    }
	}

	bounce = -1;
	crash = -1;
	for (i = 0; i < pl->ship->num_points; i++) {
	    Move_segment_old(&ms[i]);
	    pos_update |= (ms[i].crash | ms[i].bounce);
	    if (ms[i].crash) {
		crash = i;
		break;
	    }
	    if (ms[i].bounce) {
		if (bounce == -1) {
		    bounce = i;
		}
		else if (ms[bounce].bounce != BounceEdge
		    && ms[i].bounce == BounceEdge) {
		    bounce = i;
		}
		else if ((ms[bounce].bounce == BounceEdge)
		    == (ms[i].bounce == BounceEdge)) {
		    if ((int)(rfrac() * (pl->ship->num_points - bounce)) == i) {
			bounce = i;
		    }
		}
		worst = bounce;
	    }
	}
	if (crash != -1) {
	    worst = crash;
	    break;
	}
	else if (bounce != -1) {
	    worst = bounce;
	    pl->last_wall_touch = frame_loops;
	    if (ms[worst].bounce != BounceEdge) {
		DFLOAT	speed = VECTOR_LENGTH(ms[worst].vel);
		int	v = (int) speed >> 2;
		int	m = (int) (pl->mass - pl->emptymass * 0.75f);
		DFLOAT	b = 1 - 0.5f * playerWallBrakeFactor;
		long	cost = (long) (b * m * v);
		int	delta_dir,
			abs_delta_dir,
			wall_dir;
		DFLOAT	max_speed = BIT(pl->used, HAS_SHIELD)
				    ? maxShieldedWallBounceSpeed
				    : maxUnshieldedWallBounceSpeed;
		int	max_angle = BIT(pl->used, HAS_SHIELD)
				    ? mp.max_shielded_angle
				    : mp.max_unshielded_angle;

		if (BIT(pl->used, (HAS_SHIELD|HAS_EMERGENCY_SHIELD))
		    == (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
		    if (max_speed < 100) {
			max_speed = 100;
		    }
		    max_angle = RES;
		}

		ms[worst].vel.x *= playerWallBrakeFactor;
		ms[worst].vel.y *= playerWallBrakeFactor;
		ms[worst].todo.cx = (int)(ms[worst].todo.cx * playerWallBrakeFactor);
		ms[worst].todo.cy = (int)(ms[worst].todo.cy * playerWallBrakeFactor);

		/* only use armor if neccessary */
		if (speed > max_speed
		    && max_speed < maxShieldedWallBounceSpeed
		    && !BIT(pl->used, HAS_SHIELD)
		    && BIT(pl->have, HAS_ARMOR)) {
		    max_speed = maxShieldedWallBounceSpeed;
		    max_angle = mp.max_shielded_angle;
		    Player_hit_armor(ind);
		}

		if (speed > max_speed) {
		    crash = worst;
		    ms[worst].crash = (ms[worst].target >= 0 ? CrashTarget :
				       CrashWallSpeed);
		    break;
		}

		switch (ms[worst].bounce) {
		case BounceHorLo: wall_dir = 4*RES/8; break;
		case BounceHorHi: wall_dir = 0*RES/8; break;
		case BounceVerLo: wall_dir = 6*RES/8; break;
		default:
		case BounceVerHi: wall_dir = 2*RES/8; break;
		case BounceLeftDown: wall_dir = 1*RES/8; break;
		case BounceLeftUp: wall_dir = 7*RES/8; break;
		case BounceRightDown: wall_dir = 3*RES/8; break;
		case BounceRightUp: wall_dir = 5*RES/8; break;
		}
		if (pl->dir >= wall_dir) {
		    delta_dir = (pl->dir - wall_dir <= RES/2)
				? -(pl->dir - wall_dir)
				: (wall_dir + RES - pl->dir);
		} else {
		    delta_dir = (wall_dir - pl->dir <= RES/2)
				? (wall_dir - pl->dir)
				: -(pl->dir + RES - wall_dir);
		}
		abs_delta_dir = ABS(delta_dir);
		/* only use armor if neccessary */
		if (abs_delta_dir > max_angle
		    && max_angle < mp.max_shielded_angle
		    && !BIT(pl->used, HAS_SHIELD)
		    && BIT(pl->have, HAS_ARMOR)) {
		    max_speed = maxShieldedWallBounceSpeed;
		    max_angle = mp.max_shielded_angle;
		    Player_hit_armor(ind);
		}
		if (abs_delta_dir > max_angle) {
		    crash = worst;
		    ms[worst].crash = (ms[worst].target >= 0 ? CrashTarget :
				       CrashWallAngle);
		    break;
		}
		if (abs_delta_dir <= RES/16) {
		    pl->float_dir += (1.0f - playerWallBrakeFactor) * delta_dir;
		    if (pl->float_dir >= RES) {
			pl->float_dir -= RES;
		    }
		    else if (pl->float_dir < 0) {
			pl->float_dir += RES;
		    }
		}

		/*
		 * Small explosion and fuel loss if survived a hit on a wall.
		 * This doesn't affect the player as the explosion is sparks
		 * which don't collide with player.
		 * Clumsy touches (head first) with wall are more costly.
		 */
		cost = (cost * (RES/2 + abs_delta_dir)) / RES;
		if (BIT(pl->used, (HAS_SHIELD|HAS_EMERGENCY_SHIELD))
		    != (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
		    Add_fuel(&pl->fuel, (long)(-((cost << FUEL_SCALE_BITS)
					  * wallBounceFuelDrainMult)));
		    Item_damage(ind, wallBounceDestroyItemProb);
		}
		if (!pl->fuel.sum && wallBounceFuelDrainMult != 0) {
		    crash = worst;
		    ms[worst].crash = (ms[worst].target >= 0 ? CrashTarget :
				       CrashWallNoFuel);
		    break;
		}
		if (cost) {
		    int intensity = (int)(cost * wallBounceExplosionMult);
		    Make_debris(
			/* pos.cx, pos.cy   */ pl->pos.cx, pl->pos.cy,
			/* vel.x, vel.y   */ pl->vel.x, pl->vel.y,
			/* owner id       */ pl->id,
			/* owner team	  */ pl->team,
			/* kind           */ OBJ_SPARK,
			/* mass           */ 3.5,
			/* status         */ GRAVITY | OWNERIMMUNE | FROMBOUNCE,
			/* color          */ RED,
			/* radius         */ 1,
			/* num debris     */ (intensity>>1) + ((intensity>>1) * rfrac()),
			/* min,max dir    */ wall_dir - (RES/4), wall_dir + (RES/4),
			/* min,max speed  */ 20, 20 + (intensity>>2),
			/* min,max life   */ 10 * TIME_FACT,
			(10 + (intensity >> 1)) * TIME_FACT
			);
		    sound_play_sensors(pl->pos.cx, pl->pos.cy,
				       PLAYER_BOUNCED_SOUND);
		    if (ms[worst].target >= 0) {
			cost <<= FUEL_SCALE_BITS;
			cost = (long)(cost * (wallBounceFuelDrainMult / 4.0));
			Object_hits_target_old(&ms[worst], cost);
		    }
		}
	    }
	}
	else {
	    for (i = 0; i < pl->ship->num_points; i++) {
		r[i].x = (vel.x) ? (DFLOAT) ms[i].todo.cx / vel.x : 0;
		r[i].y = (vel.y) ? (DFLOAT) ms[i].todo.cy / vel.y : 0;
		r[i].x = ABS(r[i].x);
		r[i].y = ABS(r[i].y);
	    }
	    worst = 0;
	    for (i = 1; i < pl->ship->num_points; i++) {
		if (r[i].x > r[worst].x || r[i].y > r[worst].y) {
		    worst = i;
		}
	    }
	}

	if (!(ms[worst].done.cx | ms[worst].done.cy)) {
	    if (++nothing_done >= 5) {
		ms[worst].crash = CrashUnknown;
		break;
	    }
	} else {
	    nothing_done = 0;
	    ms[worst].pos.cx += ms[worst].done.cx;
	    ms[worst].pos.cy += ms[worst].done.cy;
	}
	if (!(ms[worst].todo.cx | ms[worst].todo.cy)) {
	    break;
	}

	vel = ms[worst].vel;
	for (i = 0; i < pl->ship->num_points; i++) {
	    if (i != worst) {
		ms[i].pos.cx += ms[worst].done.cx;
		ms[i].pos.cy += ms[worst].done.cy;
		ms[i].vel = vel;
		ms[i].todo = ms[worst].todo;
		ms[i].dir = ms[worst].dir;
	    }
	}
    }

    pos.cx = ms[worst].pos.cx - pl->ship->pts[worst][pl->dir].cx;
    pos.cy = ms[worst].pos.cy - pl->ship->pts[worst][pl->dir].cy;
    pos.cx = WRAP_XCLICK(pos.cx);
    pos.cy = WRAP_YCLICK(pos.cy);
    Player_position_set_clicks(pl, pos.cx, pos.cy);
    pl->vel = ms[worst].vel;
    pl->velocity = VECTOR_LENGTH(pl->vel);

    if (ms[worst].crash) {
	Player_crash_old(&ms[worst], worst /*, false*/);
    }
    if (pos_update) {
	Player_position_remember(pl);
    }
}

static void Turn_player_old(int ind)
{
    player		*pl = Players[ind];
    int			i;
    move_info_t		mi;
    move_state_t	ms[RES];
    int			dir;
    int			new_dir = MOD2((int)(pl->float_dir + 0.5f), RES);
    int			sign;
    int			crash = -1;
    int			nothing_done = 0;
    int			turns_done = 0;
    int			blocked = 0;
    clpos		pos;
    vector		salt;

    if (new_dir == pl->dir) {
	return;
    }
    if (BIT(pl->status, PLAYING|PAUSE|GAME_OVER|KILLED) != PLAYING) {
	pl->dir = new_dir;
	return;
    }

    if (walldist[pl->pos.bx][pl->pos.by] > 2) {
	pl->dir = new_dir;
	return;
    }

    mi.pl = pl;
    mi.obj = (object *) pl;
    mi.edge_wrap = BIT(World.rules->mode, WRAP_PLAY);
    mi.edge_bounce = edgeBounce;
    mi.wall_bounce = true;
    mi.cannon_crashes = true;
    mi.treasure_crashes = true;
    mi.target_crashes = true;
    mi.wormhole_warps = false;
    mi.phased = BIT(pl->used, HAS_PHASING_DEVICE);

    if (new_dir > pl->dir) {
	sign = (new_dir - pl->dir <= RES + pl->dir - new_dir) ? 1 : -1;
    }
    else {
	sign = (pl->dir - new_dir <= RES + new_dir - pl->dir) ? -1 : 1;
    }

#if 0
    salt.x = (pl->vel.x > 0) ? 0.1f : (pl->vel.x < 0) ? -0.1f : 0;
    salt.y = (pl->vel.y > 0) ? 0.1f : (pl->vel.y < 0) ? -0.1f : 0;
#else
    salt.x = (pl->vel.x > 0) ? 1e-6f : (pl->vel.x < 0) ? -1e-6f : 0;
    salt.y = (pl->vel.y > 0) ? 1e-6f : (pl->vel.y < 0) ? -1e-6f : 0;
#endif

    pos.cx = pl->pos.cx;
    pos.cy = pl->pos.cy;
    for (; pl->dir != new_dir; turns_done++) {
	dir = MOD2(pl->dir + sign, RES);
	if (!mi.edge_wrap) {
	    if (pos.cx <= 22 * CLICK) {
		for (i = 0; i < pl->ship->num_points; i++) {
		    if (pos.cx + pl->ship->pts[i][dir].cx < 0) {
			pos.cx = -pl->ship->pts[i][dir].cx;
		    }
		}
	    }
	    if (pos.cx >= mp.click_width - 22 * CLICK) {
		for (i = 0; i < pl->ship->num_points; i++) {
		    if (pos.cx + pl->ship->pts[i][dir].cx
			>= mp.click_width) {
			pos.cx = mp.click_width - 1
			       - pl->ship->pts[i][dir].cx;
		    }
		}
	    }
	    if (pos.cy <= 22 * CLICK) {
		for (i = 0; i < pl->ship->num_points; i++) {
		    if (pos.cy + pl->ship->pts[i][dir].cy < 0) {
			pos.cy = -pl->ship->pts[i][dir].cy;
		    }
		}
	    }
	    if (pos.cy >= mp.click_height - 22 * CLICK) {
		for (i = 0; i < pl->ship->num_points; i++) {
		    if (pos.cy + pl->ship->pts[i][dir].cy
			>= mp.click_height) {
			pos.cy = mp.click_height - 1
			       - pl->ship->pts[i][dir].cy;
		    }
		}
	    }
	    if (pos.cx != pl->pos.cx || pos.cy != pl->pos.cy) {
		Player_position_set_clicks(pl, pos.cx, pos.cy);
	    }
	}

	for (i = 0; i < pl->ship->num_points; i++) {
	    ms[i].mip = &mi;
	    ms[i].pos.cx = pos.cx + pl->ship->pts[i][pl->dir].cx;
	    ms[i].pos.cy = pos.cy + pl->ship->pts[i][pl->dir].cy;
	    ms[i].todo.cx = pos.cx + pl->ship->pts[i][dir].cx - ms[i].pos.cx;
	    ms[i].todo.cy = pos.cy + pl->ship->pts[i][dir].cy - ms[i].pos.cy;
	    ms[i].vel.x = ms[i].todo.cx + salt.x;
	    ms[i].vel.y = ms[i].todo.cy + salt.y;
	    ms[i].target = -1;

	    do {
		Move_segment_old(&ms[i]);
		if (ms[i].crash | ms[i].bounce) {
		    if (ms[i].crash) {
			if (ms[i].crash != CrashUniverse) {
			    crash = i;
			}
			blocked = 1;
			break;
		    }
		    if (ms[i].bounce != BounceEdge) {
			blocked = 1;
			break;
		    }
		    if (++nothing_done >= 5) {
			ms[i].crash = CrashUnknown;
			crash = i;
			blocked = 1;
			break;
		    }
		}
		else if (ms[i].done.cx | ms[i].done.cy) {
		    ms[i].pos.cx += ms[i].done.cx;
		    ms[i].pos.cy += ms[i].done.cy;
		    nothing_done = 0;
		}
	    } while (ms[i].todo.cx | ms[i].todo.cy);
	    if (blocked) {
		break;
	    }
	}
	if (blocked) {
	    break;
	}
	pl->dir = dir;
    }

    if (blocked) {
	/* kps - Mara "turnqueue" type hack  */
	/*pl->float_dir = (DFLOAT) pl->dir;*/
	pl->last_wall_touch = frame_loops;
    }

    if (crash != -1) {
	Player_crash_old(&ms[crash], crash /*, true*/);
    }

}



static char msg[MSG_LEN];

static void *ralloc(void *ptr, size_t size)
{
    if (!(ptr = realloc(ptr, size))) {
	warn("Realloc failed.");
	exit(1);
    }
    return ptr;
}

static DFLOAT wallBounceExplosionMult;

static unsigned short *Shape_lines(const shipobj *shape, int dir)
{
    int p;
    static unsigned short foo[100];
    static const shipobj *lastshape;
    static int lastdir;
    const int os = linec;

    /* linet[i].group MUST BE INITIALIZED TO 0 */

    if (shape == lastshape && dir == lastdir)
	return foo;

    lastshape = shape;
    lastdir = dir;

    for (p = 0; p < shape->num_points; p++) {
	linet[p + os].start.cx = -shape->pts[p][dir].cx;
	linet[p + os].start.cy = -shape->pts[p][dir].cy;
    }
    for (p = 0; p < shape->num_points - 1; p++) {
	linet[p + os].delta.cx
	    = linet[p + os + 1].start.cx - linet[p + os].start.cx;
	linet[p + os].delta.cy
	    = linet[p + os + 1].start.cy - linet[p + os].start.cy;
    }
    linet[p + os].delta.cx = linet[os].start.cx - linet[p + os].start.cx;
    linet[p + os].delta.cy = linet[os].start.cy - linet[p + os].start.cy;
    for (p = 0; p < shape->num_points; p++)
	foo[p] = p + os;
    foo[p] = 65535;
    return foo;
}


static int Bounce_object(object *obj, struct move *move, int line, int point)
{
    DFLOAT fx, fy;
    DFLOAT c, s;
    int group, type, item_id;

    group = linet[line >= linec ? point : line].group;
    type = groups[group].type;
    item_id = groups[group].item_id;

    if (obj->collmode == 1) {
	fx = ABS(obj->vel.x) + ABS(obj->vel.y);
	/* If fx<1, there is practically no movement. Object
	   collision detection can ignore the bounce. */
	if (fx > 1) {
	    obj->wall_time = 1 -
		CLICK_TO_FLOAT(ABS(move->delta.cx) + ABS(move->delta.cy)) / fx;
	    obj->collmode = 2;
	}
    }

    if (type == TREASURE) {
	if (obj->type == OBJ_BALL)
	    Ball_hits_goal(BALL_PTR(obj), group);
	obj->life = 0;
	return 0;
    }

    /* kps hack */
    if (type == TARGET) {
	Object_hits_target(&World.targets[item_id], obj, -1);
	return 0;
    }
    /* kps hack */
#if 0
    /* kps hack */
    if (type == WORMHOLE) {
	/* kps - ??? */
	/*Object_crash(obj, move, CrashWormHole, ???  );*/
	return 0;
    }
    /* kps hack */
#endif
    if (!BIT(mp.obj_bounce_mask, obj->type)) {
	obj->life = 0;
	return 0;
    }

    if (obj->type != OBJ_BALL) {
	obj->life = (long)(obj->life * objectWallBounceLifeFactor);
	if (obj->life <= 0)
	    return 0;
    }
    /*
     * Any bouncing sparks are no longer owner immune to give
     * "reactive" thrust.  This is exactly like ground effect
     * in the real world.  Very useful for stopping against walls.
     *
     * If the FROMBOUNCE bit is set the spark was caused by
     * the player bouncing of a wall and thus although the spark
     * should bounce, it is not reactive thrust otherwise wall
     * bouncing would cause acceleration of the player.
     */
    if (sqr(obj->vel.x) + sqr(obj->vel.y) > sqr(maxObjectWallBounceSpeed)){
	obj->life = 0;
	return 0;
    }
    if (!BIT(obj->status, FROMBOUNCE) && BIT(obj->type, OBJ_SPARK))
	CLR_BIT(obj->status, OWNERIMMUNE);


    if (line >= linec) {
	DFLOAT x, y, l2;
	x = linet[line].delta.cx;
	y = linet[line].delta.cy;
	l2 = (x*x + y*y);
	c = (x*x - y*y) / l2;
	s = 2*x*y / l2;
    }
    else {
	c = linet[line].c;
	s = linet[line].s;
    }
    fx = move->delta.cx * c + move->delta.cy * s;
    fy = move->delta.cx * s - move->delta.cy * c;
    move->delta.cx = fx * objectWallBrakeFactor;
    move->delta.cy = fy * objectWallBrakeFactor;
    fx = obj->vel.x * c + obj->vel.y * s;
    fy = obj->vel.x * s - obj->vel.y * c;
    obj->vel.x = fx * objectWallBrakeFactor;
    obj->vel.y = fy * objectWallBrakeFactor;
    if (obj->collmode == 2)
	obj->collmode = 3;
    return 1;
}



static void Bounce_player(player *pl, struct move *move, int line, int point)
{
    DFLOAT fx, fy;
    DFLOAT c, s;
    int group, type, item_id;

    if (line >= linec) {
	DFLOAT x, y, l2;
	x = linet[line].delta.cx;
	y = linet[line].delta.cy;
	l2 = (x*x + y*y);
	c = (x*x - y*y) / l2;
	s = 2*x*y / l2;
	group = linet[point].group;
    }
    else {
	group = linet[line].group;
	c = linet[line].c;
	s = linet[line].s;
    }
    type = groups[group].type;
    item_id = groups[group].item_id;
    if (type == TREASURE) {
	Player_crash(pl, move, CrashTreasure, NO_ID, 1);
	return;
    }
    /* kps hack */
    if (type == TARGET) {
	/*Object_hits_target(&World.targets[item_id], obj, -1);*/
	Player_crash(pl, move, CrashTarget, item_id, 1);
	return;
    }
    /* kps hack */

    /* kps hack */
    if (type == WORMHOLE) {
	Player_crash(pl, move, CrashWormHole, item_id, 1);
	return;
    }
    /* kps hack */

    pl->last_wall_touch = frame_loops;
    {
	DFLOAT	speed = VECTOR_LENGTH(pl->vel);
	int	v = (int) speed >> 2;
	int	m = (int) (pl->mass - pl->emptymass * 0.75f);
	DFLOAT	b = 1 - 0.5f * playerWallBrakeFactor;
	long	cost = (long) (b * m * v);
	DFLOAT	max_speed = BIT(pl->used, HAS_SHIELD)
		? maxShieldedWallBounceSpeed
		: maxUnshieldedWallBounceSpeed;

	if (BIT(pl->used, (HAS_SHIELD|HAS_EMERGENCY_SHIELD))
	    == (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
	    max_speed = 100;
	}
	if (speed > max_speed) {
	    Player_crash(pl, move, CrashWallSpeed, NO_ID, 1);
	    return;
	}

	/*
	 * Small explosion and fuel loss if survived a hit on a wall.
	 * This doesn't affect the player as the explosion is sparks
	 * which don't collide with player.
	 */
	cost *= 0.9; /* used to depend on bounce angle, .5 .. 1.0 */
	if (BIT(pl->used, (HAS_SHIELD|HAS_EMERGENCY_SHIELD))
	    != (HAS_SHIELD|HAS_EMERGENCY_SHIELD)) {
	    Add_fuel(&pl->fuel, (long)(-((cost << FUEL_SCALE_BITS)
					 * wallBounceFuelDrainMult)));
	    Item_damage(GetInd[pl->id], wallBounceDestroyItemProb);
	}
	if (!pl->fuel.sum && wallBounceFuelDrainMult != 0) {
	    Player_crash(pl, move, CrashWallNoFuel, NO_ID, 1);
	    return;
	}
/* !@# I didn't implement wall direction calculation yet. */
	if (cost) {
#if 0
	    int intensity = (int)(cost * wallBounceExplosionMult);
	    Make_debris(
			/* pos.cx, pos.cy   */ pl->pos.cx, pl->pos.cy,
			/* vel.x, vel.y   */ pl->vel.x, pl->vel.y,
			/* owner id       */ pl->id,
			/* owner team	  */ pl->team,
			/* kind           */ OBJ_SPARK,
			/* mass           */ 3.5,
			/* status         */ GRAVITY | OWNERIMMUNE | FROMBOUNCE,
			/* color          */ RED,
			/* radius         */ 1,
			/* min,max debris */ intensity>>1, intensity,
			/* min,max dir    */ wall_dir - (RES/4), wall_dir + (RES/4),
			/* min,max speed  */ 20, 20 + (intensity>>2),
			/* min,max life   */ 10, 10 + (intensity>>1)
					     );
#endif
	    sound_play_sensors(pl->pos.cx, pl->pos.cy,
			       PLAYER_BOUNCED_SOUND);
#if 0
	    /* I'll leave this here until i implement targets */
	    if (ms[worst].target >= 0) {
		cost <<= FUEL_SCALE_BITS;
		cost = (long)(cost * (wallBounceFuelDrainMult / 4.0));
		Object_hits_target_old(&ms[worst], cost);
	    }
#endif
	}
    }
    fx = move->delta.cx * c + move->delta.cy * s;
    fy = move->delta.cx * s - move->delta.cy * c;
    move->delta.cx = fx * playerWallBrakeFactor;
    move->delta.cy = fy * playerWallBrakeFactor;
    fx = pl->vel.x * c + pl->vel.y * s;
    fy = pl->vel.x * s - pl->vel.y * c;
    pl->vel.x = fx * playerWallBrakeFactor;
    pl->vel.y = fy * playerWallBrakeFactor;
}

static int Away(struct move *move, int line)
{
    int i, dx, dy, lsx, lsy, res;
    unsigned short *lines;

    lsx = linet[line].start.cx - move->start.cx;
    lsy = linet[line].start.cy - move->start.cy;
    lsx = CENTER_XCLICK(lsx);
    lsy = CENTER_YCLICK(lsy);

    if (ABS(linet[line].delta.cx) >= ABS(linet[line].delta.cy)) {
	dx = 0;
	dy = -SIGN(linet[line].delta.cx);
    }
    else {
	dy = 0;
	dx = SIGN(linet[line].delta.cy);
    }

    if ((ABS(lsx) > SEPARATION_DIST || ABS(lsy) > SEPARATION_DIST)
	&& (ABS(lsx + linet[line].delta.cx) > SEPARATION_DIST
	    || ABS(lsy + linet[line].delta.cy) > SEPARATION_DIST)) {
	move->start.cx = WRAP_XCLICK(move->start.cx + dx);
	move->start.cy = WRAP_YCLICK(move->start.cy + dy);
	return -1;
    }

    lines = blockline[(move->start.cx >> B_SHIFT)
		     + mapx * (move->start.cy >> B_SHIFT)].lines;
    while ( (i = *lines++) != 65535) {
	if (i == line)
	    continue;
	if (linet[i].group
	    && (groups[linet[i].group].hit_mask & move->hit_mask))
	    continue;

	lsx = linet[i].start.cx - move->start.cx;
	lsy = linet[i].start.cy - move->start.cy;
	lsx = CENTER_XCLICK(lsx);
	lsy = CENTER_YCLICK(lsy);

	if ((ABS(lsx) > SEPARATION_DIST || ABS(lsy) > SEPARATION_DIST)
	    && (ABS(lsx + linet[i].delta.cx) > SEPARATION_DIST
		|| ABS(lsy + linet[i].delta.cy) > SEPARATION_DIST))
	    continue;

	if (lsx < dx && lsx + linet[i].delta.cx < dx)
	    continue;
	if (lsx > dx && lsx + linet[i].delta.cx > dx)
	    continue;
	if (lsy < dy && lsy + linet[i].delta.cy < dy)
	    continue;
	if (lsy > dy && lsy + linet[i].delta.cy > dy)
	    continue;

	if ((res = SIDE(lsx - dx, lsy - dy, i)) == 0
	    || res > 0 != SIDE(lsx, lsy, i) > 0) {
	    if (res) {
		if (lsx < 0 && lsx + linet[i].delta.cx < 0)
		    continue;
		if (lsx > 0 && lsx + linet[i].delta.cx > 0)
		    continue;
		if (lsy < 0 && lsy + linet[i].delta.cy < 0)
		    continue;
		if (lsy > 0 && lsy + linet[i].delta.cy > 0)
		    continue;
	    }
	    return i;
	}
    }

    move->start.cx = WRAP_XCLICK(move->start.cx + dx);
    move->start.cy = WRAP_YCLICK(move->start.cy + dy);
    return -1;
}

static int Shape_move1(int dx, int dy, struct move *move,
		       const shipobj *shape, int dir, int *line, int *point)
{
    int i, p, lsx, lsy, res;
    unsigned short *lines, *points;
    int block;

    block = (move->start.cx >> B_SHIFT) + mapx * (move->start.cy >> B_SHIFT);
    for (p = 0; p < shape->num_points; p++) {
	lines = blockline[block].lines;
	/* Can use the same block for all points because the block of the
	   center point contains lines for their start & end positions. */
	while ( (i = *lines++) != 65535) {
	    if (linet[i].group
		&& (groups[linet[i].group].hit_mask & move->hit_mask))
		continue;
	    lsx = linet[i].start.cx - move->start.cx - shape->pts[p][dir].cx;
	    lsy = linet[i].start.cy - move->start.cy - shape->pts[p][dir].cy;
	    lsx = CENTER_XCLICK(lsx);
	    lsy = CENTER_YCLICK(lsy);

	    if (lsx < dx && lsx + linet[i].delta.cx < dx)
		continue;
	    if (lsx > dx && lsx + linet[i].delta.cx > dx)
		continue;
	    if (lsy < dy && lsy + linet[i].delta.cy < dy)
		continue;
	    if (lsy > dy && lsy + linet[i].delta.cy > dy)
		continue;

	    if ( (res = SIDE(lsx - dx, lsy - dy, i)) == 0
		 || res > 0 != SIDE(lsx, lsy, i) > 0) {
		if (res) {
		    if (lsx < 0 && lsx + linet[i].delta.cx < 0)
			continue;
		    if (lsx > 0 && lsx + linet[i].delta.cx > 0)
			continue;
		    if (lsy < 0 && lsy + linet[i].delta.cy < 0)
			continue;
		    if (lsy > 0 && lsy + linet[i].delta.cy > 0)
			continue;
		}
		*line = i;
		return 0;
	    }
	}
    }

    points = blockline[block].points;
    while ( (p = *points++) != 65535) {
	if (linet[p].group
	    && (groups[linet[p].group].hit_mask & move->hit_mask))
	    continue;
	lines = Shape_lines(shape, dir);
	while ( (i = *lines++) != 65535) {
	    lsx = linet[i].start.cx + (linet[p].start.cx - move->start.cx);
	    lsy = linet[i].start.cy + (linet[p].start.cy - move->start.cy);
	    lsx = CENTER_XCLICK(lsx);
	    lsy = CENTER_YCLICK(lsy);

	    if (lsx < dx && lsx + linet[i].delta.cx < dx)
		continue;
	    if (lsx > dx && lsx + linet[i].delta.cx > dx)
		continue;
	    if (lsy < dy && lsy + linet[i].delta.cy < dy)
		continue;
	    if (lsy > dy && lsy + linet[i].delta.cy > dy)
		continue;

	    if ( (res = SIDE(lsx - dx, lsy - dy, i)) == 0
		 || res > 0 != SIDE(lsx, lsy, i) > 0) {
		if (res) {
		    if (lsx < 0 && lsx + linet[i].delta.cx < 0)
			continue;
		    if (lsx > 0 && lsx + linet[i].delta.cx > 0)
			continue;
		    if (lsy < 0 && lsy + linet[i].delta.cy < 0)
			continue;
		    if (lsy > 0 && lsy + linet[i].delta.cy > 0)
			continue;
		}
		*line = i;
		*point = p;
		return 0;
	    }
	}
    }

    move->start.cx = WRAP_XCLICK(move->start.cx + dx);
    move->start.cy = WRAP_YCLICK(move->start.cy + dy);
    return 1;
}

static int Shape_away(struct move *move, const shipobj *shape,
		      int dir, int line, int *rline, int *rpoint)
{
    int dx, dy;

    if (ABS(linet[line].delta.cx) >= ABS(linet[line].delta.cy)) {
	dx = 0;
	dy = -SIGN(linet[line].delta.cx);
    }
    else {
	dy = 0;
	dx = SIGN(linet[line].delta.cy);
    }
    return Shape_move1(dx, dy, move, shape, dir, rline, rpoint);
}

static int Lines_check(int msx, int msy, int mdx, int mdy, int *mindone,
		       const unsigned short *lines, int chx, int chy,
		       int chxy, const struct move *move, int *minline,
		       int *height)
{
    int lsx, lsy, ldx, ldy, temp, bigger, start, end, i, x, sy, ey, prod;
    int mbase = mdy >> 1, hit = 0;

    while ( (i = *lines++) != 65535) {
	if (linet[i].group
	    && (groups[linet[i].group].hit_mask & move->hit_mask))
	    continue;
	lsx = linet[i].start.cx;
	lsy = linet[i].start.cy;
	ldx = linet[i].delta.cx;
	ldy = linet[i].delta.cy;

	if (chx) {
	    lsx = -lsx;
	    ldx = -ldx;
	}
	if (chy) {
	    lsy = -lsy;
	    ldy = -ldy;
	}
	if (chxy) {
	    temp = ldx;
	    ldx = ldy;
	    ldy = temp;
	    temp = lsx;
	    lsx = lsy;
	    lsy = temp;
	}
	lsx -= msx;
	lsy -= msy;
	if (chxy) {
	    lsx = CENTER_YCLICK(lsx);
	    lsy = CENTER_XCLICK(lsy);
	}
	else {
	    lsx = CENTER_XCLICK(lsx);
	    lsy = CENTER_YCLICK(lsy);
	}
	if (*height < lsy + (ldy < 0 ? ldy : 0))
	    continue;
	if (0 > lsy + (ldy < 0 ? 0 : ldy))
	    continue;

	lsx = CENTER_XCLICK(lsx);

	if (ldx < 0) {
	    lsx += ldx;
	    ldx = -ldx;
	    lsy += ldy;
	    ldy = -ldy;
	}

	start = MAX(0, lsx);
	end = MIN(*mindone + 1, lsx + ldx);
	if (start > end)
	    continue;

	sy = LINEY(mdx, mdy, mbase, start);
	prod = (start - lsx) * ldy - (sy - lsy) * ldx;

	if (!prod) {
	    if (!ldx && (lsy + (ldy < 0 ? ldy : 0) > sy ||
			 lsy + (ldy < 0 ? 0 : ldy) < sy))
		continue;
	    start--;
	}
	else {
	    bigger = prod > 0;
	    ey = LINEY(mdx, mdy, mbase, end);
	    if ( ABS(prod) >= ldx
		 && ABS( (prod = (end - lsx) * ldy - (ey - lsy) * ldx) ) 
		 >= ldx && prod > 0 == bigger)
		continue;
	    {
		int schs, sche;
		double diff = ((double)(-mbase)/mdx-(double)(lsx)*ldy/ldx+lsy);
		double diff2 = (double)mdy / mdx - (double)ldy / ldx;

		if (ABS(diff2) < 1. / (50000.*50000)) {
		    if (diff > 0 || diff < -1)
			continue;
		    else {
			schs = start + 1;
			sche = end;
		    }
		}
		/* Can this float->int conversion cause overflows?
		 * If so, calculate min/max before conversion. */
		else if (diff2 < 0) {
		    schs = MAX(start + 1, (int) ((diff + 1) / diff2 + .9));
		    sche = MIN(end, (int) (diff / diff2 + 1.1));
		}
		else {
		    schs = MAX(start + 1, (int) (diff / diff2 + .9));
		    sche = MIN(end, (int) ((diff + 1) / diff2 + 1.1));
		}

		for (x = schs; x <= sche; x++)
		    if ( (prod = (x - lsx) * ldy
			  - (LINEY(mdx, mdy, mbase, x) - lsy) * ldx)
			 >= 0 != bigger || prod == 0)
			goto found;
		continue;
	    found:
		start = x - 1;
	    }
	}

	if (start < *mindone
	    || (start == *mindone
		&& *minline != -1
		&& SIDE(move->delta.cx, move->delta.cy, i) < 0)) {
	    hit = 1;
	    *mindone = start;
	    *minline = i;
	    *height = LINEY(mdx, mdy, mbase, start);
	}
    }
    return hit;
}

/* Do not call this with no movement. */
/* May not be called with point already on top of line.
   Maybe I should change that to allow lines that could be crossed. */
static void Move_point(const struct move *move, struct collans *answer)
{
    int minline, mindone, minheight;
    int block;
    int msx = move->start.cx, msy = move->start.cy;
    int mdx = move->delta.cx, mdy = move->delta.cy;
    int mbase;
    int chxy = 0, chx = 0, chy = 0;
    int x, temp;
    unsigned short *lines;

    block = (move->start.cx >> B_SHIFT) + mapx * (move->start.cy >> B_SHIFT);
    x = blockline[block].distance;
    lines = blockline[block].lines;

    if (mdx < 0) {
	mdx = -mdx;
	msx = -msx;
	chx = 1;
    }
    if (mdy < 0) {
	mdy = -mdy;
	msy = -msy;
	chy = 1;
    }
    if (mdx < mdy) {
	temp = mdx;
	mdx = mdy;
	mdy = temp;
	temp = msx;
	msx = msy;
	msy = temp;
	chxy = 1;
    }

    /* 46341*46341 overflows signed 32-bit int */
    if (mdx > 45000) {
      mdy = (float)mdy * 45000 / mdx; /* might overflow without float */
      mdx = 45000;
    }

    mindone = mdx;
    minheight = mdy;
    mdx++;
    mdy++;
    mbase = mdy >> 1;

    if (mindone > x) {
	if (x < MAX_MOVE) {
	    /* !@# change this so that the point always moves away from
	       the current block */
	    temp = msx > 0 ? B_CLICKS - (msx & B_MASK) : (-msx) & B_MASK;
	    temp = MIN(temp,
		       (msy > 0 ? B_CLICKS - (msy & B_MASK) : -msy & B_MASK));
	    x += temp;
	    x = MIN(x, MAX_MOVE);
	}
	if (mindone > x) {
	    mindone = x;
	    minheight = LINEY(mdx, mdy, mbase, mindone);
	}
    }
    minline = -1;

    Lines_check(msx, msy, mdx, mdy, &mindone, lines, chx, chy, chxy,
		move, &minline, &minheight);

    answer->line = minline;
    if (chxy) {
	temp = mindone;
	mindone = minheight;
	minheight = temp;
    }
    if (chx)
	mindone = -mindone;
    if (chy)
	minheight = -minheight;
    answer->moved.cx = mindone;
    answer->moved.cy = minheight;

    return;
}
/* Do not call this with no movement. */
/* May not be called with point already on top of line.
   maybe I should change that to allow lines which could be
   crossed. */
/* This could be sped up by a lot in several ways if needed.
 * For example, there's no need to consider all the points
 * separately if the ship is not close to a wall.
 */
static void Shape_move(const struct move *move, const shipobj *shape,
		       int dir, struct collans *answer)
{
    int minline, mindone, minheight, minpoint;
    int p, block;
    int msx = move->start.cx, msy = move->start.cy;
    int mdx = move->delta.cx, mdy = move->delta.cy;
    int mbase;
    int chxy = 0, chx = 0, chy = 0;
    int x, temp;
    unsigned short *lines;
    unsigned short *points;

    if (mdx < 0) {
	mdx = -mdx;
	chx = 1;
    }
    if (mdy < 0) {
	mdy = -mdy;
	chy = 1;
    }
    if (mdx < mdy) {
	temp = mdx;
	mdx = mdy;
	mdy = temp;
	chxy = 1;
    }

    /* 46341*46341 overflows signed 32-bit int */
    if (mdx > 45000) {
      mdy = (float)mdy * 45000 / mdx; /* might overflow without float */
      mdx = 45000;
    }

    mindone = mdx;
    minheight = mdy;

    mdx++;
    mdy++;
    mbase = mdy >> 1;
    minline = -1;
    minpoint = -1;

    for (p = 0; p < shape->num_points; p++) {
	msx = WRAP_XCLICK(move->start.cx + shape->pts[p][dir].cx);
	msy = WRAP_YCLICK(move->start.cy + shape->pts[p][dir].cy);
	block = (msx >> B_SHIFT) + mapx * (msy >> B_SHIFT);
	if (chx)
	    msx = -msx;
	if (chy)
	    msy = -msy;
	if (chxy) {
	    temp = msx;
	    msx = msy;
	    msy = temp;
	}

	x = blockline[block].distance;
	lines = blockline[block].lines;

	if (mindone > x) {
	    if (x < MAX_MOVE) {
		temp = msx > 0 ? B_CLICKS - (msx & B_MASK) : -msx & B_MASK;
		temp = MIN(temp,
			   (msy > 0 ?
			    B_CLICKS - (msy & B_MASK)
			    : -msy & B_MASK));
		x += temp;
		x = MIN(x, MAX_MOVE);
	    }
	    if (mindone > x) {
		mindone = x;
		minheight = LINEY(mdx, mdy, mbase, mindone);
	    }
	}

	if (Lines_check(msx, msy, mdx, mdy, &mindone, lines, chx, chy,
			chxy, move, &minline, &minheight))
	    minpoint = p;
    }

    block = (move->start.cx >> B_SHIFT) + mapx * (move->start.cy >> B_SHIFT);
    points = blockline[block].points;
    lines = Shape_lines(shape, dir);
    x = -1;
    while ( ( p = *points++) != 65535) {
	if (linet[p].group
	    && (groups[linet[p].group].hit_mask & move->hit_mask))
	    continue;
	msx = move->start.cx - linet[p].start.cx;
	msy = move->start.cy - linet[p].start.cy;
	if (chx)
	    msx = -msx;
	if (chy)
	    msy = -msy;
	if (chxy) {
	    temp = msx;
	    msx = msy;
	    msy = temp;
	}
	if (Lines_check(msx, msy, mdx, mdy, &mindone, lines, chx, chy,
			chxy, move, &minline, &minheight))
	    minpoint = p;
    }

    answer->point = minpoint;
    answer->line = minline;
    answer->point = minpoint;
    if (chxy) {
	temp = mindone;
	mindone = minheight;
	minheight = temp;
    }
    if (chx)
	mindone = -mindone;
    if (chy)
	minheight = -minheight;
    answer->moved.cx = mindone;
    answer->moved.cy = minheight;

    return;
}

static int Shape_turn1(const shipobj *shape, int hitmask, int x, int y,
		       int dir, int sign)
{
    struct move move;
    struct collans answer;
    int i, p, xo1, xo2, yo1, yo2, xn1, xn2, yn1, yn2, xp, yp, s, t;
    unsigned short *points;
    int newdir = MOD2(dir + sign, RES);

    move.hit_mask = hitmask;
    for (i = 0; i < shape->num_points; i++) {
	move.start.cx = x + shape->pts[i][dir].cx;
	move.start.cy = y + shape->pts[i][dir].cy;
	move.delta.cx = x + shape->pts[i][newdir].cx - move.start.cx;
	move.delta.cy = y + shape->pts[i][newdir].cy - move.start.cy;
	move.start.cx = WRAP_XCLICK(move.start.cx);
	move.start.cy = WRAP_YCLICK(move.start.cy);
	while (move.delta.cx || move.delta.cy) {
	    Move_point(&move, &answer);
	    if (answer.line != -1)
		return 0;
	    move.start.cx = WRAP_XCLICK(move.start.cx + answer.moved.cx);
	    move.start.cy = WRAP_YCLICK(move.start.cy + answer.moved.cy);
	    move.delta.cx -= answer.moved.cx;
	    move.delta.cy -= answer.moved.cy;
	}
    }

    /* Convex shapes would be much easier. */
    points = blockline[(x >> B_SHIFT) + mapx * (y >> B_SHIFT)].points;
    while ( (p = *points++) != 65535) {
	if (linet[p].group && (groups[linet[p].group].hit_mask & hitmask))
	    continue;
	xp = CENTER_XCLICK(linet[p].start.cx - x);
	yp = CENTER_YCLICK(linet[p].start.cy - y);
	xo1 = shape->pts[shape->num_points - 1][dir].cx - xp;
	yo1 = shape->pts[shape->num_points - 1][dir].cy - yp;
	xn1 = shape->pts[shape->num_points - 1][newdir].cx - xp;
	yn1 = shape->pts[shape->num_points - 1][newdir].cy - yp;
	t = 0;
	for (i = 0; i < shape->num_points; i++) {
	    xo2 = shape->pts[i][dir].cx - xp;
	    yo2 = shape->pts[i][dir].cy - yp;
	    xn2 = shape->pts[i][newdir].cx - xp;
	    yn2 = shape->pts[i][newdir].cy - yp;

#define TEMPFUNC(X1, Y1, X2, Y2)                                           \
	    if ((X1) < 0) {                                                \
		if ((X2) >= 0) {                                           \
		    if ((Y1) > 0 && (Y2) >= 0)                             \
			t++;                                               \
		    else if (((Y1) >= 0 || (Y2) >= 0) &&                   \
			     (s = (X1)*((Y1)-(Y2))-(Y1)*((X1)-(X2))) >= 0){\
			if (s == 0)                                        \
			    return 0;                                      \
			else                                               \
			    t++;                                           \
		    }                                                      \
		}                                                          \
	    }                                                              \
	    else                                                           \
		if ((X2) <= 0) {                                           \
		    if ((X2) == 0) {                                       \
			if ((Y2)==0||((X1)==0 && (((Y1)<=0 && (Y2)>= 0) || \
						 ((Y1) >= 0 && (Y2)<=0)))) \
			    return 0;                                      \
		    }                                                      \
		    else if ((Y1) > 0 && (Y2) >= 0)                        \
			t++;                                               \
		    else if (((Y1) >= 0 || (Y2) >= 0) &&                   \
			     (s = (X1)*((Y1)-(Y2))-(Y1)*((X1)-(X2))) <= 0){\
			if (s == 0)                                        \
			    return 0;                                      \
			else                                               \
			    t++;                                           \
		    }                                                      \
		}

	    TEMPFUNC(xo1, yo1, xn1, yn1);
	    TEMPFUNC(xn1, yn1, xn2, yn2);
	    TEMPFUNC(xn2, yn2, xo2, yo2);
	    TEMPFUNC(xo2, yo2, xo1, yo1);
#undef TEMPFUNC

	    if (t & 1)
		return 0;
	    xo1 = xo2;
	    yo1 = yo2;
	    xn1 = xn2;
	    yn1 = yn2;
	}
    }
    return 1;
}


/* This function should get called only rarely, so it doesn't need to
   be too efficient. */
static int Clear_corner(struct move *move, object *obj, int l1, int l2)
{
    int x, y, xm, ym, s1, s2;
    int l1sx, l2sx, l1sy, l2sy, l1dx, l2dx, l1dy, l2dy;
    int side;

    l1sx = linet[l1].start.cx - move->start.cx;
    l1sy = linet[l1].start.cy - move->start.cy;
    l1sx = CENTER_XCLICK(l1sx);
    l1sy = CENTER_YCLICK(l1sy);
    l1dx = linet[l1].delta.cx;
    l1dy = linet[l2].delta.cy;
    l2sx = linet[l2].start.cx - move->start.cx;
    l2sy = linet[l2].start.cy - move->start.cy;
    l2sx = CENTER_XCLICK(l2sx);
    l2sy = CENTER_YCLICK(l2sy);
    l2dx = linet[l2].delta.cx;
    l2dy = linet[l2].delta.cy;

    for (;;) {
	if (SIDE(obj->vel.x, obj->vel.y, l1) < 0) {
	    if (!Bounce_object(obj, move, l1, 0))
		return 0;
	}
	if (SIDE(obj->vel.x, obj->vel.y, l2) < 0) {
	    if (!Bounce_object(obj, move, l2, 0))
		return 0;
	    continue;
	}
	break;
    }

    xm = SIGN(move->delta.cx);
    ym = SIGN(move->delta.cy);

    s1 = SIDE(move->start.cx - l1sx, move->start.cy - l1sy, l1) > 0;
    s2 = SIDE(move->start.cx - l2sx, move->start.cy - l2sy, l2) > 0;

#define TMPFUNC(X, Y) ((side = SIDE((X), (Y), l1)) == 0 || side > 0 != s1 || (side = SIDE((X), (Y), l2)) == 0 || side > 0 != s2)

    if (ABS(obj->vel.x) >= ABS(obj->vel.y)) {
	x = xm;
	y = 0;
	for (;;) {
	    if (TMPFUNC(move->start.cx + x, move->start.cy + y)) {
		y += ym;
		if (!TMPFUNC(move->start.cx + x, move->start.cy + y + ym))
		    break;
		else
		    x += xm;;
	    }
	    else {
		if (TMPFUNC(move->start.cx + x, move->start.cy + y + 1) &&
		    TMPFUNC(move->start.cx + x, move->start.cy + y - 1))
		    x += xm;
		else
		    break;
	    }
	}
	move->delta.cx -= x;
	move->delta.cy -= y;
	if ((obj->vel.x >= 0) ^ (move->delta.cx >= 0)) {
	    move->delta.cx = 0;
	    move->delta.cy = 0;
	}
    }
    else {
	x = 0;
	y = ym;
	for (;;) {
	    if (TMPFUNC(move->start.cx + x, move->start.cy + y)) {
		x += xm;
		if (!TMPFUNC(move->start.cx + x + xm, move->start.cy + y))
		    break;
		else
		    y += ym;
	    }
	    else {
		if (TMPFUNC(move->start.cx + x + 1, move->start.cy + y) &&
		    TMPFUNC(move->start.cx + x - 1, move->start.cy + y))
		    y += ym;
		else
		    break;
	    }
	}

#undef TMPFUNC

	move->delta.cx -= x;
	move->delta.cy -= y;
	if ((obj->vel.y >= 0) ^ (move->delta.cy >= 0)) {
	    move->delta.cx = 0;
	    move->delta.cy = 0;
	}
    }
    move->start.cx = WRAP_XCLICK(move->start.cx + x);
    move->start.cy = WRAP_YCLICK(move->start.cy + y);
    return 1;
}


static void store_short(unsigned char **ptr, int i)
{
    *(*ptr)++ = i >> 8;
    *(*ptr)++ = i & 0xff;
}


static void store_32bit(unsigned char **ptr, int i)
{
    store_short(ptr, i >> 16);
    store_short(ptr, i & 0xffff);
}


int Polys_to_client(unsigned char *ptr)
{
    int i, j, startx, starty, dx, dy;
    int *edges;
    unsigned char *start = ptr;

    *ptr++ = num_pstyles;
    *ptr++ = num_estyles;
    *ptr++ = num_bstyles;
    for (i = 0; i < num_pstyles; i++) {
	store_32bit(&ptr, pstyles[i].color);
	*ptr++ = pstyles[i].texture_id;
	*ptr++ = pstyles[i].defedge_id;
	*ptr++ = pstyles[i].flags;
    }
    for (i = 0; i < num_estyles; i++) {
	*ptr++ = estyles[i].width;
	store_32bit(&ptr, estyles[i].color);
	*ptr++ = estyles[i].style;
    }
    for (i = 0; i < num_bstyles; i++) {
	strcpy((char *)ptr, bstyles[i].filename);
	ptr += strlen((char *)ptr) + 1;
	*ptr++ = bstyles[i].flags;
    }
    store_short(&ptr, num_polys);
    for (i = 0; i < num_polys; i++) {
	*ptr++ = pdata[i].style;
	j = pdata[i].num_points;
	store_short(&ptr, pdata[i].num_echanges);
	edges = estyleptr + pdata[i].estyles_start;
	while (*edges != INT_MAX)
	    store_short(&ptr, *edges++);
	startx = pdata[i].x;
	starty = pdata[i].y;
	edges = pdata[i].edges;
	store_short(&ptr, j);
	store_short(&ptr, startx >> CLICK_SHIFT);
	store_short(&ptr, starty >> CLICK_SHIFT);
	dx = startx;
	dy = starty;
	for (; j > 0; j--) {
	    dx += *edges++;
	    dy += *edges++;
	    if (j != 1) {
		store_short(&ptr, (dx >> CLICK_SHIFT) - (startx>>CLICK_SHIFT));
		store_short(&ptr, (dy >> CLICK_SHIFT) - (starty>>CLICK_SHIFT));
	    }
	    startx = dx;
	    starty = dy;
	}
    }
    *ptr++ = World.NumBases;
    for (i = 0; i < World.NumBases; i++) {
	if (World.base[i].team == TEAM_NOT_SET)
	    *ptr++ = 0;
	else
	    *ptr++ = World.base[i].team;
	store_short(&ptr, World.base[i].pos.cx >> CLICK_SHIFT);
	store_short(&ptr, World.base[i].pos.cy >> CLICK_SHIFT);
	*ptr++ = World.base[i].dir;
    }
    store_short(&ptr, World.NumFuels);
    for (i = 0; i < World.NumFuels; i++) {
	store_short(&ptr, World.fuel[i].pos.cx >> CLICK_SHIFT);
	store_short(&ptr, World.fuel[i].pos.cy >> CLICK_SHIFT);
    }
    *ptr++ = World.NumChecks;
    for (i = 0; i < World.NumChecks; i++) {
	store_short(&ptr, World.check[i].cx >> CLICK_SHIFT);
	store_short(&ptr, World.check[i].cy >> CLICK_SHIFT);
    }
    return ptr - start;
}


struct tempy {
    short y;
    struct tempy *next;
};


struct templine {
    short x1, x2, y1, y2;
    struct templine *next;
};


#define STORE(T,P,N,M,V)						\
    if (N >= M && ((M <= 0)						\
	? (P = (T *) malloc((M = 1) * sizeof(*P)))			\
	: (P = (T *) realloc(P, (M += M) * sizeof(*P)))) == NULL) {	\
	warn("No memory");						\
	exit(1);							\
    } else								\
	(P[N++] = V)


#define POSMOD(x, y) ((x) >= 0 ? (x) % (y) : (x) % (y) + (y))


int is_inside(int cx, int cy, int hit_mask)
{
    short *ptr;
    int inside, cx1, cx2, cy1, cy2, s;
    struct inside_block *gblock;

    gblock = &inside_table[(cx >> B_SHIFT) + mapx * (cy >> B_SHIFT)];
    if (gblock->group == -1)
	return -1;
    do {
	if (gblock->group && (groups[gblock->group].hit_mask & hit_mask)) {
	    gblock = gblock->next;
	    continue;
	}
	inside = gblock->base_value;
	if (gblock->lines == NULL) {
	    if (inside)
		return gblock->group;
	    else {
		gblock = gblock->next;
		continue;
	    }
	}
	cx &= B_MASK;
	cy &= B_MASK;
	ptr = gblock->y;
	if (ptr)
	    while (cy > *ptr++)
		inside++;
	ptr = gblock->lines;
	while (*ptr != 32767) {
	    cx1 = *ptr++ - cx;
	    cy1 = *ptr++ - cy;
	    cx2 = *ptr++ - cx;
	    cy2 = *ptr++ - cy;
	    if (cy1 < 0) {
		if (cy2 >= 0) {
		    if (cx1 > 0 && cx2 >= 0)
			inside++;
		    else if ((cx1 >= 0 || cx2 >= 0) &&
			     (s = cy1 * (cx1 - cx2) - cx1 * (cy1 - cy2)) >= 0) {
			if (s == 0)
			    return gblock->group;
			else
			    inside++;
		    }
		}
	    }
	    else
		if (cy2 <= 0) {
		    if (cy2 == 0) {
			if (cx2 == 0 || (cy1 ==0 && ((cx1 <= 0 && cx2 >= 0) ||
						     (cx1 >= 0 && cx2 <= 0))))
			    return gblock->group;
		    }
		    else if (cx1 > 0 && cx2 >= 0)
			inside++;
		    else if ((cx1 >= 0 || cx2 >= 0) &&
			     (s = cy1 * (cx1 - cx2) - cx1 * (cy1 - cy2)) <= 0) {
			if (s == 0)
			    return gblock->group;
			else
			    inside++;
		    }
		}
	}
	if (inside & 1)
	    return gblock->group;
	gblock = gblock->next;
    } while (gblock);
    return -1;
}


static void closest_line(int bx, int by, double dist, int inside)
{
    if (dist <= temparray[bx + mapx *by].distance) {
	if (dist == temparray[bx + mapx * by].distance)
	    /* Must be joined polygons(s) if the map is legal
	     * (the same line appears in both directions).
	     * Both sides of this line are inside. */
	    /* These lines could be removed from the table as a minor
	     * optimization. */
	     inside = 1;
	temparray[bx + mapx * by].distance = dist;
	temparray[bx + mapx * by].inside = inside;
    }
}


static void insert_y(int block, int y)
{
    struct tempy *ptr;
    struct tempy **prev;

    ptr = temparray[block].y;
    prev = &temparray[block].y;
    while (ptr && ptr->y < y) {
	prev = &ptr->next;
	ptr = ptr->next;
    }
    if (ptr && ptr->y == y) {
	*prev = ptr->next;
	free(ptr);
	return;
    }
    *prev = ralloc(NULL, sizeof(struct tempy));
    (*prev)->y = y;
    (*prev)->next = ptr;
}


static void store_inside_line(int bx, int by, int ox, int oy, int dx, int dy)
{
    int block;
    struct templine *s;

    block = bx + mapx * by;
    ox = CENTER_XCLICK(ox - bx * B_CLICKS);
    oy = CENTER_YCLICK(oy - by * B_CLICKS);
    if (oy >= 0 && oy < B_CLICKS && ox >= B_CLICKS)
	insert_y(block, oy);
    if (oy + dy >= 0 && oy + dy < B_CLICKS && ox + dx >= B_CLICKS)
	insert_y(block, oy + dy);
    s = ralloc(NULL, sizeof(struct templine));
    s->x1 = ox;
    s->x2 = ox + dx;
    s->y1 = oy;
    s->y2 = oy + dy;
    s->next = temparray[block].lines;
    temparray[block].lines = s;
}


static void finish_inside(int block, int group)
{
    int inside;
    struct inside_block *gblock;
    short *ptr;
    int x1, x2, y1, y2, s, j;
    struct tempy *yptr;
    struct templine *lptr;
    void *tofree;

    gblock = &inside_table[block];
    if (gblock->group != -1) {
	while (gblock->next) /* Maintain group order*/
	    gblock = gblock->next;
	gblock->next = ralloc(NULL, sizeof(struct inside_block));
	gblock = gblock->next;
    }
    gblock->group = group;
    gblock->next = NULL;
    j = 0;
    yptr = temparray[block].y;
    while (yptr) {
	j++;
	yptr = yptr->next;
    }
    if (j > 0) {
	ptr = ralloc(NULL, (j + 1) * sizeof(short));
	gblock->y = ptr;
	yptr = temparray[block].y;
	while (yptr) {
	    *ptr++ = yptr->y;
	    tofree = yptr;
	    yptr = yptr->next;
	    free(tofree);
	}
	*ptr = 32767;
    }
    else
	gblock->y = NULL;
    j = 0;
    lptr = temparray[block].lines;
    while (lptr) {
	j++;
	lptr = lptr->next;
    }
    if (j > 0) {
	ptr = ralloc(NULL, (j * 4 + 1) * sizeof(short));
	gblock->lines = ptr;
	lptr = temparray[block].lines;
	while (lptr) {
	    *ptr++ = lptr->x1;
	    *ptr++ = lptr->y1;
	    *ptr++ = lptr->x2;
	    *ptr++ = lptr->y2;
	    tofree = lptr;
	    lptr = lptr->next;
	    free(tofree);
	}
	*ptr = 32767;
    }
    else
	gblock->lines = NULL;
    inside = temparray[block].inside;
    if ( (ptr = gblock->lines) != NULL) {
	while (*ptr != 32767) {
	    x1 = *ptr++ * 2 - B_CLICKS * 2 + 1;
	    y1 = *ptr++ * 2 + 1;
	    x2 = *ptr++ * 2 - B_CLICKS * 2 + 1;
	    y2 = *ptr++ * 2 + 1;
	    if (y1 < 0) {
		if (y2 >= 0) {
		    if (x1 > 0 && x2 >= 0)
			inside++;
		    else if ((x1 >= 0 || x2 >= 0) &&
			     (s = y1 * (x1 - x2) - x1 * (y1 - y2)) > 0)
			inside++;
		}
	    }
	    else
		if (y2 <= 0) {
		    if (x1 > 0 && x2 >= 0)
			inside++;
		    else if ((x1 >= 0 || x2 >= 0) &&
			     (s = y1 * (x1 - x2) - x1 * (y1 - y2)) < 0)
			inside++;
		}
	}
    }
    gblock->base_value = inside & 1;
    temparray[block].y = NULL;
    temparray[block].lines = NULL;
    temparray[block].inside = 2;
    temparray[block].distance = 1e20;
}


static void init_inside(void)
{
    int i;

    inside_table = ralloc(NULL, mapx * mapy * sizeof(struct inside_block));
    temparray = ralloc(NULL, mapx * mapy * sizeof(struct test));
    for (i = 0; i < mapx * mapy; i++) {
	temparray[i].distance = 1e20;
	temparray[i].inside = 2;
	temparray[i].y = NULL;
	temparray[i].lines = NULL;
	inside_table[i].y = NULL;
	inside_table[i].lines = NULL;
	inside_table[i].base_value = 0;
	inside_table[i].group = -1;
	inside_table[i].next = NULL;
    }
}


/* Calculate distance of intersection from lower right corner of the
 * block counterclockwise along the edge. We don't return the true lengths
 * but values which compare the same with each other.
 * 'dir' is used to return whether the block is left through a horizontal
 * or a vertical side or a corner. */
static double edge_distance(int bx, int by, int ox, int oy, int dx, int dy,
			  int *dir)
{
    int last_width = (World.cwidth - 1) % B_CLICKS + 1;
    int last_height = (World.cheight - 1) % B_CLICKS + 1;
    double xdist, ydist, dist;
    ox = CENTER_XCLICK(ox - bx * B_CLICKS);
    oy = CENTER_YCLICK(oy - by * B_CLICKS);
    if (dx > 0)
	xdist = ((bx == mapx - 1) ? last_width : B_CLICKS) - .5 - ox;
    else if (dx < 0)
	xdist = ox + .5;
    else
	xdist = 1e20; /* Something big enough to be > ydist, dx */
    if (dy > 0)
	ydist = ((by == mapy - 1) ? last_height : B_CLICKS) - .5 - oy;
    else if (dy < 0)
	ydist = oy + .5;
    else
	ydist = 1e20;
    if (xdist > ABS(dx) && ydist > ABS(dy))
	return -1;	/* Doesn't cross box boundary */
    if (ABS(dy) * xdist == ABS(dx) * ydist)
	*dir = 3;
    else if (ABS(dy) * xdist < ABS(dx) * ydist)
	*dir = 1;
    else
	*dir = 2;
    if (*dir == 1)
	if (dx > 0)
	    dist = oy + dy * xdist / dx;
	else
	    dist = 5 * B_CLICKS - oy + dy * xdist / dx;
    else
	if (dy > 0)
	    dist = 3 * B_CLICKS - ox - dx * ydist / dy;
	else
	    dist = 6 * B_CLICKS + ox - dx * ydist / dy;
    return dist;
}


static void inside_test(void)
{
    int dx, dy, bx, by, ox, oy, startx, starty;
    int i, j, num_points, minx = -1, miny = -1, poly, group;
    int bx2, by2, maxx = -1, maxy = -1, dir;
    double dist;
    int *edges;

    init_inside();
    for (group = 0; group <= num_groups; group++) {
	minx = -1;
	for (poly = 0; poly < num_polys; poly++) {
	    if (pdata[poly].is_decor || pdata[poly].group != group)
		continue;
	    num_points = pdata[poly].num_points;
	    dx = 0;
	    dy = 0;
	    startx = pdata[poly].x;
	    starty = pdata[poly].y;
	    /* Better wrapping for bx2/by2 could be selected for speed here,
	     * but this keeping track of min/max at all is probably
	     * unnoticeable in practice. */
	    bx2 = bx = startx >> B_SHIFT;
	    by2 = by = starty >> B_SHIFT;
	    if (minx == -1) {
		minx = maxx = bx2;
		miny = maxy = by2;
	    }
	    edges = pdata[poly].edges;
	    closest_line(bx, by, 1e10, 0); /* For polygons within one block */
	    for (j = 0; j < num_points; j++) {
		if (((startx >> B_SHIFT) != bx)
		    || ((starty >> B_SHIFT) != by)) {
		    printf("startx = %d, startx >> B_SHIFT = %d, bx = %d\n",
			   startx, startx >> B_SHIFT, bx);
		    printf("starty = %d, starty >> B_SHIFT = %d, by = %d\n",
			   starty, starty >> B_SHIFT, by);
		    printf("going into infinite loop...\n");
		    /* hmm ??? */
		    while (1);
		}
		ox = startx & B_MASK;
		oy = starty & B_MASK;
		dx = *edges++;
		dy = *edges++;
		while (1) {  /* All blocks containing a part of this line */
		    store_inside_line(bx, by, startx, starty, dx, dy);
		    dist = edge_distance(bx, by, WRAP_XCLICK(startx + dx),
				 WRAP_YCLICK(starty + dy), -dx, -dy, &dir);
		    if (dist != -1)
			closest_line(bx, by, dist, 1);
		    dist = edge_distance(bx, by, startx, starty, dx, dy, &dir);
		    if (dist == -1)
			break;
		    closest_line(bx, by, dist, 0);
		    if (dir == 1 || dir == 3)
			bx2 += (dx > 0) ? 1 : -1;
		    if (bx2 > maxx)
			maxx = bx2;
		    if (bx2 < minx)
			minx = bx2;
		    bx = POSMOD(bx2, mapx);
		    if (dir == 2 || dir == 3)
			by2 += (dy > 0) ? 1 : -1;
		    if (by2 > maxy)
			maxy = by2;
		    if (by2 < miny)
			miny = by2;
		    by = POSMOD(by2, mapy);
		}
		startx = WRAP_XCLICK(startx + dx);
		starty = WRAP_YCLICK(starty + dy);
	    }
	}
	bx = maxx - minx + 1;
	if (bx > 2 * mapx)
	    bx = 2 * mapx;
	by = maxy - miny + 1;
	if (by > mapy)
	    by = mapy;
	for (i = POSMOD(miny, mapy); by-- > 0; i++) {
	    if (i == mapy)
		i = 0;
	    bx2 = bx;
	    dir = 0;
	    for (j = POSMOD(minx, mapx); bx2-- > 0; j++) {
		if (j == mapx)
		    j = 0;
		if (temparray[j + mapx * i].inside < 2) {
		    dir = temparray[j + mapx * i].distance > B_CLICKS &&
			temparray[j + mapx * i].inside == 1;
		}
		else {
		    if (dir)
			temparray[i * mapx + j].inside = 1;
		}
		if (bx2 < mapx)
		    finish_inside(j + mapx * i, group);
	    }
	}
    }
    return;
}


/* Include NCLLIN - 1 closest lines or all closer than CUTOFF (whichever
 * is less) in the line table for this block.
 * Include all lines closer than DICLOSE, however many there are.
 * LINSIZE tells the amout of temporary memory to reserve for the algorithm.
 * If it is not large enough to hold the required lines, print error and
 * exit.
 */

#define DICLOSE (5 * CLICK)
#define LINSIZE 100
#define NCLLIN (10 + 1)
static void Distance_init(void)
{
    int cx,cy;
    int *lineno, *dis;
    int lsx, lsy, ldx, ldy, temp, dist, n, i, bx, by, j, k;
    int base, height2, by2, width, height;
    int distbound, size;
    unsigned short *lptr;

    /* max line delta 30000 */

    blockline = ralloc(NULL, mapx * mapy * sizeof(struct blockinfo));
    lineno = ralloc(NULL, mapx * mapy * LINSIZE * sizeof(int));
    dis = ralloc(NULL, mapx * mapy * LINSIZE * sizeof(int));
    size = 1; /* start with end marker */
    for (bx = 0; bx < mapx; bx++)
	for (by = 0; by < mapy; by++)
	    for (i = 0; i < LINSIZE; i++) {
		dis[(by * mapx + bx) * LINSIZE + i] = MAX_MOVE + B_CLICKS / 2;
		lineno[(by * mapx + bx) * LINSIZE +i] = 65535;
	    }
    for (i = 0; i < linec; i++) {
	bx = linet[i].start.cx;
	by = linet[i].start.cy;
	width = linet[i].delta.cx;
	height = linet[i].delta.cy;
	if (width < 0) {
	    bx += width;
	    width = -width;
	    bx = WRAP_XCLICK(bx);
	}
	if (height < 0) {
	    by += height;
	    height = -height;
	    by = WRAP_YCLICK(by);
	}
	width = (width + 2 * MAX_MOVE) / B_CLICKS + 5;
	if (width >= mapx)
	    width = mapx;
	height = (height + 2 * MAX_MOVE) / B_CLICKS + 5;
	if (height >= mapy)
	    height = mapy;
	bx = (bx - MAX_MOVE) / B_CLICKS - 2;
	by = (by - MAX_MOVE) / B_CLICKS - 2;
	while (bx < 0)
	    bx += mapx;
	while (by < 0)
	    by += mapy;
	height2 = height;
	by2 = by;
	for (; width-- > 0; bx = bx == mapx - 1 ? 0 : bx + 1)
	    for (by = by2, height = height2;
		 height -- > 0;
		 by = by == mapy - 1? 0 : by + 1) {
		cx = bx * B_CLICKS + B_CLICKS / 2;
		cy = by * B_CLICKS + B_CLICKS / 2;
		base = (by * mapx + bx) * LINSIZE;
		lsx = CENTER_XCLICK(linet[i].start.cx - cx);
		if (ABS(lsx) > 32767 + MAX_MOVE + B_CLICKS / 2)
		    continue;
		lsy = CENTER_YCLICK(linet[i].start.cy - cy);
		if (ABS(lsy) > 32767 + MAX_MOVE + B_CLICKS / 2)
		    continue;
		ldx = linet[i].delta.cx;
		ldy = linet[i].delta.cy;
		if (MAX(ABS(lsx), ABS(lsy)) > MAX(ABS(lsx + ldx),
						  ABS(lsy + ldy))) {
		    lsx += ldx;
		    ldx = -ldx;
		    lsy += ldy;
		    ldy = -ldy;
		}
		if (ABS(lsx) < ABS(lsy)) {
		    temp = lsx;
		    lsx = lsy;
		    lsy = temp;
		    temp = ldx;
		    ldx = ldy;
		    ldy = temp;
		}
		if (lsx < 0) {
		    lsx = -lsx;
		    ldx = -ldx;
		}
		if (ldx >= 0)
		    dist = lsx - 1;
		else {
		    if (lsy + ldy < 0) {
			lsy = -lsy;
			ldy = -ldy;
		    }
		    temp = lsy - lsx;
		    lsx += lsy;
		    lsy = temp;
		    temp = ldy - ldx;
		    ldx += ldy;
		    ldy = temp;
		    dist = lsx - ldx * lsy / ldy;
		    if (lsx + ldx < 0)
			dist = MIN(ABS(dist), ABS(lsy - ldy * lsx / ldx));
		    dist = dist / 2 - 3; /* 3? didn't bother to get the right value */
		}
		dist--;
		/* Room for one extra click of movement after main collision
		   detection. Used to get away from a line after a bounce. */
		if (dist < CUTOFF + B_CLICKS / 2) {
		    if (dist < B_CLICKS / 2 + DICLOSE)
			distbound = LINSIZE;
		    else
			distbound = NCLLIN;
		    for (j = 1; j < distbound; j++) {
			if (dis[base + j] <= dist)
			    continue;
			k = dis[base + j];
			n = j;
			for (j++; j < distbound; j++)
			    if (dis[base + j] > k) {
				k = dis[base + j];
				n = j;
			    }
			if (dis[base + 0] > dis[base + n])
			    dis[base + 0] = dis[base + n];
			if (lineno[base + n] == 65535) {
			    size++; /* more saved lines */
			    if (n == 1)
				size++; /* first this block, for 65535 */
			}
			dis[base + n] = dist;
			lineno[base + n] = i;
			goto stored;
		    }
		}
		if (dist < dis[base + 0])
		    dis[base + 0] = dist;
		if (dist < B_CLICKS / 2 + DICLOSE) {
		    printf("Not enough space in line table. "
			   "Fix allocation in walls.c\n");
		    exit(1);
		}
	    stored:
		; /* semicolon for ansi compatibility */
	    }
	}
    llist = ralloc(NULL, size * sizeof(short));
    lptr = llist;
    *lptr++ = 65535; /* All blocks with no lines stored point to this. */
    for (bx = 0; bx < mapx; bx++)
	for (by = 0; by < mapy; by++) {
	    base = (by * mapx + bx) * LINSIZE;
	    k = bx + mapx * by;
	    blockline[k].distance = dis[base + 0] - B_CLICKS / 2;
	    if (lineno[base + 1] == 65535)
		blockline[k].lines = llist;
	    else {
		blockline[k].lines = lptr;
		for (j = 1; j < LINSIZE && lineno[base + j] != 65535; j++)
		    *lptr++ = lineno[base + j];
		*lptr++ = 65535;
	    }
	}
    free(lineno);
    free(dis);
}


static void Corner_init(void)
{
    int bx, by, cx, cy, dist, i;
    unsigned short *ptr, *temp;
    int block, size = mapx * mapy;
    int height, height2, width, by2;

#define DISIZE 200
    temp = ralloc(NULL, mapx * mapy * DISIZE * sizeof(short)); /* !@# */
    for (i = 0; i < mapx * mapy; i++)
	temp[i * DISIZE] = 0;
    for (i = 0; i < linec; i++) {
	bx = linet[i].start.cx;
	by = linet[i].start.cy;
	width = height = (2 * MAX_MOVE) / B_CLICKS + 7;
	if (width >= mapx)
	    width = mapx;
	if (height >= mapy)
	    height = mapy;
	bx = (bx - MAX_MOVE) / B_CLICKS - 3;
	by = (by - MAX_MOVE) / B_CLICKS - 3;
	while (bx < 0)
	    bx += mapx;
	while (by < 0)
	    by += mapy;
	height2 = height;
	by2 = by;
	for (; width-- > 0; bx = bx == mapx - 1 ? 0 : bx + 1)
	    for (by = by2, height = height2;
		 height -- > 0;
		 by = by == mapy - 1? 0 : by + 1) {
		block = bx + mapx * by;
		dist = blockline[block].distance
		    + MAX_SHAPE_OFFSET + B_CLICKS / 2;
		cx = bx * B_CLICKS + B_CLICKS / 2;
		cy = by * B_CLICKS + B_CLICKS / 2;
		if (ABS(CENTER_XCLICK(linet[i].start.cx - cx)) > dist)
		    continue;
		if (ABS(CENTER_YCLICK(linet[i].start.cy - cy)) > dist)
		    continue;
		temp[++temp[DISIZE * block] + DISIZE * block] = i;
		size++;
	    }
    }
    plist = ralloc(NULL, size * sizeof(short));
    ptr = plist;
    for (block = 0; block < mapx * mapy; block++) {
	blockline[block].points = ptr;
	i = temp[block * DISIZE];
	if (i > DISIZE - 1) {
	    warn("Not enough corner space in walls.c, add more.");
	    exit(1);
	}
	while (i > 0) {
	    *ptr++ = temp[block * DISIZE + i];
	    i--;
	}
	*ptr++ = 65535;
    }
    free(temp);
#undef DISIZE
}


void Ball_line_init(void)
{
    int i;
    static clpos coords[24];

    if (!treatBallAsPoint) {
	ball_wire.num_points = 24;
	for (i = 0; i < 24; i++) {
	    ball_wire.pts[i] = coords + i;
	    coords[i].cx = cos(i * PI / 12) * BALL_RADIUS * CLICK;
	    coords[i].cy = sin(i * PI / 12) * BALL_RADIUS * CLICK;
	}
	/*xpprintf(__FILE__ ": treating ball as polygon.\n");*/
    } else {
	ball_wire.num_points = 1;
	ball_wire.pts[0] = coords;
	coords[0].cx = 0;
	coords[0].cy = 0;
	/*xpprintf(__FILE__ ": treating ball as point.\n");*/
    }

    return;
}


static void Poly_to_lines(void)
{
    int i, np, j, startx, starty, dx, dy, group, *styleptr, style;
    int *edges;

    linec = 0;
    for (i = 0; i < num_polys; i++) {
	if (pdata[i].is_decor)
	    continue;
	group = pdata[i].group;
	np = pdata[i].num_points;
	styleptr = estyleptr + pdata[i].estyles_start;
	style = pstyles[pdata[i].style].defedge_id;
	dx = 0;
	dy = 0;
	startx = pdata[i].x;
	starty = pdata[i].y;
	edges = pdata[i].edges;
	for (j = 0; j < np; j++) {
	    if (j == *styleptr) {
		styleptr++;
		style = *styleptr++;
	    }
	    if (style == 0) {
		dx += *edges++;
		dy += *edges++;
		continue;
	    }
	    if (!(linec % 2000))
		linet = ralloc(linet, (linec + 2000) * sizeof(struct bline));
	    linet[linec].group = group;
	    linet[linec].start.cx = TWRAP_XCLICK(startx + dx);
	    linet[linec].start.cy = TWRAP_YCLICK(starty + dy);
	    linet[linec].delta.cx = *edges;
	    dx += *edges++;
	    linet[linec++].delta.cy = *edges;
	    dy += *edges++;
	}
	if (dx || dy) {
	    warn("Polygon %d (%d points) doesn't start and end at the "
		 "same place", i + 1, np);
	    exit(1);
	}
    }
    linet = ralloc(linet, (linec + S_LINES) * sizeof(struct bline));
    return;
}


void Walls_init_new(void)
{
    DFLOAT x, y, l2;
    int i;

    mapx = (World.cwidth + B_MASK) >> B_SHIFT;
    mapy = (World.cheight + B_MASK) >> B_SHIFT;
    Poly_to_lines();
    Distance_init();
    Corner_init();
    Ball_line_init();
    inside_test();
    groups[0].type = FILLED;

    for (i = 0; i < linec; i++) {
	x = linet[i].delta.cx;
	y = linet[i].delta.cy;
	l2 = (x*x + y*y);
	linet[i].c = (x*x - y*y) / l2;
	linet[i].s = 2*x*y / l2;
    }
}


/* end */

static char msg[MSG_LEN];



static void Move_ball_new(object *obj)
{
    /*object		*obj = Obj[ind];*/
    int line, point;
    struct move move;
    struct collans ans;
    int owner;

    move.delta.cx = FLOAT_TO_CLICK(obj->vel.x * timeStep2);
    move.delta.cy = FLOAT_TO_CLICK(obj->vel.y * timeStep2);
    obj->extmove.cx = move.delta.cx;
    obj->extmove.cy = move.delta.cy;

    if (obj->id != -1
	&& BIT(Players[GetInd[obj->id]]->used, HAS_PHASING_DEVICE)) {

	int x = obj->pos.cx + move.delta.cx;
	int y = obj->pos.cy + move.delta.cy;
	while (x >= World.cwidth)
	    x -= World.cwidth;
	while (x < 0)
	    x += World.cwidth;
	while (y >= World.cheight)
	    y -= World.cheight;
	while (y < 0)
	    y += World.cheight;
	Object_position_set_clicks(obj, x, y);
	/* kps hack */
	Cell_add_object(obj);
	/* kps hack */
	return;
    }
    owner = BALL_PTR(obj)->owner;
    if (owner == NO_ID || Players[GetInd[owner]]->team == TEAM_NOT_SET)
	move.hit_mask = BALL_BIT | NOTEAM_BIT;
    else
	move.hit_mask = BALL_BIT | 1 << Players[GetInd[owner]]->team;
    move.start.cx = obj->pos.cx;
    move.start.cy = obj->pos.cy;
    while (move.delta.cx || move.delta.cy) {
	Shape_move(&move, &ball_wire, 0, &ans);
	move.start.cx = WRAP_XCLICK(move.start.cx + ans.moved.cx);
	move.start.cy = WRAP_YCLICK(move.start.cy + ans.moved.cy);
	move.delta.cx -= ans.moved.cx;
	move.delta.cy -= ans.moved.cy;
	if (ans.line != -1) {
	    if (!Shape_away(&move, &ball_wire, 0, ans.line, &line, &point)) {
		if (SIDE(obj->vel.x, obj->vel.y, ans.line) < 0) {
		    if (!Bounce_object(obj, &move, ans.line, ans.point))
			break;
		}
		else if (SIDE(obj->vel.x, obj->vel.y, line) < 0) {
		    if (!Bounce_object(obj, &move, line, point))
			break;
		}
		else {
		    /* This case could be handled better,
		       I'll write the code for that if this
		       happens too often. */
		    move.delta.cx = 0;
		    move.delta.cy = 0;
		    obj->vel.x = 0;
		    obj->vel.y = 0;
		}
	    }
	    else if (SIDE(obj->vel.x, obj->vel.y, ans.line) < 0)
		if (!Bounce_object(obj, &move, ans.line, ans.point))
		    break;
	}
    }
    Object_position_set_clicks(obj, move.start.cx, move.start.cy);
    /* kps hack */
    Cell_add_object(obj);
    /* kps hack */
    return;
}

/* hack */
char *typename(int type)
{
    if (type & OBJ_PLAYER)
	return "OBJ_PLAYER";
    if (type & OBJ_DEBRIS)
	return "OBJ_DEBRIS";
    if (type & OBJ_SPARK)
	return "OBJ_SPARK";
    if (type & OBJ_BALL)
	return "OBJ_BALL";
    if (type & OBJ_SHOT)
	return "OBJ_SHOT";
    if (type & OBJ_SMART_SHOT)
	return "OBJ_SMART_SHOT";
    if (type & OBJ_MINE)
	return "OBJ_MINE";
    if (type & OBJ_TORPEDO)
	return "OBJ_TORPEDO";
    if (type & OBJ_HEAT_SHOT)
	return "OBJ_HEAT_SHOT";
    if (type & OBJ_PULSE)
	return "OBJ_PULSE";
    if (type & OBJ_ITEM)
	return "OBJ_ITEM";
    if (type & OBJ_WRECKAGE)
	return "OBJ_WRECKAGE";
    if (type & OBJ_ASTEROID)
	return "OBJ_ASTEROID";
    if (type & OBJ_CANNON_SHOT)
	return "OBJ_CANNON_SHOT";
    return "unknown type";
}

/* kps- collision.c has a move_object call in ng */
/* used to have ind argument in ng */
void Move_object_new(object *obj)
{
    int t;
    struct move move;
    struct collans ans;
    int trycount = 5000;
    int team;            /* !@# should make TEAM_NOT_SET 0 */
    /*object *obj = Obj[ind];*/

    Object_position_remember(obj);

    obj->collmode = 1;

#if 1
    if (obj->type == OBJ_BALL) {
	Move_ball_new(obj);
	return;
    }
#else
    if (obj->type == OBJ_BALL) {
	if (obj->owner != -1)
	    team =  Players[GetInd[obj->owner]].team;
	else
	    team = TEAM_NOT_SET;
	move.hit_mask = BALL_BIT;
    }
    else
#endif
	{
	    move.hit_mask = NONBALL_BIT;
	    team = obj->team;
	}
    if (team == TEAM_NOT_SET)
	move.hit_mask |= NOTEAM_BIT;
    else
	move.hit_mask |= 1 << team;

    move.start.cx = obj->pos.cx;
    move.start.cy = obj->pos.cy;
    move.delta.cx = FLOAT_TO_CLICK(obj->vel.x * timeStep2);
    move.delta.cy = FLOAT_TO_CLICK(obj->vel.y * timeStep2);
    obj->extmove.cx = move.delta.cx;
    obj->extmove.cy = move.delta.cy;
    while (move.delta.cx || move.delta.cy) {
	if (!trycount--) {
	    sprintf(msg, "COULDN'T MOVE OBJECT!!!! Type = %s, x = %d, y = %d. "
		    "Object was DELETED. [*DEBUG*]",
		    typename(obj->type), move.start.cx, move.start.cy);
	    warn(msg);
	    /*Set_message(msg);*/
	    obj->life = 0;
	    return;
	}
	Move_point(&move, &ans);
	move.delta.cx -= ans.moved.cx;
	move.delta.cy -= ans.moved.cy;
	move.start.cx = WRAP_XCLICK(move.start.cx + ans.moved.cx);
	move.start.cy = WRAP_YCLICK(move.start.cy + ans.moved.cy);
	if (ans.line != -1) {
	    if ( (t = Away(&move, ans.line)) != -1) {
		if (!Clear_corner(&move, obj, ans.line, t))
		    break;
	    }
	    else if (SIDE(obj->vel.x, obj->vel.y, ans.line) < 0) {
		if (!Bounce_object(obj, &move, ans.line, 0))
		    break;
	    }
	}
    }
    Object_position_set_clicks(obj, move.start.cx, move.start.cy);
    /* kps hack */
    Cell_add_object(obj);
    /* kps hack */
    return;
}


static void Move_player_new(int ind)
{
    player *pl = Players[ind];
    clpos  pos;
    int    line, point;
    struct move move;
    struct collans ans;


    if (BIT(pl->status, PLAYING|PAUSE|GAME_OVER|KILLED) != PLAYING) {
	if (!BIT(pl->status, KILLED|PAUSE)) {
	    pos.cx = pl->pos.cx + FLOAT_TO_CLICK(pl->vel.x * timeStep2);
	    pos.cy = pl->pos.cy + FLOAT_TO_CLICK(pl->vel.y * timeStep2);
	    pos.cx = WRAP_XCLICK(pos.cx);
	    pos.cy = WRAP_YCLICK(pos.cy);
	    if (pos.cx != pl->pos.cx || pos.cy != pl->pos.cy) {
		Player_position_remember(pl);
		Player_position_set_clicks(pl, pos.cx, pos.cy);
	    }
	}
	pl->velocity = VECTOR_LENGTH(pl->vel);
	return;
    }

    /* kps - changed from  vel *= friction */
    pl->vel.x *= (1.0f - friction);
    pl->vel.y *= (1.0f - friction);

    Player_position_remember(pl);

    pl->collmode = 1;

    move.delta.cx = FLOAT_TO_CLICK(pl->vel.x * timeStep2);
    move.delta.cy = FLOAT_TO_CLICK(pl->vel.y * timeStep2);
#if 0
    pl->extmove.cx = move.delta.cx;
    pl->extmove.cy = move.delta.cy;
#endif

    if (BIT(pl->used, HAS_PHASING_DEVICE)) {
	int x = pl->pos.cx + move.delta.cx;
	int y = pl->pos.cy + move.delta.cy;
	while (x >= World.cwidth)
	    x -= World.cwidth;
	while (x < 0)
	    x += World.cwidth;
	while (y >= World.cheight)
	    y -= World.cheight;
	while (y < 0)
	    y += World.cheight;
	Player_position_set_clicks(pl, x, y);
    }
    else {
	if (pl->team != TEAM_NOT_SET)
	    move.hit_mask = NONBALL_BIT | 1 << pl->team;
	else
	    move.hit_mask = NONBALL_BIT | NOTEAM_BIT;
	move.start.cx = pl->pos.cx;
	move.start.cy = pl->pos.cy;
	while (move.delta.cx || move.delta.cy) {
	    Shape_move(&move, pl->ship, pl->dir, &ans);
	    move.start.cx = WRAP_XCLICK(move.start.cx + ans.moved.cx);
	    move.start.cy = WRAP_YCLICK(move.start.cy + ans.moved.cy);
	    move.delta.cx -= ans.moved.cx;
	    move.delta.cy -= ans.moved.cy;
	    if (ans.line != -1) {
		if (!Shape_away(&move, pl->ship, pl->dir,
				ans.line, &line, &point)) {
		    if (SIDE(pl->vel.x, pl->vel.y, ans.line) < 0)
			Bounce_player(pl, &move, ans.line, ans.point);
		    else if (SIDE(pl->vel.x, pl->vel.y, line) < 0)
			Bounce_player(pl, &move, line, point);
		    else {
			/* This case could be handled better,
			   I'll write the code for that if this
			   happens too often. */
			move.delta.cx = 0;
			move.delta.cy = 0;
			pl->vel.x = 0;
			pl->vel.y = 0;
		    }
		}
		else if (SIDE(pl->vel.x, pl->vel.y, ans.line) < 0)
		    Bounce_player(pl, &move, ans.line, ans.point);
	    }
	}
	Player_position_set_clicks(pl, move.start.cx, move.start.cy);
    }
    pl->velocity = VECTOR_LENGTH(pl->vel);
    /* !@# Better than ignoring collisions after wall touch for players,
     * but might cause some erroneous hits */
    pl->extmove.cx = CENTER_XCLICK(pl->pos.cx - pl->prevpos.cx);
    pl->extmove.cy = CENTER_YCLICK(pl->pos.cy - pl->prevpos.cy);
    return;
}


static void Turn_player_new(int ind)
{
    player	*pl = Players[ind];
    int		new_dir = MOD2((int)(pl->float_dir + 0.5f), RES);
    int		sign, hitmask;

    if (recOpt) {
	if (record)
	    *playback_data++ = new_dir;
	else if (playback)
	    new_dir = *playback_data++;
    }
    if (new_dir == pl->dir) {
	return;
    }
    if (BIT(pl->status, PLAYING|PAUSE|GAME_OVER|KILLED) != PLAYING) {
	pl->dir = new_dir;
	return;
    }

    if (new_dir > pl->dir)
	sign = (new_dir - pl->dir <= RES + pl->dir - new_dir) ? 1 : -1;
    else
	sign = (pl->dir - new_dir <= RES + new_dir - pl->dir) ? -1 : 1;

    if (pl->team != TEAM_NOT_SET)
	hitmask = NONBALL_BIT | 1 << pl->team;
    else
	hitmask = NONBALL_BIT | NOTEAM_BIT;

    /* kps - Mara "turnqueue" type hack  */
    /* someone clean this obfuscated code up */
    while (pl->dir != new_dir
	   && (Shape_turn1(pl->ship, hitmask, pl->pos.cx, pl->pos.cy,
			   pl->dir, sign) /*|| (pl->float_dir = pl->dir, 0)*/))
	pl->dir = MOD2(pl->dir + sign, RES);
    return;
}
