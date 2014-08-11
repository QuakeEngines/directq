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


#include "pr_comp.h"			// defs shared with qcc
#include "progdefs.h"			// generated by program cdefs


// progs debugger
void QC_DebugOutput (char *debugtext, ...);

typedef union eval_s
{
	string_t		string;
	float			_float;
	float			vector[3];
	func_t			function;
	int				_int;
	int				edict;
} eval_t;

// edict field fast lookup offsets
extern int ed_alpha;
extern int ed_fullbright;
extern int ed_ammo_shells1;
extern int ed_ammo_nails1;
extern int ed_ammo_lava_nails;
extern int ed_ammo_rockets1;
extern int ed_ammo_multi_rockets;
extern int ed_ammo_cells1;
extern int ed_ammo_plasma;
extern int ed_items2;
extern int ed_gravity;

#define GETEDICTFIELDVALUEFAST(ed, fieldoffset) ((fieldoffset) ? (eval_t *) ((byte *) &(ed)->v + (fieldoffset)) : NULL)

// 16 is sufficient with the new bbox code; 32 is just for paranoia
#define MAX_ENT_LEAFS	32

typedef struct edict_s
{
	bool			free;
	link_t			area;			// linked to a division node or leaf
	int				num_leafs;
	unsigned short	leafnums[MAX_ENT_LEAFS];
	entity_state_t	baseline;
	float			freetime;		// time when the object was freed
	float			tracetimer;		// timer for cullentities tracing
	int				alphaval;		// consistent with entity_t so a search will find them both
	bool			sendinterval;
	int				ednum;
	int				Prog;

	// LordHavoc: for MOVETYPE_STEP interpolation
	vec3_t		steporigin;
	vec3_t		stepangles;
	vec3_t		stepoldorigin;
	vec3_t		stepoldangles;
	double		steplerptime;

	entvars_t		v;				// C exported fields from progs

	// DO NOT ADD ANY MEMBERS BEYOND THIS POINT - other fields from progs come immediately after
} edict_t;

#define	EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)

//============================================================================


//============================================================================

void PR_Init (void);

void ED_Free (edict_t *ed);

char	*ED_NewString (char *string);
// returns a copy of the string allocated from the server's string heap

void ED_Print (edict_t *ed);
void ED_Write (FILE *f, edict_t *ed);
char *ED_ParseEdict (char *data, edict_t *ent);

void ED_WriteGlobals (FILE *f);
void ED_ParseGlobals (char *data);

void ED_LoadFromFile (char *data);

edict_t *GetEdictForNumber (int n);
int GetNumberForEdict (edict_t *e);

#define	NEXT_EDICT2(e) ((edict_t *) ((byte *)e + SVProgs->EdictSize))
#define	NEXT_EDICT(e) (SVProgs->EdictPointers[(e)->ednum + 1])

#define	EDICT_TO_PROG2(e) ((byte *)e - (byte *)SVProgs->Edicts)
#define	EDICT_TO_PROG(e) ((e)->Prog)
#define PROG_TO_EDICT2(e) ((edict_t *) ((byte *)SVProgs->Edicts + e))
#define PROG_TO_EDICT(e) (SVProgs->EdictPointers[e / SVProgs->EdictSize])

//============================================================================

#define	G_FLOAT(o) (SVProgs->Globals[o])
#define	G_INT(o) (*(int *)&SVProgs->Globals[o])
#define	G_EDICT2(o) ((edict_t *) ((byte *) SVProgs->Edicts + *(int *) &SVProgs->Globals[o]))
#define	G_EDICT(o) (PROG_TO_EDICT(*(int *) &SVProgs->Globals[o]))
#define G_EDICTNUM(o) GetNumberForEdict(G_EDICT(o))
#define	G_VECTOR(o) (&SVProgs->Globals[o])
#define	G_STRING(o) (SVProgs->GetString (*(string_t *) &SVProgs->Globals[o]))
#define	G_FUNCTION(o) (*(func_t *) &SVProgs->Globals[o])

#define	E_FLOAT(e,o) (((float*)&e->v)[o])
#define	E_INT(e,o) (*(int *)&((float*)&e->v)[o])
#define	E_VECTOR(e,o) (&((float*)&e->v)[o])
#define	E_STRING(e,o) (SVProgs->GetString (*(string_t *)&((float*)&e->v)[o]))

extern	int		type_size[8];

typedef void (*builtin_t) (void);
extern	builtin_t *pr_builtins;
extern int pr_numbuiltins;

// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  start
typedef struct ebfs_builtin_s
{
	int			default_funcno;
	char		*funcname;
	builtin_t	function;
	int			funcno;
} ebfs_builtin_t;

extern ebfs_builtin_t	pr_ebfs_builtins[];
extern int				pr_ebfs_numbuiltins;

#define PR_DEFAULT_FUNCNO_BUILTIN_FIND	100

extern cvar_t	pr_builtin_find;
extern cvar_t	pr_builtin_remap;

#define PR_DEFAULT_FUNCNO_EXTENSION_FIND	99	// 2001-10-20 Extension System by Lord Havoc/Maddes
// 2001-09-14 Enhanced BuiltIn Function System (EBFS) by Maddes  end


void ED_PrintEdicts (void);
void ED_PrintNum (int ent);

eval_t *GetEdictFieldValue (edict_t *ed, char *field);

