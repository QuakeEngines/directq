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

// vid.h -- video driver defs

#define VID_CBITS	6
#define VID_GRADES	(1 << VID_CBITS)

// a pixel can be one, two, or four bytes
typedef byte pixel_t;

typedef struct vrect_s
{
	int				x,y,width,height;
	struct vrect_s	*pnext;
} vrect_t;

typedef struct
{
	pixel_t			*colormap;		// 256 * VID_GRADES size
	bool			fullbright[256];	// true if a colour at this index is fullbright
	unsigned		rowbytes;	// may be > width if displayed in a window
	unsigned		width;
	unsigned		height;
	float			aspect;		// width / height -- < 0 is taller than wide
	int				recalc_refdef;	// if true, recalc vid-based stuff
} viddef_t;

extern	viddef_t	vid;				// global video state

void	VID_Shutdown (void);
// Called at shutdown

void	VID_Update (vrect_t *rects);
// flushes the given rectangles from the view buffer to the screen

int VID_SetMode (int modenum, unsigned char *palette);
// sets the mode; only used by the Quake engine for resetting to mode 0 (the
// base mode) on memory allocation failures
