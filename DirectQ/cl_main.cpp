/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "d3d_model.h"
#include "cl_fx.h"


cvar_t	cl_web_download	("cl_web_download", "1");
cvar_t	cl_web_download_url	("cl_web_download_url", "http://bigfoot.quake1.net/"); // the quakeone.com link is dead //"http://downloads.quakeone.com/");

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name ("_cl_name", "player", CVAR_ARCHIVE);
cvar_t	cl_color ("_cl_color", "0", CVAR_ARCHIVE);

cvar_t	cl_shownet ("cl_shownet", "0");	// can be 0, 1, or 2
cvar_t	cl_nolerp ("cl_nolerp", "0");

cvar_t	cl_autoaim ("cl_autoaim", "0");
cvar_t	lookspring ("lookspring", "0", CVAR_ARCHIVE);
cvar_t	lookstrafe ("lookstrafe", "0", CVAR_ARCHIVE);
cvar_t	sensitivity ("sensitivity", "3", CVAR_ARCHIVE);

cvar_t	m_pitch ("m_pitch", "0.022", CVAR_ARCHIVE);
cvar_t	m_yaw ("m_yaw", "0.022", CVAR_ARCHIVE);
cvar_t	m_forward ("m_forward", "0", CVAR_ARCHIVE);
cvar_t	m_side ("m_side", "0.8", CVAR_ARCHIVE);

// proquake nat fix
cvar_t	cl_natfix ("cl_natfix", "1");

client_static_t	cls;
client_state_t	cl;

// FIXME: put these on hunk?
// consider it done ;)
#define BASE_ENTITIES	512

entity_t		**cl_entities = NULL;
lightstyle_t	*cl_lightstyle;
dlight_t		*cl_dlights = NULL;

// visedicts now belong to the render
void D3D_AddVisEdict (entity_t *ent);
void D3D_BeginVisedicts (void);

// save and restore anything that needs it while wiping the cl struct
void CL_ClearCLStruct (void)
{
	client_state_t *oldcl = (client_state_t *) scratchbuf;

	memcpy (oldcl, &cl, sizeof (client_state_t));
	memset (&cl, 0, sizeof (client_state_t));

	// now restore the stuff we want to persist between servers
	cl.death_location[0] = oldcl->death_location[0];
	cl.death_location[1] = oldcl->death_location[1];
	cl.death_location[2] = oldcl->death_location[2];
}


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int i;

	// this really only happens with demos
	if (!sv.active) Host_ClearMemory ();

	SAFE_DELETE (ClientZone);
	ClientZone = new CQuakeZone ();

	// wipe the entire cl structure
	CL_ClearCLStruct ();

	SZ_Clear (&cls.message);

	cl.teamscores = (teamscore_t *) ClientZone->Alloc (sizeof (teamscore_t) * 16);

	// clear down anything that was allocated one-time-only at startup
	memset (cl_dlights, 0, MAX_DLIGHTS * sizeof (dlight_t));
	memset (cl_lightstyle, 0, MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	// allocate space for the first 512 entities - also clears the array
	// the remainder are left at NULL and allocated on-demand if they are ever needed
	for (i = 0; i < MAX_EDICTS; i++)
	{
		if (i < BASE_ENTITIES)
		{
			cl_entities[i] = (entity_t *) ClientZone->Alloc (sizeof (entity_t));
			cl_entities[i]->entnum = i;
		}
		else cl_entities[i] = NULL;
	}

	// ensure that effects get spawned on the first client frame
	cl.nexteffecttime = -1;

	// start with a valid view entity number as otherwise we will try to read the world and we'll have an invalid entity!!!
	cl.viewentity = 1;
}


/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void SHOWLMP_clear (void);

void CL_Disconnect (void)
{
	// stop sounds (especially looping!)
	S_StopAllSounds (true);

	SHOWLMP_clear ();

	// We have to shut down webdownloading first
	if (cls.download.web)
	{
		cls.download.disconnect = true;
		return;
	}

	// NULL the worldmodel so that rendering won't happen
	// (this is crap)
	cl.worldmodel = NULL;

	// stop all intermissions (the intermission screen prints the map name so
	// this is important for preventing a crash)
	cl.intermission = 0;

	// remove all center prints
	SCR_ClearCenterString ();

	// clear the map from the title bar
	UpdateTitlebarText ();

	// and tell it that we're not running a map either
	cls.maprunning = false;

	// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;

		if (sv.active)
			Host_ShutdownServer (false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	if (cls.download.web)
	{
		cls.download.disconnect = true;
		return;
	}

	CL_Disconnect ();

	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_KeepaliveMessage (bool showmsg = true);

void CL_EstablishConnection (char *host)
{
	if (cls.demoplayback) return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);

	if (!cls.netcon) Host_Error ("CL_Connect: connect failed\n");

	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing

	if (cl_natfix.integer) MSG_WriteByte (&cls.message, clc_nop); // ProQuake NAT Fix
}


/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char *str = (char *) scratchbuf;

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("name \"%s\"\n", cl_name.string));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("color %i %i\n", ((int) cl_color.value) >> 4, ((int) cl_color.value) & 15));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		_snprintf (str, 8192, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		break;

	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	// this causes lockups when switching between demos in warpspasm.
	// let's just not do it. ;)
	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;

		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	_snprintf (str, 1024, "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}


/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	int i;

	for (i = 0; i < cl.num_entities; i++)
	{
		entity_t *ent = cl_entities[i];

		Con_Printf ("%3i:", i);

		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}

		Con_Printf
		(
			"%s:%3i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
			ent->model->name,
			ent->frame,
			ent->origin[0],
			ent->origin[1],
			ent->origin[2],
			ent->angles[0],
			ent->angles[1],
			ent->angles[2]
		);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal (int i)
{
}


/*
========================================================================================================================

		A NOTE ON INTERPOLATION

	3 types of interpolation are used: client-side lerp-point, server-side lerp-point and client-side transform.

	in order to keep everything smoothly synced up with no jerkiness or positioning glitches (there is one small one
	still; i'll come to that later), the following rules apply:

	- if connected to a server locally, server-side lerp-point is the main method of interpolation.  this ensures that
	  everything on the server (which is the master copy of the entity data) is synced up and smoothed out when a
	  server frame runs.  client-side lerp-point is used to fill in the gaps.

	- if connected to a server remotely, a combination of client-side lerp-point and client-side transform is used.
	  server-side lerp-point is not used as (obviously) the server is on a different machine.

	about that one glitch - monsters on moving platforms will sometimes go out of sync with the platform.  it's not
	really noticeable (unless you're looking for it) and only happens when connected to a local server.

========================================================================================================================
*/


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo)// || sv.active)
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1)
	{
		// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.time - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
			cl.time = cl.mtime[1];

		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
			cl.time = cl.mtime[0];

		frac = 1;
	}

	if (cl_nolerp.value)
		return 1;

	return frac;
}


void CL_EntityInterpolateOrigins (entity_t *ent)
{
	float timepassed = cl.time - ent->translatestarttime;
	float blend = 0;
	vec3_t delta = {0, 0, 0};

	if (ent->translatestarttime < 0.001 || timepassed > 1)
	{
		ent->translatestarttime = cl.time;

		VectorCopy (ent->origin, ent->lastorigin);
		VectorCopy (ent->origin, ent->currorigin);

		return;
	}

	if (!VectorCompare (ent->origin, ent->currorigin))
	{
		ent->translatestarttime = cl.time;

		VectorCopy (ent->currorigin, ent->lastorigin);
		VectorCopy (ent->origin,  ent->currorigin);

		blend = 0;
	}
	else
	{
		blend = timepassed / 0.1;

		if (cl.paused || blend > 1) blend = 1;
	}

	VectorSubtract (ent->currorigin, ent->lastorigin, delta);

	ent->origin[0] = ent->lastorigin[0] + delta[0] * blend;
	ent->origin[1] = ent->lastorigin[1] + delta[1] * blend;
	ent->origin[2] = ent->lastorigin[2] + delta[2] * blend;
}


void CL_EntityInterpolateAngles (entity_t *ent)
{
	float timepassed = cl.time - ent->rotatestarttime;
	float blend = 0;

	if (ent->rotatestarttime < 0.001 || timepassed > 1)
	{
		ent->rotatestarttime = cl.time;

		VectorCopy (ent->angles, ent->lastangles);
		VectorCopy (ent->angles, ent->currangles);

		return;
	}

	if (!VectorCompare (ent->angles, ent->currangles))
	{
		ent->rotatestarttime = cl.time;

		VectorCopy (ent->currangles, ent->lastangles);
		VectorCopy (ent->angles,  ent->currangles);

		blend = 0;
	}
	else
	{
		blend = timepassed / 0.1;

		if (cl.paused || blend > 1) blend = 1;
	}

	for (int i = 0; i < 3; i++)
	{
		float delta = ent->currangles[i] - ent->lastangles[i];

		// always interpolate along the shortest path
		if (delta > 180)
			delta -= 360;
		else if (delta < -180)
			delta += 360;

		ent->angles[i] = ent->lastangles[i] + delta * blend;
	}
}


float CL_EntityLerpPoint (void)
{
	float	f;

	if (cl_nolerp.value || cls.timedemo)// || (sv.active && svs.maxclients == 1))
		return 1;

	f = cl.mtime[0] - cl.mtime[1];
	//	Con_Printf(" %g-%g=%g", ent->state_current.time, ent->state_previous.time, f);

	if (f <= 0)
		return 1;

	if (f >= 0.1)
		f = 0.1;

	//	Con_Printf(" %g-%g/%g=%f", cl.time, ent->state_previous.time, f, (cl.time - ent->state_previous.time) / f);
	f = (cl.time - cl.mtime[1]) / f;

	if (f < 0) return 0;
	if (f > 1) return 1;

	return f;
}


cvar_t cl_itembobheight ("cl_itembobheight", 0.0f);
cvar_t cl_itembobspeed ("cl_itembobspeed", 0.5f);
cvar_t cl_itemrotatespeed ("cl_itemrotatespeed", 100.0f);

void CL_RelinkEntities (void)
{
	extern cvar_t r_lerporient;

	// get the fractional lerp time
	float frac = CL_LerpPoint ();
	float bobjrotate = anglemod (cl_itemrotatespeed.value * cl.time);

	// update frametime after CL_LerpPoint as it can change the value of cl.time
	cl.frametime = cl.time - cl.oldtime;

	// interpolate player info
	for (int i = 0; i < 3; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
		// interpolate the angles
		for (int j = 0; j < 3; j++)
		{
			float d = cl.mviewangles[0][j] - cl.mviewangles[1][j];

			if (d > 180) d -= 360; else if (d < -180) d += 360;

			cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
		}
	}

	for (int i = 1; i < cl.num_entities; i++)
	{
		entity_t *ent = cl_entities[i];
		dlight_t *dl = NULL;

		if (!ent) continue;

		// if the object wasn't included in the last packet, remove it
		// (intentionally falls through to next condition)
		if (ent->msgtime != cl.mtime[0]) ent->model = NULL;

		// doesn't have a model
		if (!ent->model)
		{
			// clear it's interpolation data too
			CL_ClearInterpolation (ent, CLEAR_ALLLERP);
			continue;
		}

		if (i == cl.viewentity) ent->lerpflags &= ~LERP_MOVESTEP;

		if (ent->forcelink)
		{
			// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy2 (ent->origin, ent->msg_origins[0]);
			VectorCopy2 (ent->angles, ent->msg_angles[0]);
		}
		else
		{
			float delta[3];
			float f = CL_EntityLerpPoint (); //frac;

			// if the delta is large, assume a teleport and don't lerp
			for (int j = 0; j < 3; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];

				if (delta[j] > 100 || delta[j] < -100)
				{
					CL_ClearInterpolation (ent, CLEAR_ALLLERP);
					f = 1;
				}
			}

			// if we're interpolating position/angles don't interpolate them here
			if (r_lerporient.integer && (ent->lerpflags & LERP_MOVESTEP) && !sv.active) f = 1;

			for (int j = 0; j < 3; j++)
			{
				// interpolate the origin and angles
				ent->origin[j] = ent->msg_origins[1][j] + f * delta[j];

				float d = ent->msg_angles[0][j] - ent->msg_angles[1][j];

				// interpolate along the shortest path
				if (d > 180) d -= 360; else if (d < -180) d += 360;

				ent->angles[j] = ent->msg_angles[1][j] + f * d;
			}
		}

		// now run the interpolation for rendering
		// this is done here instead of in the renderer so that we don't include static ents or temp ents
		// (transform interpolation is now done on the server if we have an active local server)
		if (r_lerporient.integer && (ent->lerpflags & LERP_MOVESTEP) && !sv.active)
		{
			CL_EntityInterpolateOrigins (ent);
			CL_EntityInterpolateAngles (ent);
		}
		else
		{
			// clear interpolation to current values
			ent->translatestarttime = 0;
			ent->rotatestarttime = 0;

			VectorCopy (ent->origin, ent->lastorigin);
			VectorCopy (ent->origin, ent->currorigin);

			VectorCopy (ent->angles, ent->lastangles);
			VectorCopy (ent->angles, ent->currangles);
		}

		// rotate binary objects locally (this affects entity state rather than being an effect so it's done here)
		if (ent->model->flags & EF_ROTATE)
		{
			// allow bouncing items for those who like them
			if (cl_itembobheight.value > 0.0f)
				ent->origin[2] += (cos ((cl.time + ent->entnum) * cl_itembobspeed.value * (2.0f * D3DX_PI)) + 1.0f) * 0.5f * cl_itembobheight.value;

			// bugfix - a rotating backpack spawned from a dead player gets the same angles as the player
			// if it was spawned when the player is not upright (e.g. killed by a rocket or similar) and
			// it inherits the players entity_t struct
			ent->angles[0] = 0;
			ent->angles[1] = bobjrotate;
			ent->angles[2] = 0;
		}

		// these need to spawn from here as they are per-frame dynamic effects
		if (ent->effects & EF_MUZZLEFLASH) CL_MuzzleFlash (ent, i);
		if (ent->effects & EF_BRIGHTLIGHT) CL_BrightLight (ent, i);
		if (ent->effects & EF_DIMLIGHT) CL_DimLight (ent, i);

		// this is now animated on the GPU so it needs to be emitted every frame
		if (ent->effects & EF_BRIGHTFIELD) R_EntityParticles (ent);

		// testing
		// R_EntityParticles (ent);

		// check for timed effects
		if (cl.time >= cl.nexteffecttime)
		{
			if (ent->model->flags & EF_WIZARDTRAIL)
				CL_WizardTrail (ent, i);
			else if (ent->model->flags & EF_KNIGHTTRAIL)
				CL_KnightTrail (ent, i);
			else if (ent->model->flags & EF_ROCKET)
				CL_RocketTrail (ent, i);
			else if (ent->model->flags & EF_VORETRAIL)
				CL_VoreTrail (ent, i);
			else if (ent->model->flags & EF_GIB)
				R_RocketTrail (ent->oldorg, ent->origin, RT_GIB);
			else if (ent->model->flags & EF_ZOMGIB)
				R_RocketTrail (ent->oldorg, ent->origin, RT_ZOMGIB);
			else if (ent->model->flags & EF_GRENADE)
				R_RocketTrail (ent->oldorg, ent->origin, RT_GRENADE);

			// and update the old origin
			VectorCopy2 (ent->oldorg, ent->origin);
		}

		ent->forcelink = false;

		extern bool chase_nodraw;

		// chasecam test
		if (i == cl.viewentity && !chase_active.value) continue;
		if (i == cl.viewentity && chase_nodraw) continue;

		// the entity is a visedict now
		D3D_AddVisEdict (ent);
	}
}


/*
===============
CL_UpdateClient

Read all incoming data from the server
===============
*/
void CL_UpdateClient (double frametime, bool readfromserver)
{
	// time is always advanced even if we're not reading from the server
	cl.oldtime = cl.time;
	cl.time += frametime;

	// initial frametime in case anything needs it
	cl.frametime = cl.time - cl.oldtime;

	// reading from the server is optional
	// (fixme - put a "no message" message at the start of the net_message buffer instead...)
	if (readfromserver)
	{
		do
		{
			int ret = CL_GetMessage ();

			if (ret == -1) Host_Error ("CL_UpdateClient: lost server connection");
			if (!ret) break;

			cl.lastrecievedmessage = realtime;
			CL_ParseServerMessage ();
		} while (cls.state == ca_connected);

		if (cl_shownet.value) Con_Printf ("\n");
	}
}


void CL_PrepEntitiesForRendering (void)
{
	if (cls.state == ca_connected)
	{
		// reset visedicts count and structs
		D3D_BeginVisedicts ();

		// entity states are always brought up to date, even if not reading from the server
		CL_RelinkEntities ();
		CL_UpdateTEnts ();

		// set time for the next set of effects to fire
		if (cl.time >= cl.nexteffecttime)
		{
			if (cl_effectrate.value > 0)
				cl.nexteffecttime = cl.time + (1.0 / cl_effectrate.value);
			else cl.nexteffecttime = cl.time;
		}
	}
}


/*
=================
CL_Init
=================
*/
cmd_t CL_PrintEntities_f_Cmd ("entities", CL_PrintEntities_f);
cmd_t CL_Disconnect_f_Cmd ("disconnect", CL_Disconnect_f);
cmd_t CL_Record_f_Cmd ("record", CL_Record_f);
cmd_t CL_Stop_f_Cmd ("stop", CL_Stop_f);
cmd_t CL_PlayDemo_f_Cmd ("playdemo", CL_PlayDemo_f);
cmd_t CL_TimeDemo_f_Cmd ("timedemo", CL_TimeDemo_f);


void CL_Init (void)
{
	CL_InitFX ();

	SZ_Alloc (&cls.message, 1024);

	// allocated here because (1) it's just a poxy array of pointers, so no big deal, and (2) it's
	// referenced during a changelevel (?only when there's no intermission?) so it needs to be in
	// the persistent heap, and (3) the full thing is allocated each map anyway, so why not shift
	// the overhead (small as it is) to one time only.
	cl_entities = (entity_t **) Zone_Alloc (MAX_EDICTS * sizeof (entity_t *));

	// these were static arrays but we put them into memory pools so that we can track usage more accurately
	cl_dlights = (dlight_t *) Zone_Alloc (MAX_DLIGHTS * sizeof (dlight_t));
	cl_lightstyle = (lightstyle_t *) Zone_Alloc (MAX_LIGHTSTYLES * sizeof (lightstyle_t));

	CL_InitInput ();
}


