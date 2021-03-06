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
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "resource.h"
#include <shlobj.h>


int profilestart;
int profileend;

HWND hwndSplash;
SYSTEM_INFO SysInfo;
bool bWindowActive = false;
void AllowAccessibilityShortcutKeys (bool bAllowKeys);

int Sys_LoadResourceData (int resourceid, void **resbuf)
{
	// per MSDN, UnlockResource is obsolete and does nothing any more.  There is
	// no way to free the memory used by a resource after you're finished with it.
	// If you ask me this is kinda fucked, but what do I know?  We'll just leak it.
	HRSRC hResInfo = FindResource (NULL, MAKEINTRESOURCE (resourceid), RT_RCDATA);
	HGLOBAL hResData = LoadResource (NULL, hResInfo);
	resbuf[0] = (byte *) LockResource (hResData);
	return SizeofResource (NULL, hResInfo);
}


// we need this a lot so define it just once
HRESULT hr = S_OK;

#define PAUSE_SLEEP		50				// sleep time on pause or minimization
#define NOT_FOCUS_SLEEP	20				// sleep time when not focus

BOOL	ActiveApp, Minimized;
bool	WinNT = false;

static HANDLE	hFile;
static HANDLE	heventParent;
static HANDLE	heventChild;


int	Sys_FileExists (char *path)
{
	FILE	*f;

	if ((f = fopen (path, "rb")))
	{
		fclose (f);
		return 1;
	}

	return 0;
}


/*
==================
Sys_mkdir

A better Sys_mkdir.

Uses the Windows API instead of direct.h for better compatibility.
Doesn't need com_gamedir included in the path to make.
Will make all elements of a deeply nested path.
==================
*/
void Sys_mkdir (char *path)
{
	char fullpath[256];

	// if a full absolute path is given we just copy it out, otherwise we build from the gamedir
	if (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'))
		Q_strncpy (fullpath, path, 255);
	else _snprintf (fullpath, 255, "%s/%s", com_gamedir, path);

	for (int i = 0;; i++)
	{
		if (!fullpath[i]) break;

		if (fullpath[i] == '/' || fullpath[i] == '\\')
		{
			// correct seperator
			fullpath[i] = '\\';

			if (i > 3)
			{
				// make all elements of the path
				fullpath[i] = 0;
				CreateDirectory (fullpath, NULL);
				fullpath[i] = '\\';
			}
		}
	}

	// final path
	CreateDirectory (fullpath, NULL);
}


/*
================
Sys_Init
================
*/
void Sys_Init (void)
{
}


void Sys_Error (char *error, ...)
{
	extern LPDIRECT3DDEVICE9 d3d_Device;
	extern HWND d3d_Window;

	va_list		argptr;
	char		text[1024];
	static int	in_sys_error0 = 0;
	static int	in_sys_error1 = 0;
	static int	in_sys_error2 = 0;
	static int	in_sys_error3 = 0;

	if (d3d_Device)
	{
		// we're going down so make sure that the user can see the error
		d3d_Device->SetDialogBoxMode (TRUE);
	}

	if (!in_sys_error3)
	{
		in_sys_error3 = 1;
	}

	va_start (argptr, error);
	_vsnprintf (text, 1024, error, argptr);
	va_end (argptr);

	QC_DebugOutput ("Sys_Error: %s", text);

	// switch to windowed so the message box is visible, unless we already tried that and failed
	if (!in_sys_error0)
	{
		in_sys_error0 = 1;
		MessageBox (d3d_Window, text, "Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
	}
	else MessageBox (d3d_Window, text, "Double Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);

	if (!in_sys_error1)
	{
		in_sys_error1 = 1;
		Host_Shutdown ();
	}

	// shut down QHOST hooks if necessary
	if (!in_sys_error2)
	{
		in_sys_error2 = 1;
	}

	exit (666);
}


void Sys_Quit (int ExitCode)
{
	Host_Shutdown ();
	AllowAccessibilityShortcutKeys (true);
	timeEndPeriod (1);
	exit (ExitCode);
}


static __int64 sys_firstqpc;
static __int64 sys_qpcfreq;
static DWORD sys_milliseconds;

void Sys_InitTimers (void)
{
	// this just gets the starting timers
	QueryPerformanceFrequency ((LARGE_INTEGER *) &sys_qpcfreq);
	QueryPerformanceCounter ((LARGE_INTEGER *) &sys_firstqpc);

	sys_milliseconds = timeGetTime ();
}


void Sys_ChangeTimer (cvar_t *var)
{
	// update the timer resolutions
	if (var->value)
	{
		QueryPerformanceFrequency ((LARGE_INTEGER *) &sys_qpcfreq);
		timeBeginPeriod (1);
	}
	else
	{
		QueryPerformanceFrequency ((LARGE_INTEGER *) &sys_qpcfreq);
		timeEndPeriod (1);
	}
}


cvar_t sys_usetimegettime ("sys_usetimegettime", "1", CVAR_ARCHIVE, Sys_ChangeTimer);

DWORD Sys_Milliseconds (void)
{
	if (sys_usetimegettime.value)
	{
		return (timeGetTime () - sys_milliseconds);
	}
	else
	{
		__int64 qpccount;

		QueryPerformanceCounter ((LARGE_INTEGER *) &qpccount);

		return ((((qpccount - sys_firstqpc) * 1000) + (sys_qpcfreq >> 1)) / sys_qpcfreq);
	}
}


double Sys_FloatTime (void)
{
	static DWORD starttime = 0;
	static bool firsttime = true;

	if (firsttime)
	{
		starttime = Sys_Milliseconds ();
		firsttime = false;
		return 0;
	}

	DWORD now = Sys_Milliseconds ();

	return (double) (now - starttime) * 0.001;
}


void Sys_SendKeyEvents (void)
{
	MSG		msg;

	while (PeekMessage (&msg, NULL, 0, 0, PM_NOREMOVE))
	{
		// we always update if there are any events, even if we're paused
		scr_skipupdate = 0;

		// GetMessage returns -1 if there was an error, 0 if WM_QUIT
		if (GetMessage (&msg, NULL, 0, 0) > 0)
		{
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
		else Sys_Quit (msg.wParam);
	}
}


/*
==================
WinMain
==================
*/
int			global_nCmdShow;
char		*argv[MAX_NUM_ARGVS];
static char	*empty_string = "";

bool CheckKnownContent (char *mask);

bool ValidateQuakeDirectory (char *quakedir)
{
	// check for known files that indicate a gamedir
	if (CheckKnownContent (va ("%s/ID1/pak0.pak", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/config.cfg", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/autoexec.cfg", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/progs.dat", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/gfx.wad", quakedir))) return true;

	// some gamedirs just have maps or models, or may have weirdly named paks
	if (CheckKnownContent (va ("%s/ID1/maps/*.bsp", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/progs/*.mdl", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.pak", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.pk3", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.zip", quakedir))) return true;

	// some gamedirs are just used for keeping stuff separate
	if (CheckKnownContent (va ("%s/ID1/*.sav", quakedir))) return true;
	if (CheckKnownContent (va ("%s/ID1/*.dem", quakedir))) return true;

	// not quake
	return false;
}


bool RecursiveCheckFolderForQuake (char *path)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	char qpath[MAX_PATH];

	_snprintf (qpath, MAX_PATH, "%s\\*.*", path);
	hFind = FindFirstFile (qpath, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE)
	{
		// found no files
		FindClose (hFind);
		return false;
	}

	do
	{
		// directories only
		if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

		// not interested in these types
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
		if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

		// don't do these as they can cause us to be recursing up and down the same folder tree multiple times!!!
		if (!strcmp (FindFileData.cFileName, ".")) continue;
		if (!strcmp (FindFileData.cFileName, "..")) continue;

		// build the full path (we can stomp the one we came in with as it's no longer needed)
		_snprintf (qpath, MAX_PATH, "%s\\%s", path, FindFileData.cFileName);

		// see if this directory is Quake
		if (ValidateQuakeDirectory (qpath))
		{
			// set to current directory
			SetCurrentDirectory (qpath);
			FindClose (hFind);
			return true;
		}

		// check subdirs
		if (RecursiveCheckFolderForQuake (qpath)) return true;
	} while (FindNextFile (hFind, &FindFileData));

	// done (not found)
	FindClose (hFind);
	return false;
}

char MyDesktopFolder[MAX_PATH] = {0};

char *WellKnownDirectories[] =
{
	"Quake",
	"Program Files\\Quake",
	"Program Files\\Games\\Quake",
	"Program Files (x86)\\Quake",
	"Program Files (x86)\\Games\\Quake",
	"Games\\Quake",
	MyDesktopFolder,
	NULL
};

bool CheckWellKnownDirectory (char *drive, char *wellknown)
{
	char path[MAX_PATH];

	_snprintf (path, MAX_PATH, "%s%s", drive, wellknown);

	if (ValidateQuakeDirectory (path))
	{
		SetCurrentDirectory (path);
		return true;
	}

	return false;
}


typedef struct drivespec_s
{
	char letter[4];
	bool valid;
} drivespec_t;

void SetQuakeDirectory (void)
{
	char currdir[MAX_PATH];

	// some people install quake to their desktops
	hr = SHGetFolderPath (NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, MyDesktopFolder);

	if (FAILED (hr))
		MyDesktopFolder[0] = 0;
	else strcat (MyDesktopFolder, "\\quake");

	// if the current directory is the Quake directory, we search no more
	GetCurrentDirectory (MAX_PATH, currdir);

	if (ValidateQuakeDirectory (currdir)) return;

	// set up all drives
	drivespec_t drives[26];

	// check each in turn and flag as valid or not
	for (char c = 'A'; c <= 'Z'; c++)
	{
		// initially invalid
		int dnum = c - 'A';
		sprintf (drives[dnum].letter, "%c:\\", c);
		drives[dnum].valid = false;

		// start at c
		if (c < 'C') continue;

		UINT DriveType = GetDriveType (drives[dnum].letter);

		// don't check these types
		if (DriveType == DRIVE_NO_ROOT_DIR) continue;
		if (DriveType == DRIVE_UNKNOWN) continue;
		if (DriveType == DRIVE_CDROM) continue;
		if (DriveType == DRIVE_RAMDISK) continue;

		// it's valid for checking now
		drives[dnum].valid = true;
	}

	// check all drives (we expect that it will be in C:\Quake 99% of the time)
	// the first pass just checks the well-known directories for speed
	// (e.g so that D:\Quake won't cause a full scan of C)
	for (int d = 0; d < 26; d++)
	{
		if (!drives[d].valid) continue;

		// check some well-known directories (where we might reasonably expect to find Quake)
		for (int i = 0;; i++)
		{
			if (!WellKnownDirectories[i]) break;
			if (CheckWellKnownDirectory (drives[d].letter, WellKnownDirectories[i])) return;
		}
	}

	// if we still haven't found it we need to do a full HD scan.
	// be nice and ask the user...
	int conf = MessageBox (NULL, "Quake not found.\nPerform full disk scan?", "Alert", MB_YESNO | MB_ICONWARNING);

	if (conf == IDYES)
	{
		// second pass does a full scan of each drive
		for (int d = 0; d < 26; d++)
		{
			// not a validated drive
			if (!drives[d].valid) continue;

			// check everything
			if (RecursiveCheckFolderForQuake (drives[d].letter)) return;
		}
	}

	// oh shit
	MessageBox (NULL, "Could not locate Quake on your PC.\n\nPerhaps you need to move DirectQ.exe into C:\\Quake?", "Error", MB_OK | MB_ICONERROR);
	Sys_Quit (666);
}


STICKYKEYS StartupStickyKeys = {sizeof (STICKYKEYS), 0};
TOGGLEKEYS StartupToggleKeys = {sizeof (TOGGLEKEYS), 0};
FILTERKEYS StartupFilterKeys = {sizeof (FILTERKEYS), 0};


void AllowAccessibilityShortcutKeys (bool bAllowKeys)
{
	if (bAllowKeys)
	{
		// Restore StickyKeys/etc to original state
		// (note that this function is called "allow", not "enable"; if they were previously
		// disabled it will put them back that way too, it doesn't force them to be enabled.)
		SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
		SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
		SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);
	}
	else
	{
		// Disable StickyKeys/etc shortcuts but if the accessibility feature is on,
		// then leave the settings alone as its probably being usefully used
		STICKYKEYS skOff = StartupStickyKeys;

		if ((skOff.dwFlags & SKF_STICKYKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			skOff.dwFlags &= ~SKF_HOTKEYACTIVE;
			skOff.dwFlags &= ~SKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETSTICKYKEYS, sizeof (STICKYKEYS), &skOff, 0);
		}

		TOGGLEKEYS tkOff = StartupToggleKeys;

		if ((tkOff.dwFlags & TKF_TOGGLEKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			tkOff.dwFlags &= ~TKF_HOTKEYACTIVE;
			tkOff.dwFlags &= ~TKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETTOGGLEKEYS, sizeof (TOGGLEKEYS), &tkOff, 0);
		}

		FILTERKEYS fkOff = StartupFilterKeys;

		if ((fkOff.dwFlags & FKF_FILTERKEYSON) == 0)
		{
			// Disable the hotkey and the confirmation
			fkOff.dwFlags &= ~FKF_HOTKEYACTIVE;
			fkOff.dwFlags &= ~FKF_CONFIRMHOTKEY;

			SystemParametersInfo (SPI_SETFILTERKEYS, sizeof (FILTERKEYS), &fkOff, 0);
		}
	}
}


int MapKey (int key);
void ClearAllStates (void);
LONG CDAudio_MessageHandler (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void VID_SetOSGamma (void);
void VID_SetAppGamma (void);
void CDAudio_Pause (void);
void CDAudio_Resume (void);

void AppActivate (BOOL fActive, BOOL minimize)
{
	static bool vid_wassuspended = false;
	extern bool vid_canalttab;

	ActiveApp = fActive;
	Minimized = minimize;

	if (fActive)
	{
		bWindowActive = true;
		block_drawing = false;

		// do this first as the api calls might affect the other stuff
		if (D3DVid_IsFullscreen ())
		{
			if (vid_canalttab && vid_wassuspended)
			{
				vid_wassuspended = false;

				// ensure that the window is shown at at the top of the z order
				ShowWindow (d3d_Window, SW_SHOWNORMAL);
				SetForegroundWindow (d3d_Window);
			}
		}

		ClearAllStates ();
		IN_SetMouseState (D3DVid_IsFullscreen ());

		// restore everything else
		VID_SetAppGamma ();
		CDAudio_Resume ();
		AllowAccessibilityShortcutKeys (false);

		// needed to reestablish the correct viewports
		vid.recalc_refdef = 1;
	}
	else
	{
		bWindowActive = false;
		ClearAllStates ();
		IN_SetMouseState (D3DVid_IsFullscreen ());
		VID_SetOSGamma ();
		CDAudio_Pause ();
		S_ClearBuffer ();
		block_drawing = true;
		AllowAccessibilityShortcutKeys (true);

		if (D3DVid_IsFullscreen ())
		{
			if (vid_canalttab)
			{
				vid_wassuspended = true;
			}
		}
	}
}


cvar_t sys_sleeptime ("sys_sleeptime", 0.0f, CVAR_ARCHIVE);
double last_inputtime = 0;
bool IN_ReadInputMessages (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

/* main window procedure */
LRESULT CALLBACK MainWndProc (HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int fActive, fMinimized;

	// check for input messages
	if (IN_ReadInputMessages (hWnd, Msg, wParam, lParam)) return 0;

	switch (Msg)
	{
		// events we want to discard
	case WM_CREATE: return 0;
	case WM_ERASEBKGND: return 1; // treachery!!! see your MSDN!
	case WM_SYSCHAR: return 0;

	case WM_SYSCOMMAND:
		switch (wParam & ~0x0F)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			// prevent from happening
			return 0;

		default:
			return DefWindowProc (hWnd, Msg, wParam, lParam);
		}

	case WM_MOVE:
		// update cursor clip region
		IN_UpdateClipCursor ();
		return 0;

	case WM_CLOSE:
		if (MessageBox (d3d_Window, "Are you sure you want to quit?", "Confirm Exit", MB_YESNO | MB_SETFOREGROUND | MB_ICONQUESTION) == IDYES)
			Sys_Quit (0);

		return 0;

	case WM_ACTIVATE:
		fActive = LOWORD (wParam);
		fMinimized = (BOOL) HIWORD (wParam);
		AppActivate (!(fActive == WA_INACTIVE), fMinimized);

		// fix the leftover Alt from any Alt-Tab or the like that switched us away
		ClearAllStates ();
		IN_UpdateClipCursor ();

		return 0;

	case WM_DESTROY:
		PostQuitMessage (0);
		return 0;

	case MM_MCINOTIFY:
		return CDAudio_MessageHandler (hWnd, Msg, wParam, lParam);

	default:
		break;
	}

	// pass all unhandled messages to DefWindowProc
	return DefWindowProc (hWnd, Msg, wParam, lParam);
}


void Host_Frame (DWORD time);
void VID_DefaultMonitorGamma_f (void);
void GetCrashReason (LPEXCEPTION_POINTERS ep);

// fixme - run shutdown through here (or else consolidate the restoration stuff in a separate function)
LONG WINAPI TildeDirectQ (LPEXCEPTION_POINTERS toast)
{
	// restore monitor gamma
	VID_DefaultMonitorGamma_f ();

	MessageBox (NULL, "An unhandled exception occurred.\n",
				"An error has occurred",
				MB_OK | MB_ICONSTOP);

	// down she goes
	return EXCEPTION_EXECUTE_HANDLER;
}


int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	InitCommonControls ();

	// init memory pools
	// these need to be up as early as possible as other things in the startup use them
	Heap_Init ();

	// this is useless for error diagnosis but at least restores gamma on a crash
	SetUnhandledExceptionFilter (TildeDirectQ);

	if ((hwndSplash = CreateDialog (hInstance, MAKEINTRESOURCE (IDD_DIALOG1), NULL, NULL)) != NULL)
	{
		// we can only specify the dimensions in dialog units in the .rc file which are dependent on font-sizes in the system,
		// and which are therefore totally crap and inappropriate for a splash screen, so let's set the real size manually.
		RECT DesktopRect;
		SystemParametersInfo (SPI_GETWORKAREA, 0, &DesktopRect, 0);

		// if the bitmap size changes these will also need to change
		// (init to 0 so we can check if we got the bitmap OK)
		int SPLASHW = 0;
		int SPLASHH = 0;

		// so load the bitmap to check them
		HBITMAP hBitmap = LoadBitmap (hInstance, MAKEINTRESOURCE (IDB_DQLOGO256));

		if (hBitmap)
		{
			BITMAP bm;

			if (GetObject (hBitmap, sizeof (BITMAP), &bm))
			{
				SPLASHW = bm.bmWidth;
				SPLASHH = bm.bmHeight;
			}

			DeleteObject (hBitmap);
		}

		// only show the splash if we got the bitmap
		if (SPLASHW && SPLASHH)
		{
			// center it in the work area (don't assume that top and left are both 0)
			int x = DesktopRect.left + (((DesktopRect.right - DesktopRect.left) - SPLASHW) >> 1);
			int y = DesktopRect.top + (((DesktopRect.bottom - DesktopRect.top) - SPLASHH) >> 1);

			SetWindowPos (hwndSplash, NULL, x, y, SPLASHW, SPLASHH, SWP_NOZORDER);

			ShowWindow (hwndSplash, SW_SHOWDEFAULT);
			UpdateWindow (hwndSplash);
			SetForegroundWindow (hwndSplash);

			// make sure that we pump the message loop so that the dialog will show before we go to sleep
			Sys_SendKeyEvents ();

			// and sleep for a short while so that the player can see the dialog
			Sleep (750);
		}
	}

	// in case we ever need it for anything...
	GetSystemInfo (&SysInfo);

	// set up and register the window class (d3d doesn't need CS_OWNDC)
	// do this before anything else so that we'll have it available for the splash too
	WNDCLASS wc;

	// here we use DefWindowProc because we're going to change it a few times and we don't want spurious messages
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_CLASSDC;
	wc.lpfnWndProc = (WNDPROC) DefWindowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = 0;
	wc.hCursor = LoadCursor (NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = 0;
	wc.lpszClassName = D3D_WINDOW_CLASS_NAME;

	if (!RegisterClass (&wc))
	{
		Sys_Error ("D3D_CreateWindowClass: Failed to register Window Class");
		return 666;
	}

	// create our default window that we're going to use with everything
	d3d_Window = CreateWindowEx
	(
		0,
		D3D_WINDOW_CLASS_NAME,
		va ("DirectQ Release %s", DIRECTQ_VERSION),
		0,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	if (!d3d_Window)
	{
		Sys_Error ("Couldn't create window");
		return 666;
	}

	OSVERSIONINFO vinfo;

	vinfo.dwOSVersionInfoSize = sizeof (vinfo);

	if (!GetVersionEx (&vinfo))
	{
		// if we couldn't get it we pop the warning but still let it run
		vinfo.dwMajorVersion = 0;
		vinfo.dwPlatformId = 0;
	}

	// we officially support v5 and above of Windows
	if (vinfo.dwMajorVersion < 5)
	{
		int mret = MessageBox
		(
			NULL,
			"!!! UNSUPPORTED !!!\n\nThis software may run on your Operating System\nbut is NOT officially supported.\n\nCertain pre-requisites are needed.\nNow might be a good time to read the readme.\n\nClick OK if you are sure you want to continue...",
			"Warning",
			MB_OKCANCEL | MB_ICONWARNING
		);

		if (mret == IDCANCEL) return 666;
	}

	if (vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
		WinNT = true;
	else WinNT = false;

	quakeparms_t parms;
	char cwd[MAX_PATH];

	// attempt to get the path that the executable is in
	// if it doesn't work we just don't do it
	if (GetModuleFileName (NULL, cwd, 1023))
	{
		for (int i = strlen (cwd); i; i--)
		{
			if (cwd[i] == '/' || cwd[i] == '\\')
			{
				cwd[i] = 0;
				break;
			}
		}

		// attempt to set that path as the current working directory
		SetCurrentDirectory (cwd);
	}

	// Declare this process to be high DPI aware, and prevent automatic scaling
	HINSTANCE hUser32 = LoadLibrary ("user32.dll");

	if (hUser32)
	{
		typedef BOOL (WINAPI * LPSetProcessDPIAware) (void);
		LPSetProcessDPIAware pSetProcessDPIAware = (LPSetProcessDPIAware) GetProcAddress (hUser32, "SetProcessDPIAware");

		if (pSetProcessDPIAware) pSetProcessDPIAware ();

		UNLOAD_LIBRARY (hUser32);
	}

	global_nCmdShow = nCmdShow;

	// set the directory containing Quake
	SetQuakeDirectory ();

	if (!GetCurrentDirectory (sizeof (cwd), cwd))
		Sys_Error ("Couldn't determine current directory");

	// remove any trailing slash
	if (cwd[strlen (cwd) - 1] == '/' || cwd[strlen (cwd) - 1] == '\\')
		cwd[strlen (cwd) - 1] = 0;

	parms.basedir = cwd;
	parms.cachedir = NULL;

	parms.argc = 1;
	argv[0] = empty_string;

	// parse the command-line into args
	while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
	{
		while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
			lpCmdLine++;

		if (*lpCmdLine)
		{
			argv[parms.argc] = lpCmdLine;
			parms.argc++;

			while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				*lpCmdLine = 0;
				lpCmdLine++;
			}
		}
	}

	parms.argv = argv;

	COM_InitArgv (parms.argc, parms.argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	// Save the current sticky/toggle/filter key settings so they can be restored later
	SystemParametersInfo (SPI_GETSTICKYKEYS, sizeof (STICKYKEYS), &StartupStickyKeys, 0);
	SystemParametersInfo (SPI_GETTOGGLEKEYS, sizeof (TOGGLEKEYS), &StartupToggleKeys, 0);
	SystemParametersInfo (SPI_GETFILTERKEYS, sizeof (FILTERKEYS), &StartupFilterKeys, 0);

	// Disable when full screen
	AllowAccessibilityShortcutKeys (false);

	// force an initial refdef calculation
	vid.recalc_refdef = 1;

	Sys_Init ();
	Host_Init (&parms);

	// init the timers
	Sys_InitTimers ();
	Sys_ChangeTimer (&sys_usetimegettime);

	DWORD oldtime = Sys_Milliseconds ();
	MSG msg;

	while (1)
	{
		// note - a normal frame needs to be run even if paused otherwise we'll never be able to unpause!!!
		if (cl.paused)
			Sleep (PAUSE_SLEEP);
		else if (!ActiveApp || Minimized || block_drawing)
			Sleep (NOT_FOCUS_SLEEP);

		if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE) != 0)
		{
			if (msg.message == WM_QUIT)
				break;
			else
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}

		if (sys_sleeptime.value > 0 && !cls.demoplayback)
		{
			// start sleeping if we go idle (unless we're in a demo in which case we take the full CPU)
			if (realtime > last_inputtime + (sys_sleeptime.value * 8))
				Sleep (100);
			else if (realtime > last_inputtime + (sys_sleeptime.value * 4))
				Sleep (5);
			else if (realtime > last_inputtime + (sys_sleeptime.value * 2))
				Sleep (1);
			else if (realtime > last_inputtime + sys_sleeptime.value)
				Sleep (0);
		}

		// RMQ engine timer; more limited resolution than QPC but virtually immune to resolution problems at very high FPS
		// and gives ultra-smooth performance
		DWORD newtime = Sys_Milliseconds ();

		Host_Frame (newtime - oldtime);
		oldtime = newtime;
	}

	// run through correct shutdown
	timeEndPeriod (1);
	Sys_Quit ((int) msg.wParam);
	return (int) msg.wParam;
}

