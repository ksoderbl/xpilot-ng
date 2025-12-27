/* $Id: play.c,v 5.10 2001/12/11 12:45:13 bertg Exp $
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
#include <limits.h>
#include <math.h>
#include <time.h>

#ifdef _WINDOWS
# include "NT/winServer.h"
#endif

#define SERVER
#include "version.h"
#include "config.h"
#include "serverconst.h"
#include "global.h"
#include "proto.h"
#include "saudio.h"
#include "score.h"
#include "objpos.h"
#include "click.h"
#include "object.h"

char play_version[] = VERSION;


int Punish_team(int ind, int t_destroyed, int cx, int cy)
{
    static char		msg[MSG_LEN];
    treasure_t		*td = &World.treasures[t_destroyed];
    player		*pl = Players[ind];
    int			i;
    int			win_score = 0,lose_score = 0;
    int			win_team_members = 0, lose_team_members = 0;
    int			somebody_flag = 0;
    DFLOAT		sc, por;

    Check_team_members (td->team);
    if (td->team == pl->team)
	return 0;

    if (BIT(World.rules->mode, TEAM_PLAY)) {
	for (i = 0; i < NumPlayers; i++) {
	    if (IS_TANK_IND(i)
		|| (BIT(Players[i]->status, PAUSE)
		    && Players[i]->count <= 0)
		|| (BIT(Players[i]->status, GAME_OVER)
		    && Players[i]->mychar == 'W'
		    && Players[i]->score == 0)) {
		continue;
	    }
	    if (Players[i]->team == td->team) {
		lose_score += Players[i]->score;
		lose_team_members++;
		if (BIT(Players[i]->status, GAME_OVER) == 0) {
		    somebody_flag = 1;
		}
	    }
	    else if (Players[i]->team == pl->team) {
		win_score += Players[i]->score;
		win_team_members++;
	    }
	}
    }

    sound_play_all(DESTROY_BALL_SOUND);
    sprintf(msg, " < %s's (%d) team has destroyed team %d treasure >",
	    pl->name, pl->team, td->team);
    Set_message(msg);

    if (!somebody_flag) {
	SCORE(ind, Rate(pl->score, CANNON_SCORE)/2, cx, cy, "Treasure:");
	return 0;
    }

    td->destroyed++;
    World.teams[td->team].TreasuresLeft--;
    World.teams[pl->team].TreasuresDestroyed++;


    sc  = 3 * Rate(win_score, lose_score);
    por = (sc * lose_team_members) / (2 * win_team_members + 1);

    for (i = 0; i < NumPlayers; i++) {
	if (IS_TANK_IND(i)
	    || (BIT(Players[i]->status, PAUSE)
		&& Players[i]->count <= 0)
	    || (BIT(Players[i]->status, GAME_OVER)
		&& Players[i]->mychar == 'W'
		&& Players[i]->score == 0)) {
	    continue;
	}
	if (Players[i]->team == td->team) {
	    SCORE(i, -sc, cx, cy, "Treasure: ");
	    Rank_LostBall(Players[i]);
	    if (treasureKillTeam)
		SET_BIT(Players[i]->status, KILLED);
	}
	else if (Players[i]->team == pl->team &&
		 (Players[i]->team != TEAM_NOT_SET || i == ind)) {
	    if (lose_team_members > 0) {
		if (i == ind) {
		    Rank_CashedBall(Players[i]);
		}
		Rank_WonBall(Players[i]);
	    }
	    SCORE(i, (i == ind ? 3*por : 2*por), cx, cy, "Treasure: ");
	}
    }

    if (treasureKillTeam) {
	Rank_AddKill(Players[ind]);
    }

    updateScores = true;

    return 1;
}


/****************************
 * Functions for explosions.
 */

/* Create debris particles */
void Make_debris(
    /* pos.x, pos.y   */ int    cx,          int   cy,
    /* vel.x, vel.y   */ DFLOAT  velx,       DFLOAT vely,
    /* owner id       */ int    id,
    /* owner team     */ unsigned short team,
    /* type           */ int    type,
    /* mass           */ DFLOAT  mass,
    /* status         */ long   status,
    /* color          */ int    color,
    /* radius         */ int    radius,
    /* num debris     */ int    num_debris,
    /* min,max dir    */ int    min_dir,    int    max_dir,
    /* min,max speed  */ DFLOAT  min_speed,  DFLOAT  max_speed,
    /* min,max life   */ int    min_life,   int    max_life
)
{
    object		*debris;
    int			i, life;
    modifiers		mods;

    cx = WRAP_XCLICK(cx);
    cy = WRAP_YCLICK(cy);

    if (max_life < min_life)
	max_life = min_life;

    if (max_speed < min_speed)
	max_speed = min_speed;

    CLEAR_MODS(mods);

    if (type == OBJ_SHOT) {
	SET_BIT(mods.warhead, CLUSTER);
	if (!ShotsGravity) {
	    CLR_BIT(status, GRAVITY);
	}
    }

    if (num_debris > MAX_TOTAL_SHOTS - NumObjs) {
	num_debris = MAX_TOTAL_SHOTS - NumObjs;
    }
    for (i = 0; i < num_debris; i++) {
	DFLOAT		speed, dx, dy, diroff;
	int		dir, dirplus;

	if ((debris = Object_allocate()) == NULL) {
	    break;
	}

	debris->color = color;
	debris->id = id;
	debris->team = team;
	Object_position_init_clicks(debris, cx, cy);
	dir = MOD2(min_dir + (int)(rfrac() * (max_dir - min_dir)), RES);
	dirplus = MOD2(dir + 1, RES);
	diroff = rfrac();
	dx = tcos(dir) + (tcos(dirplus) - tcos(dir)) * diroff;
	dy = tsin(dir) + (tsin(dirplus) - tsin(dir)) * diroff;
	speed = min_speed + rfrac() * (max_speed - min_speed);
	debris->vel.x = velx + dx * speed;
	debris->vel.y = vely + dy * speed;
	debris->acc.x = 0;
	debris->acc.y = 0;
	if (shotHitFuelDrainUsesKineticEnergy
	    && type == OBJ_SHOT) {
	    /* compensate so that m*v^2 is constant */
	    DFLOAT sp_shotsp = speed / ShotsSpeed;
	    debris->mass = mass / (sp_shotsp * sp_shotsp);
	} else {
	    debris->mass = mass;
	}
	debris->type = type;
	life = (int)(min_life + rfrac() * (max_life - min_life) + 1);
	debris->life = life;
	/*debris->fuselife = life;*/
	debris->fuseframe = 0;
	debris->pl_range = radius;
	debris->pl_radius = radius;
	debris->status = status;
	debris->mods = mods;
	Cell_add_object(debris);
    }
}


void Ball_is_replaced(ballobject *ball, treasure_t *tt, player *pl)
{
    char msg[MSG_LEN];

    ball->life = 0;
    SET_BIT(ball->status, (NOEXPLOSION|RECREATE));
    
    SCORE(GetInd[pl->id], 5, tt->pos.x, tt->pos.y, "Treasure: ");
    sprintf(msg, " < %s (team %d) has replaced the treasure >",
	    pl->name, pl->team);
    Set_message(msg);
    Rank_SavedBall(pl);
}


void Ball_is_destroyed(ballobject *ball)
{
    char msg[MSG_LEN];
    long frames = (LONG_MAX - ball->life) / timeStep;
    int ind = GetInd[ball->owner];
    DFLOAT seconds = ((DFLOAT)frames) / framesPerSecond;
    DFLOAT frames12 = ((DFLOAT)frames) / FPSMultiplier;

    /*
     * Ball has been brought back to home treasure.
     * The team should be punished.
     */
    sprintf(msg," < The ball was loose for %ld frames (best %d) "
	    "/ %.2f frames @ 12fps / %.2f seconds >",
	    frames, Rank_GetBestBall(Players[ind]), frames12, seconds);
    Set_message(msg);
    Rank_BallRun(Players[ind], frames);
}



void Ball_hits_goal(ballobject *ball, int group)
{
    if (ball->owner == NO_ID) {	/* Probably the player quit */
	SET_BIT(ball->status, (NOEXPLOSION|RECREATE));
	return;
    }
    if (World.treasures[ball->treasure].team == groups[group].team) {
	/*
	 * Ball has been replaced back in the hoop from whence
	 * it came. The player must be from the same team as the ball,
	 * otherwise Bounce_object() wouldn't have been called. It
	 * should be replaced into the hoop without exploding and
	 * the player gets some points.
	 */
	Ball_is_replaced(ball, &World.treasures[ball->treasure],
			 Players[GetInd[ball->owner]]);
	return;
    }
    Ball_is_destroyed(ball);
    if (Punish_team(GetInd[ball->owner], ball->treasure,
		    ball->pos.cx, ball->pos.cy))
	CLR_BIT(ball->status, RECREATE);
}
