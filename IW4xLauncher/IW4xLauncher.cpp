#include "pch.h"
#include "IW4xLauncher.h"
#include "MainDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CIW4xLauncherApp, CWinApp)
END_MESSAGE_MAP()

CIW4xLauncherApp::CIW4xLauncherApp()
{
}

CIW4xLauncherApp theApp;

BOOL CIW4xLauncherApp::InitInstance()
{
    // InitCommonControlsEx() is required on Windows XP if an application
    // manifest specifies use of ComCtl32.dll version 6 or later.
    INITCOMMONCONTROLSEX InitCtrls{};
    InitCtrls.dwSize = sizeof(InitCtrls);
    InitCtrls.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&InitCtrls);

    CWinApp::InitInstance();

    // Start GDI+ - all of MainDlg's custom flat/rounded drawing depends on it.
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);

    SetRegistryKey(_T("IW4xLauncher"));

    CMainDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    // Since the dialog has been closed, return FALSE so that we exit the
    // application, rather than start the application's message pump.
    return FALSE;
}

int CIW4xLauncherApp::ExitInstance()
{
    if (m_gdiplusToken)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    return CWinApp::ExitInstance();
}
