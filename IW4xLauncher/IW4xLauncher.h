#pragma once

#ifndef __AFXWIN_H__
#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"

// ---------------------------------------------------------------------------
// CIW4xLauncherApp
//   Standard MFC application object. Starts up GDI+ (needed by MainDlg's
//   custom drawing) alongside the usual MFC init.
// ---------------------------------------------------------------------------
class CIW4xLauncherApp : public CWinApp
{
public:
    CIW4xLauncherApp();

    virtual BOOL InitInstance();
    virtual int ExitInstance();

    DECLARE_MESSAGE_MAP()

private:
    ULONG_PTR m_gdiplusToken = 0;
};

extern CIW4xLauncherApp theApp;
