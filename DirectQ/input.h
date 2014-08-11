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

// input.h -- external (non-keyboard) input devices

void IN_Init (void);
void IN_Shutdown (void);

void IN_Commands (void);
void IN_JoyMove (usercmd_t *cmd, double movetime);
void IN_MouseMove (usercmd_t *cmd, double movetime);

void IN_ClearMouseState (void);
void IN_SetMouseState (bool fullscreen);

