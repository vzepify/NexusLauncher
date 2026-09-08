#pragma once
#include "afxdialogex.h"
#include "resource.h"
#include <gdiplus.h>
#include <objidl.h>
#include <string>
#include <cstdint>
#include "DiscordPresence.h"
#include "UpdateManager.h"

// ---------------------------------------------------------------------------
// CMainDlg
//
// The entire launcher window. It is a single borderless MFC dialog with no
// child controls at all - the title bar, sidebar, cards, buttons, text
// fields and radio dots you see are all painted by hand with GDI+ in
// OnPaint(), and every click is resolved with plain hit-testing against the
// rectangles computed in RecalcLayout().
//
// Three games are supported, matching the reference launcher's sidebar
// order (top-to-bottom, below the hub icon) and Settings row order:
// Advanced Warfare (S1x), Ghosts (IW6x), Modern Warfare 2 (IW4x).
//
// Two views are supported (m_view):
//   Home     - the "Multiplayer" card for whichever game is selected, with
//              that game's background image and a Play button
//   Settings - three install-path fields (one per game) + Stable/
//              Experimental toggle
// ---------------------------------------------------------------------------
class CMainDlg : public CDialogEx
{
public:
    CMainDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_IW4XLAUNCHER_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave();
    afx_msg LRESULT OnNcHitTest(CPoint point);
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg LRESULT OnUpdateFinished(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    enum class View { Home, Settings };

    // Index into every per-game array below. Order matches the sidebar
    // (top-to-bottom) and the Settings rows, mirroring the reference app.
    enum class Game { S1X = 0, IW6X = 1, IW4X = 2 };
    static constexpr int kGameCount = 3;

    // ---- layout ----
    void RecalcLayout();

    // ---- drawing ----
    void Draw(Gdiplus::Graphics& g);
    void DrawTitleBar(Gdiplus::Graphics& g);
    void DrawSidebar(Gdiplus::Graphics& g);
    void DrawHubIcon(Gdiplus::Graphics& g, const Gdiplus::Rect& r, bool active);
    void DrawGameIcon(Gdiplus::Graphics& g, const Gdiplus::Rect& r, bool active, Gdiplus::Image* img, const wchar_t* fallbackLabel);
    void DrawGearIcon(Gdiplus::Graphics& g, const Gdiplus::Rect& r, bool active);
    void DrawCard(Gdiplus::Graphics& g);
    void DrawHomeView(Gdiplus::Graphics& g, const Gdiplus::Rect& content);
    void DrawSettingsView(Gdiplus::Graphics& g, const Gdiplus::Rect& content);

    static void FillRoundedRect(Gdiplus::Graphics& g, const Gdiplus::Rect& r, int radius, const Gdiplus::Brush& brush);
    static void FillRoundedRectTop(Gdiplus::Graphics& g, const Gdiplus::Rect& r, int radius, const Gdiplus::Brush& brush);
    static void MakeRoundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::Rect& r, int radius, bool topOnly = false);
    static void DrawImageCover(Gdiplus::Graphics& g, Gdiplus::Image* img, const Gdiplus::Rect& dest, int radius);
    static CString ResourcePath(const wchar_t* filename);
    static bool FileExists(const CString& path);
    Gdiplus::Image* LoadOptionalImage(const wchar_t* filename, CString& diagOut);
    Gdiplus::Image* LoadEmbeddedImage(const wchar_t* filename, CString& diagOut, IStream** streamOut);

    // ---- actions ----
    void SetView(View v);
    void SelectGame(Game g);
    void BrowseForInstall(Game g);
    void CheckForUpdates(Game g);
    void TryLaunchGame();
    void LoadSettings();
    void SaveSettings();
    void UpdateDiscordPresence();

private:
    View m_view = View::Home;
    Game m_selectedGame = Game::IW4X;

    // GDI+ fonts (created in OnInitDialog, freed in OnDestroy)
    Gdiplus::Font* m_fontTitle = nullptr;
    Gdiplus::Font* m_fontHeader = nullptr;
    Gdiplus::Font* m_fontLabel = nullptr;
    Gdiplus::Font* m_fontButton = nullptr;
    Gdiplus::Font* m_fontSmall = nullptr;
    Gdiplus::FontFamily* m_fontFamily = nullptr;

    // Launcher artwork compiled into the executable as Windows resources.
    // The source images remain under res\ for resource compilation, but no
    // res\ folder is required beside the built EXE.
    Gdiplus::Image* m_imgBackground[kGameCount] = {};   // embedded background resource
    Gdiplus::Image* m_imgGameIcon[kGameCount] = {};     // embedded game icon resource
    Gdiplus::Image* m_imgHubIcon = nullptr;             // embedded hub resource
    Gdiplus::Image* m_imgSettingsIcon = nullptr;        // embedded settings resource
    CString m_strBgDiag[kGameCount];
    CString m_strIconDiag[kGameCount];
    CString m_strHubDiag;
    CString m_strSettingsDiag;
    IStream* m_imgStreams[kGameCount * 2 + 2] = {}; // keep resource-backed image streams alive

    CString m_strInstallPath[kGameCount];
    int     m_updateChannel = 1; // 0 = Stable, 1 = Experimental

    // cached layout rectangles, recomputed in RecalcLayout()
    CRect m_rcTitleBar;
    CRect m_rcDotMin;
    CRect m_rcDotClose;
    CRect m_rcSidebar;
    CRect m_rcIconHub;
    CRect m_rcIconGame[kGameCount];
    CRect m_rcIconGear;
    CRect m_rcCard;
    CRect m_rcCardHeader;
    CRect m_rcContent;

    // settings-view sub rects
    CRect m_rcField[kGameCount];
    CRect m_rcBrowseBtn[kGameCount];
    CRect m_rcUpdateBtn[kGameCount];
    CRect m_rcRadioStable;
    CRect m_rcRadioExperimental;

    // home-view sub rects
    CRect m_rcPlayBtn;

    bool m_hoverMin = false;
    bool m_hoverClose = false;
    bool m_hoverBrowse[kGameCount] = {};
    bool m_hoverUpdate[kGameCount] = {};
    bool m_hoverPlay = false;
    bool m_trackingMouse = false;

    // ---- Discord Rich Presence ----
    CDiscordPresence m_discord;
    std::wstring m_discordClientId;
    bool m_discordPresenceActive = false;
    DWORD m_discordLastGamePid = 0;
    Game m_discordLastGame = Game::IW4X;
    CString m_discordLastServer;
    std::int64_t m_discordStartUnix = 0;
    std::int64_t m_discordLauncherStartUnix = 0;

    // ---- GitHub updater ----
    bool m_updateBusy = false;
    Game m_updateGame = Game::IW4X;
};
