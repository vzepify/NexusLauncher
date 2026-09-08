#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#define NOMINMAX

#include "targetver.h"

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

#define _AFX_ALL_WARNINGS

#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>

#ifndef _AFX_NO_OLE_SUPPORT
#include <afxdtctl.h>
#endif
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>
#endif

#include <afxdialogex.h>

// GDI+ for the flat/rounded modern UI drawing
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// Folder picker for the Settings "Browse..." button, and ShellExecute for
// launching the game from the Home view.
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
