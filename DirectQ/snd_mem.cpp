/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mem.c: sound caching

#include "quakedef.h"

// this is our new cache object for sounds
CQuakeCache *SoundCache = NULL;
CQuakeZone *SoundHeap = NULL;
int snd_NumSounds = 0;

/*
================
ResampleSfx
================
*/
void ResampleSfx (sfx_t *sfx, int inrate, int inwidth, void *data)
{
	int		outcount;
	int		srcsample;
	float	stepscale;
	sfxcache_t	*sc;

	sc = sfx->sndcache;

	// not in memory
	if (!sc)
	{
		// Con_Printf ("Sound %s not in memory\n", sfx->name);
		return;
	}

	if (inrate == shm->speed && inwidth == 2)
	{
		short *data16 = (short *) data;

		for (int i = 0; i < sc->length; i++)
			sc->data[i] = data16[i];

		return;
	}

	if (inrate == shm->speed && inwidth == 1)
	{
		byte *data8 = (byte *) data;

		for (int i = 0; i < sc->length; i++)
			sc->data[i] = ((int) data8[i] - 128) << 8;

		return;
	}

	// pitch-shift the sound to the new frequency
	stepscale = (float) inrate / shm->speed;	// this is usually 0.5, 1, or 2

	outcount = sc->length / stepscale;
	sc->length = outcount;

	if (sc->loopstart != -1)
		sc->loopstart = sc->loopstart / stepscale;

	// yayyy - no more 8-bit sounds
	sc->speed = shm->speed;
	sc->stereo = 0;

	int samplefrac = 0;
	int fracstep = stepscale * 256;

	for (int i = 0; i < outcount; i++)
	{
		srcsample = samplefrac >> 8;
		samplefrac += fracstep;

		if (inwidth == 2)
			sc->data[i] = ((short *) data)[srcsample];
		else sc->data[i] = ((int) ((unsigned char *) data)[srcsample] - 128) << 8;
	}
}


//=============================================================================

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s)
{
	char	namebuffer[256];
	byte	*data;
	wavinfo_t	info;
	int		len;
	float	stepscale;
	sfxcache_t	*sc;

	// already loaded
	if (s->sndcache) return s->sndcache;

	// look for a cached copy
	sc = (sfxcache_t *) SoundCache->Check (s->name);

	if (sc)
	{
		// use the cached version if it was found
		s->sndcache = sc;
		return sc;
	}

	// load it in
	strcpy (namebuffer, "sound/");
	strcat (namebuffer, s->name);

	// don't load as a stack file!!!  no way!!!  never!!!
	data = COM_LoadFile (namebuffer);

	if (!data)
	{
		Con_DPrintf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	info = GetWavinfo (s->name, data, com_filesize);

	if (info.channels != 1)
	{
		// we'll fix this later by converting it to mono
		Con_DPrintf ("%s is a stereo sample\n", s->name);
		Zone_Free (data);
		return NULL;
	}

	stepscale = (float) info.rate / shm->speed;
	len = info.samples / stepscale;
	len = len * 2 * info.channels;

	// alloc in the cache using s->name
	sc = (sfxcache_t *) SoundCache->Alloc (s->name, NULL, len + sizeof (sfxcache_t));
	s->sndcache = sc;

	if (!sc)
	{
		Zone_Free (data);
		return NULL;
	}

	sc->length = info.samples;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate;
	sc->stereo = info.channels;

	ResampleSfx (s, info.rate, info.width, data + info.dataofs);

	Zone_Free (data);
	snd_NumSounds++;
	return sc;
}



/*
===============================================================================

WAV loading

===============================================================================
*/


byte	*data_p;
byte 	*iff_end;
byte 	*last_chunk;
byte 	*iff_data;
int 	iff_chunk_len;


short GetLittleShort (void)
{
	short val = 0;

	val = *data_p;
	val = val + (* (data_p + 1) << 8);
	data_p += 2;

	return val;
}

int GetLittleLong (void)
{
	int val = 0;

	val = *data_p;
	val = val + (*(data_p + 1) << 8);
	val = val + (*(data_p + 2) << 16);
	val = val + (*(data_p + 3) << 24);
	data_p += 4;

	return val;
}

void FindNextChunk (char *name)
{
	while (1)
	{
		data_p = last_chunk;

		if (data_p >= iff_end)
		{
			// didn't find the chunk
			data_p = NULL;
			return;
		}

		data_p += 4;
		iff_chunk_len = GetLittleLong ();

		if (iff_chunk_len < 0)
		{
			data_p = NULL;
			return;
		}

		data_p -= 8;
		last_chunk = data_p + 8 + ( (iff_chunk_len + 1) & ~1);

		if (!strncmp ( (char *) data_p, name, 4))
			return;
	}
}

void FindChunk (char *name)
{
	last_chunk = iff_data;
	FindNextChunk (name);
}


void DumpChunks (void)
{
	char	str[5];

	str[4] = 0;
	data_p = iff_data;

	do
	{
		memcpy (str, data_p, 4);
		data_p += 4;
		iff_chunk_len = GetLittleLong ();
		Con_Printf ("0x%x : %s (%d)\n", (int) (data_p - 4), str, iff_chunk_len);
		data_p += (iff_chunk_len + 1) & ~1;
	}
	while (data_p < iff_end);
}


/*
============
GetWavinfo
============
*/
wavinfo_t GetWavinfo (char *name, byte *wav, int wavlength)
{
	wavinfo_t	info;
	int     format;
	int		samples;

	memset (&info, 0, sizeof (info));

	if (!wav)
		return info;

	iff_data = wav;
	iff_end = wav + wavlength;

	// find "RIFF" chunk
	FindChunk ("RIFF");

	if (!(data_p && !strncmp ((char *) data_p + 8, "WAVE", 4)))
	{
		Con_DPrintf ("Missing RIFF/WAVE chunks\n");
		return info;
	}

	// get "fmt " chunk
	iff_data = data_p + 12;
	// DumpChunks ();

	FindChunk ("fmt ");

	if (!data_p)
	{
		Con_DPrintf ("Missing fmt chunk\n");
		return info;
	}

	data_p += 8;
	format = GetLittleShort ();

	if (format != 1)
	{
		Con_DPrintf ("Microsoft PCM format only\n");
		return info;
	}

	// we need all of this for our WAVEFORMATEX struct
	info.channels = GetLittleShort ();
	info.rate = GetLittleLong ();
	info.byterate = GetLittleLong ();
	info.blockalign = GetLittleShort ();
	info.width = GetLittleShort () / 8;

	// get cue chunk
	FindChunk ("cue ");

	if (data_p)
	{
		data_p += 32;
		info.loopstart = GetLittleLong ();

		// if the next chunk is a LIST chunk, look for a cue length marker
		FindNextChunk ("LIST");

		if (data_p)
		{
			if (!strncmp ((char *) data_p + 28, "mark", 4))
			{
				// this is not a proper parse, but it works with cooledit...
				data_p += 24;
				int i = GetLittleLong ();	// samples in loop
				info.samples = info.loopstart + i;
			}
		}
	}
	else info.loopstart = -1;

	// find data chunk
	FindChunk ("data");

	if (!data_p)
	{
		Con_DPrintf ("Missing data chunk\n");
		return info;
	}

	data_p += 4;
	samples = GetLittleLong () / info.width;

	if (info.samples)
	{
		if (samples < info.samples)
			Sys_Error ("Sound %s has a bad loop length", name);
	}
	else info.samples = samples;

	info.dataofs = data_p - wav;

	return info;
}

