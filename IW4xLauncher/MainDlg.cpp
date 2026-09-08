#include "pch.h"
#include "IW4xLauncher.h"
#include "MainDlg.h"
#include "UITheme.h"
#include <shlobj.h>
#include <cmath>
#include <tlhelp32.h>
#include <fstream>
#include <string>
#include <vector>
#include <cwctype>
#include <ctime>
#include <algorithm>
#include <thread>
#include <objidl.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace Gdiplus;
using namespace UITheme;

namespace
{
    // Registry value names (stored via CWinApp::Get/WriteProfileString under
    // the "IW4xLauncher" key set with SetRegistryKey in IW4xLauncher.cpp).
    constexpr wchar_t kSection[] = L"Settings";
    constexpr wchar_t kKeyChannel[] = L"UpdateChannel";

    // Everything below is indexed by CMainDlg::Game (S1X=0, IW6X=1, IW4X=2).
    // The order matches the sidebar (top-to-bottom, below the hub icon) and
    // the Settings rows, mirroring the reference launcher.
    const wchar_t* kGameIconFile[3]      = { L"s1x.png", L"iw6x.png", L"iw4x.png" };
    const wchar_t* kGameBgFile[3]        = { L"background_s1x.jpg", L"background_iw6x.jpg", L"background_iw4x.jpg" };
    const wchar_t* kGameShortLabel[3]    = { L"S1X", L"IW6X", L"IW4X" };
    const wchar_t* kGameSettingsLabel[3] = { L"Advanced Warfare Installation", L"Ghosts Installation", L"Modern Warfare 2 Installation" };
    const wchar_t* kGameExeName[3]       = { L"s1x.exe", L"iw6x.exe", L"iw4x.exe" };
    const wchar_t* kGameInstallKey[3]    = { L"InstallPath_S1X", L"InstallPath_IW6X", L"InstallPath_IW4X" };
    const wchar_t* kGameFolderPrompt[3] =
    {
        L"Select your Advanced Warfare (S1x) installation folder",
        L"Select your Ghosts (IW6x) installation folder",
        L"Select your Modern Warfare 2 (IW4x) installation folder"
    };

    struct RunningGame
    {
        int index = -1;
        DWORD pid = 0;
        CString windowTitle;
        CString installPath;
    };

    struct WindowSearchContext
    {
        DWORD pid = 0;
        CString title;
    };

    BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam)
    {
        auto* ctx = reinterpret_cast<WindowSearchContext*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ctx->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
            return TRUE;

        wchar_t title[512] = {};
        GetWindowTextW(hwnd, title, _countof(title));
        if (title[0] != L'\0')
        {
            ctx->title = title;
            return FALSE;
        }
        return TRUE;
    }

    CString GetMainWindowTitleForPid(DWORD pid)
    {
        WindowSearchContext ctx{};
        ctx.pid = pid;
        EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
        return ctx.title;
    }

    CString NormalizeGameTitle(const CString& title, int gameIndex)
    {
        CString t(title);
        t.Trim();
        if (t.IsEmpty())
            return {};

        static const wchar_t* const labels[3] = { L"S1X", L"IW6X", L"IW4X" };
        static const wchar_t* const longLabels[3] =
        {
            L"Call of Duty: Advanced Warfare",
            L"Call of Duty: Ghosts",
            L"Call of Duty: Modern Warfare 2"
        };

        CString lower = t;
        lower.MakeLower();
        CString label(labels[gameIndex]);
        CString longLabel(longLabels[gameIndex]);
        CString ll = label; ll.MakeLower();
        CString lll = longLabel; lll.MakeLower();

        auto stripAround = [&](const wchar_t* separator) -> CString
        {
            int pos = t.Find(separator);
            if (pos < 0)
                return {};
            CString left = t.Left(pos);
            CString right = t.Mid(pos + (int)wcslen(separator));
            left.Trim(); right.Trim();
            CString leftLower(left); leftLower.MakeLower();
            CString rightLower(right); rightLower.MakeLower();
            if (leftLower == ll || leftLower == lll) return right;
            if (rightLower == ll || rightLower == lll) return left;
            return {};
        };

        // Common game-title forms: "IW4x - Server Name", "Server Name - IW4x"
        // and "IW4x: Server Name". If the title is only the game name, there
        // is no server name to publish.
        if (lower == ll || lower == lll)
            return {};

        for (const wchar_t* sep : { L" - ", L": ", L" | " })
        {
            CString candidate = stripAround(sep);
            if (!candidate.IsEmpty())
            {
                candidate.Trim(L" -|:");
                if (!candidate.IsEmpty())
                    return candidate;
            }
        }

        return {};
    }

    CString ReadHostnameFromLog(const CString& installPath)
    {
        static const wchar_t* const candidates[] =
        {
            L"main\\games_mp.log",
            L"main\\games_mp.log.bak",
            L"userraw\\logs\\games_mp.log",
            L"userraw\\logs\\server\\games_mp.log",
            L"userraw\\logs\\games.log"
        };

        CString found;
        for (const auto* rel : candidates)
        {
            CString path = installPath + L"\\" + rel;
            std::ifstream file;
            const int pathBytes = WideCharToMultiByte(CP_UTF8, 0, path.GetString(), -1, nullptr, 0, nullptr, nullptr);
            if (pathBytes <= 0)
                continue;
            std::vector<char> pathBuffer((size_t)pathBytes);
            WideCharToMultiByte(CP_UTF8, 0, path.GetString(), -1, pathBuffer.data(), pathBytes, nullptr, nullptr);
            file.open(pathBuffer.data(), std::ios::binary);
            if (!file)
                continue;

            file.seekg(0, std::ios::end);
            std::streamoff len = file.tellg();
            if (len <= 0)
                continue;
            const std::streamoff readSize = std::min<std::streamoff>(len, 96 * 1024);
            file.seekg(-readSize, std::ios::end);
            std::string data((size_t)readSize, '\0');
            file.read(data.data(), readSize);
            if (data.empty())
                continue;

            auto searchLastQuoted = [&](const std::string& needle)
            {
                size_t pos = 0;
                while ((pos = data.find(needle, pos)) != std::string::npos)
                {
                    size_t q1 = data.find('"', pos + needle.size());
                    if (q1 == std::string::npos) break;
                    size_t q2 = data.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    std::string value = data.substr(q1 + 1, q2 - q1 - 1);
                    if (!value.empty())
                    {
                        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
                        UINT cp = CP_UTF8;
                        if (wlen <= 0)
                        {
                            cp = CP_ACP;
                            wlen = MultiByteToWideChar(cp, 0, value.data(), (int)value.size(), nullptr, 0);
                        }
                        if (wlen > 0)
                        {
                            std::wstring wide((size_t)wlen, L'\0');
                            MultiByteToWideChar(cp, 0, value.data(), (int)value.size(), wide.data(), wlen);
                            found = wide.c_str();
                        }
                    }
                    pos = q2 + 1;
                }
            };

            searchLastQuoted("sv_hostname");
            searchLastQuoted("server hostname");
            searchLastQuoted("server name");
            if (!found.IsEmpty())
                return found;
        }
        return {};
    }

    RunningGame FindRunningGame(const CString* installPaths)
    {
        static const wchar_t* const exeNames[3] = { L"s1x.exe", L"iw6x.exe", L"iw4x.exe" };
        RunningGame best{};

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return best;

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                int gameIndex = -1;
                for (int i = 0; i < 3; ++i)
                {
                    if (_wcsicmp(entry.szExeFile, exeNames[i]) == 0)
                    {
                        gameIndex = i;
                        break;
                    }
                }
                if (gameIndex < 0)
                    continue;

                CString title = GetMainWindowTitleForPid(entry.th32ProcessID);
                if (title.IsEmpty())
                    continue;

                best.index = gameIndex;
                best.pid = entry.th32ProcessID;
                best.windowTitle = title;
                best.installPath = installPaths[gameIndex];
                CloseHandle(snapshot);
                return best;
            }
            while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return best;
    }

    std::wstring LoadDiscordClientId()
    {
        // Compiled into the executable so no txt file is required.
        // Replace the placeholder with your Discord Application Client ID.
        constexpr wchar_t kDiscordClientId[] = L"1546908541565280349";
        return std::wstring(kDiscordClientId);
    }

    inline Gdiplus::Rect ToGpRect(const CRect& r)
    {
        return Gdiplus::Rect(r.left, r.top, r.Width(), r.Height());
    }
}

BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_NCHITTEST()
    ON_WM_QUERYDRAGICON()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_MESSAGE(WM_APP + 0x421, &CMainDlg::OnUpdateFinished)
END_MESSAGE_MAP()

CMainDlg::CMainDlg(CWnd* pParent /*= nullptr*/)
    : CDialogEx(IDD_IW4XLAUNCHER_DIALOG, pParent)
{
}

void CMainDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BOOL CMainDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon((HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR), TRUE);
    SetIcon((HICON)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR), FALSE);

    // Force the exact pixel size the layout constants were designed around,
    // then center on the work area (dialog-unit sizing would drift with DPI).
    CRect rcWork;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
    int x = rcWork.left + (rcWork.Width() - WIN_W) / 2;
    int y = rcWork.top + (rcWork.Height() - WIN_H) / 2;
    SetWindowPos(nullptr, x, y, WIN_W, WIN_H, SWP_NOZORDER);

    // Give the borderless popup an actual rounded window shape (not just a
    // rounded rect drawn inside a square window) so it matches the
    // reference app's soft corners even where the desktop shows through.
    CRgn rgn;
    rgn.CreateRoundRectRgn(0, 0, WIN_W + 1, WIN_H + 1, WINDOW_CORNER_RADIUS * 2, WINDOW_CORNER_RADIUS * 2);
    SetWindowRgn((HRGN)rgn.Detach(), TRUE);

    m_fontFamily = new FontFamily(L"Segoe UI");
    if (m_fontFamily->GetLastStatus() != Ok)
    {
        delete m_fontFamily;
        m_fontFamily = new FontFamily(L"Arial");
    }
    m_fontTitle  = new Font(m_fontFamily, 14, FontStyleRegular, UnitPixel);
    m_fontHeader = new Font(m_fontFamily, 17, FontStyleRegular, UnitPixel);
    m_fontLabel  = new Font(m_fontFamily, 14, FontStyleRegular, UnitPixel);
    m_fontButton = new Font(m_fontFamily, 13, FontStyleRegular, UnitPixel);
    m_fontSmall  = new Font(m_fontFamily, 12, FontStyleRegular, UnitPixel);

    // All launcher artwork is embedded into the EXE as Windows resources.
    // No res\ folder is needed at runtime.
    m_imgBackground[0] = LoadEmbeddedImage(L"background_s1x.jpg", m_strBgDiag[0], &m_imgStreams[0]);
    m_imgBackground[1] = LoadEmbeddedImage(L"background_iw6x.jpg", m_strBgDiag[1], &m_imgStreams[1]);
    m_imgBackground[2] = LoadEmbeddedImage(L"background_iw4x.jpg", m_strBgDiag[2], &m_imgStreams[2]);
    m_imgGameIcon[0]   = LoadEmbeddedImage(L"s1x.png", m_strIconDiag[0], &m_imgStreams[3]);
    m_imgGameIcon[1]   = LoadEmbeddedImage(L"iw6x.png", m_strIconDiag[1], &m_imgStreams[4]);
    m_imgGameIcon[2]   = LoadEmbeddedImage(L"iw4x.png", m_strIconDiag[2], &m_imgStreams[5]);
    m_imgHubIcon       = LoadEmbeddedImage(L"hub.png", m_strHubDiag, &m_imgStreams[6]);
    m_imgSettingsIcon  = LoadEmbeddedImage(L"settings.png", m_strSettingsDiag, &m_imgStreams[7]);

    LoadSettings();
    RecalcLayout();

    // Discord Rich Presence is optional. If discord_client_id.txt is blank
    // or missing, the launcher simply skips RPC without affecting gameplay.
    m_discordClientId = LoadDiscordClientId();
    SetTimer(0x4E58, 2500, nullptr);
    SetTimer(0x4E59, 250, nullptr);
    UpdateDiscordPresence();

    return TRUE;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void CMainDlg::RecalcLayout()
{
    CRect client;
    GetClientRect(&client);

    m_rcTitleBar = CRect(0, 0, client.Width(), TITLEBAR_H);

    m_rcDotClose = CRect(0, 0, DOT_DIAM, DOT_DIAM);
    m_rcDotClose.OffsetRect(client.Width() - DOT_MARGIN - DOT_DIAM, (TITLEBAR_H - DOT_DIAM) / 2);

    m_rcDotMin = CRect(0, 0, DOT_DIAM, DOT_DIAM);
    m_rcDotMin.OffsetRect(m_rcDotClose.left - DOT_GAP - DOT_DIAM, (TITLEBAR_H - DOT_DIAM) / 2);

    m_rcSidebar = CRect(0, TITLEBAR_H, SIDEBAR_W, client.Height());

    int iconX = (SIDEBAR_W - ICON_SIZE) / 2;
    m_rcIconHub = CRect(iconX, TITLEBAR_H + ICON_TOP, iconX + ICON_SIZE, TITLEBAR_H + ICON_TOP + ICON_SIZE);

    CRect prev = m_rcIconHub;
    for (int i = 0; i < kGameCount; ++i)
    {
        CRect r = prev;
        r.OffsetRect(0, ICON_SIZE + ICON_GAP);
        m_rcIconGame[i] = r;
        prev = r;
    }

    int gearX = (SIDEBAR_W - GEAR_SIZE) / 2;
    int gearY = client.Height() - GEAR_BOTTOM_MARGIN - GEAR_SIZE;
    m_rcIconGear = CRect(gearX, gearY, gearX + GEAR_SIZE, gearY + GEAR_SIZE);

    m_rcCard = CRect(SIDEBAR_W + CARD_GAP, TITLEBAR_H + CARD_MARGIN,
                      client.right - CARD_MARGIN, client.bottom - CARD_MARGIN);

    m_rcCardHeader = CRect(m_rcCard.left, m_rcCard.top, m_rcCard.right, m_rcCard.top + HEADER_H);

    m_rcContent = CRect(m_rcCard.left + CONTENT_PAD, m_rcCardHeader.bottom + CONTENT_PAD,
                         m_rcCard.right - CONTENT_PAD, m_rcCard.bottom - CONTENT_PAD);

    // ---- Settings rows: one install-path field per game ----
    int labelColW = 260;
    constexpr int kUpdateBtnW = 120;
    constexpr int kButtonGap = 10;
    int fieldLeft = m_rcContent.left + labelColW + 20;
    int fieldRight = m_rcContent.right - CONTENT_PAD - kUpdateBtnW - kButtonGap - BROWSE_BTN_W - 12;

    for (int i = 0; i < kGameCount; ++i)
    {
        int rowTop = m_rcContent.top + 8 + i * (ROW_GAP + FIELD_H);
        m_rcField[i] = CRect(fieldLeft, rowTop, fieldRight, rowTop + FIELD_H);
        m_rcBrowseBtn[i] = CRect(fieldRight + 12, rowTop, fieldRight + 12 + BROWSE_BTN_W, rowTop + FIELD_H);
        m_rcUpdateBtn[i] = CRect(m_rcBrowseBtn[i].right + kButtonGap, rowTop,
                                 m_rcBrowseBtn[i].right + kButtonGap + kUpdateBtnW, rowTop + FIELD_H);
    }

    int channelRowTop = m_rcContent.top + 8 + kGameCount * (ROW_GAP + FIELD_H);
    int radioY = channelRowTop + (FIELD_H - RADIO_DIAM) / 2;

    // Lay the "○ Stable    ● Experimental" group out with fixed label
    // widths, sized so the longer "Experimental" label's right edge lands
    // exactly on the content's right edge instead of running past it.
    const int kDotLabelGap = 8;
    const int kPairGap = 36;
    const int kStableLabelW = 60;
    const int kExperimentalLabelW = 110;
    const int kGroupWidth = RADIO_DIAM + kDotLabelGap + kStableLabelW + kPairGap
                           + RADIO_DIAM + kDotLabelGap + kExperimentalLabelW;

    int groupLeft = m_rcContent.right - kGroupWidth;

    m_rcRadioStable = CRect(groupLeft, radioY, groupLeft + RADIO_DIAM, radioY + RADIO_DIAM);

    int expDotX = groupLeft + RADIO_DIAM + kDotLabelGap + kStableLabelW + kPairGap;
    m_rcRadioExperimental = CRect(expDotX, radioY, expDotX + RADIO_DIAM, radioY + RADIO_DIAM);

    // Home view Play button (bottom-right under the image)
    int playW = 140, playH = 44;
    m_rcPlayBtn = CRect(m_rcContent.right - playW, m_rcContent.bottom - playH,
                         m_rcContent.right, m_rcContent.bottom);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CMainDlg::OnPaint()
{
    CPaintDC dc(this);

    CRect client;
    GetClientRect(&client);

    // Double-buffer everything through an off-screen GDI+ bitmap so the flat
    // fills and rounded corners never flicker while the window redraws.
    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&dc, client.Width(), client.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    {
        Graphics g(memDC.GetSafeHdc());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        Draw(g);
    }

    dc.BitBlt(0, 0, client.Width(), client.Height(), &memDC, 0, 0, SRCCOPY);

    memDC.SelectObject(pOldBmp);
}

BOOL CMainDlg::OnEraseBkgnd(CDC* /*pDC*/)
{
    // Everything is repainted in OnPaint via the memory DC - avoid the
    // default background flash.
    return TRUE;
}

void CMainDlg::Draw(Graphics& g)
{
    CRect client;
    GetClientRect(&client);

    SolidBrush bg(WindowBg());
    g.FillRectangle(&bg, ToGpRect(client));

    DrawTitleBar(g);
    DrawSidebar(g);
    DrawCard(g);
}

void CMainDlg::DrawTitleBar(Graphics& g)
{
    SolidBrush barBrush(TitleBarBg());
    g.FillRectangle(&barBrush, ToGpRect(m_rcTitleBar));

    const wchar_t* title = (m_updateChannel == 1) ? L"Experimental" : L"Stable";
    SolidBrush textBrush(TitleText());
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF rc(0.0f, 0.0f, (REAL)m_rcTitleBar.Width(), (REAL)m_rcTitleBar.Height());
    g.DrawString(title, -1, m_fontTitle, rc, &fmt, &textBrush);

    // Minimize dot
    SolidBrush minBrush(m_hoverMin ? DotMinimizeHi() : DotMinimize());
    g.FillEllipse(&minBrush, ToGpRect(m_rcDotMin));

    // Close dot
    SolidBrush closeBrush(m_hoverClose ? DotCloseHi() : DotClose());
    g.FillEllipse(&closeBrush, ToGpRect(m_rcDotClose));
}

void CMainDlg::DrawSidebar(Graphics& g)
{
    // The hub icon is branding-only (not clickable) - see OnLButtonDown.
    DrawHubIcon(g, ToGpRect(m_rcIconHub), false);

    // Divider between the hub icon and the game icons.
    {
        Pen dividerPen(SidebarDivider(), 1.0f);
        int y = (m_rcIconHub.bottom + m_rcIconGame[0].top) / 2;
        g.DrawLine(&dividerPen, (REAL)(m_rcSidebar.left + SIDEBAR_DIVIDER_MARGIN), (REAL)y,
                                 (REAL)(m_rcSidebar.right - SIDEBAR_DIVIDER_MARGIN), (REAL)y);
    }

    for (int i = 0; i < kGameCount; ++i)
    {
        bool active = (m_view == View::Home) && ((int)m_selectedGame == i);
        DrawGameIcon(g, ToGpRect(m_rcIconGame[i]), active, m_imgGameIcon[i], kGameShortLabel[i]);
    }

    // Divider between the game icons and the settings gear.
    {
        Pen dividerPen(SidebarDivider(), 1.0f);
        int y = (m_rcIconGame[kGameCount - 1].bottom + m_rcIconGear.top) / 2;
        g.DrawLine(&dividerPen, (REAL)(m_rcSidebar.left + SIDEBAR_DIVIDER_MARGIN), (REAL)y,
                                 (REAL)(m_rcSidebar.right - SIDEBAR_DIVIDER_MARGIN), (REAL)y);
    }

    DrawGearIcon(g, ToGpRect(m_rcIconGear), m_view == View::Settings);
}

void CMainDlg::MakeRoundedPath(GraphicsPath& path, const Gdiplus::Rect& r, int radius, bool topOnly)
{
    path.Reset();

    if (r.Width <= 0 || r.Height <= 0)
        return;

    int maxRadius = (r.Width < r.Height ? r.Width : r.Height) / 2;
    radius = std::max(0, std::min(radius, maxRadius));

    if (radius == 0)
    {
        path.AddRectangle(RectF((REAL)r.X, (REAL)r.Y, (REAL)r.Width, (REAL)r.Height));
        path.CloseFigure();
        return;
    }

    const REAL x = (REAL)r.X;
    const REAL y = (REAL)r.Y;
    const REAL w = (REAL)r.Width;
    const REAL h = (REAL)r.Height;
    const REAL d = (REAL)(radius * 2);
    const REAL right = x + w;
    const REAL bottom = y + h;

    // Clockwise outline starting at the top-left corner.
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddLine(x + radius, y, right - radius, y);
    path.AddArc(right - d, y, d, d, 270.0f, 90.0f);
    path.AddLine(right, y + radius, right, bottom - (topOnly ? 0.0f : radius));

    if (topOnly)
    {
        path.AddLine(right, bottom, x, bottom);
        path.AddLine(x, bottom, x, y + radius);
    }
    else
    {
        path.AddArc(right - d, bottom - d, d, d, 0.0f, 90.0f);
        path.AddLine(right - radius, bottom, x + radius, bottom);
        path.AddArc(x, bottom - d, d, d, 90.0f, 90.0f);
        path.AddLine(x, bottom - radius, x, y + radius);
    }

    path.CloseFigure();
}

void CMainDlg::FillRoundedRect(Graphics& g, const Gdiplus::Rect& r, int radius, const Brush& brush)
{
    GraphicsPath path;
    MakeRoundedPath(path, r, radius, false);
    g.FillPath(&brush, &path);
}

void CMainDlg::FillRoundedRectTop(Graphics& g, const Gdiplus::Rect& r, int radius, const Brush& brush)
{
    GraphicsPath path;
    MakeRoundedPath(path, r, radius, true);
    g.FillPath(&brush, &path);
}

void CMainDlg::DrawImageCover(Graphics& g, Image* img, const Gdiplus::Rect& dest, int radius)
{
    if (img == nullptr || dest.Width <= 0 || dest.Height <= 0)
        return;

    const UINT srcW = img->GetWidth();
    const UINT srcH = img->GetHeight();
    if (srcW == 0 || srcH == 0)
        return;

    // Cover the destination while preserving aspect ratio.
    const double scaleX = (double)dest.Width / (double)srcW;
    const double scaleY = (double)dest.Height / (double)srcH;
    const double scale = (scaleX > scaleY) ? scaleX : scaleY;

    const int drawW = (int)std::lround((double)srcW * scale);
    const int drawH = (int)std::lround((double)srcH * scale);
    const int drawX = dest.X + (dest.Width - drawW) / 2;
    const int drawY = dest.Y + (dest.Height - drawH) / 2;

    GraphicsState state = g.Save();
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    GraphicsPath clipPath;
    MakeRoundedPath(clipPath, dest, radius, false);
    g.SetClip(&clipPath, CombineModeReplace);

    g.DrawImage(img,
        Gdiplus::Rect(drawX, drawY, drawW, drawH),
        0, 0, (INT)srcW, (INT)srcH,
        UnitPixel);

    g.Restore(state);
}

void CMainDlg::DrawHubIcon(Graphics& g, const Gdiplus::Rect& r, bool active)
{
    // Idle backing tile (all sidebar tiles sit on a subtle rounded square)
    SolidBrush idle(IconIdleBg());
    FillRoundedRect(g, r, 18, idle);

    Gdiplus::Rect inner(r.X + 6, r.Y + 6, r.Width - 12, r.Height - 12);

    if (m_imgHubIcon != nullptr)
    {
        // Fill the whole tile edge-to-edge - no inset border - so a
        // 512x512 image reads cleanly instead of showing a ring of the
        // idle tile background around it.
        DrawImageCover(g, m_imgHubIcon, r, 18);
    }
    else
    {
        // Blue gradient "hub" tile with a bold X mark - stands in for the
        // top-level brand icon in the reference UI without reproducing any
        // copyrighted artwork.
        LinearGradientBrush grad(inner, IconHubGradTop(), IconHubGradBot(), LinearGradientModeVertical);
        FillRoundedRect(g, inner, 14, grad);

        Pen xPen(Color(255, 255, 255, 255), (REAL)(inner.Width * 0.12));
        xPen.SetStartCap(LineCapRound);
        xPen.SetEndCap(LineCapRound);
        float pad = inner.Width * 0.28f;
        g.DrawLine(&xPen, inner.X + pad, inner.Y + pad, inner.X + inner.Width - pad, inner.Y + inner.Height - pad);
        g.DrawLine(&xPen, inner.X + inner.Width - pad, inner.Y + pad, inner.X + pad, inner.Y + inner.Height - pad);
    }

    if (active)
    {
        Pen ring(IconActiveRing(), 2.5f);
        Gdiplus::Rect ringRect(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2);
        GraphicsPath path;
        MakeRoundedPath(path, ringRect, 18);
        g.DrawPath(&ring, &path);
    }
}

void CMainDlg::DrawGameIcon(Graphics& g, const Gdiplus::Rect& r, bool active, Image* img, const wchar_t* fallbackLabel)
{
    SolidBrush idle(IconIdleBg());
    FillRoundedRect(g, r, 18, idle);

    Gdiplus::Rect inner(r.X + 6, r.Y + 6, r.Width - 12, r.Height - 12);

    if (img != nullptr)
    {
        // Fill the whole tile edge-to-edge - no inset border.
        DrawImageCover(g, img, r, 18);
    }
    else
    {
        LinearGradientBrush grad(inner, IconGameGradTop(), IconGameGradBot(), LinearGradientModeVertical);
        FillRoundedRect(g, inner, 14, grad);

        SolidBrush textBrush(Color(255, 235, 240, 235));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF rc((REAL)inner.X, (REAL)inner.Y, (REAL)inner.Width, (REAL)inner.Height);
        Font f(m_fontFamily, (REAL)(inner.Width * 0.22f), FontStyleBold, UnitPixel);
        g.DrawString(fallbackLabel, -1, &f, rc, &fmt, &textBrush);
    }

    if (active)
    {
        Pen ring(IconActiveRing(), 2.5f);
        Gdiplus::Rect ringRect(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2);
        GraphicsPath path;
        MakeRoundedPath(path, ringRect, 18);
        g.DrawPath(&ring, &path);
    }
}

void CMainDlg::DrawGearIcon(Graphics& g, const Gdiplus::Rect& r, bool active)
{
    if (m_imgSettingsIcon != nullptr)
    {
        // User-supplied artwork (drop a 512x512 image at res\settings.png),
        // filled edge-to-edge with no inset border.
        SolidBrush idle(IconIdleBg());
        FillRoundedRect(g, r, 14, idle);
        DrawImageCover(g, m_imgSettingsIcon, r, 14);

        if (active)
        {
            Pen ring(IconActiveRing(), 2.5f);
            Gdiplus::Rect ringRect(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2);
            GraphicsPath path;
            MakeRoundedPath(path, ringRect, 14);
            g.DrawPath(&ring, &path);
        }
        return;
    }

    Color c = active ? GearActive() : GearIdle();
    Pen pen(c, 2.5f);

    float cx = r.X + r.Width / 2.0f;
    float cy = r.Y + r.Height / 2.0f;
    float outerR = r.Width * 0.34f;
    float innerR = r.Width * 0.15f;
    float toothLen = r.Width * 0.12f;

    // 8 teeth as short radial lines around a ring, with a hollow center
    // circle - a simple, unmistakably "settings" glyph.
    for (int i = 0; i < 8; ++i)
    {
        double a = i * (3.14159265 / 4.0);
        float x1 = cx + (float)cos(a) * outerR;
        float y1 = cy + (float)sin(a) * outerR;
        float x2 = cx + (float)cos(a) * (outerR + toothLen);
        float y2 = cy + (float)sin(a) * (outerR + toothLen);
        g.DrawLine(&pen, x1, y1, x2, y2);
    }
    g.DrawEllipse(&pen, cx - outerR, cy - outerR, outerR * 2, outerR * 2);
    g.DrawEllipse(&pen, cx - innerR, cy - innerR, innerR * 2, innerR * 2);
}

void CMainDlg::DrawCard(Graphics& g)
{
    SolidBrush cardBrush(CardBg());
    FillRoundedRect(g, ToGpRect(m_rcCard), CARD_RADIUS, cardBrush);

    SolidBrush headerBrush(CardHeaderBg());
    FillRoundedRectTop(g, ToGpRect(m_rcCardHeader), CARD_RADIUS, headerBrush);

    const wchar_t* headerText = (m_view == View::Home) ? L"Multiplayer" : L"Settings";
    SolidBrush headerTextBrush(CardHeaderText());
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF rc((REAL)m_rcCardHeader.left + CONTENT_PAD, (REAL)m_rcCardHeader.top,
             (REAL)m_rcCardHeader.Width() - CONTENT_PAD * 2, (REAL)m_rcCardHeader.Height());
    g.DrawString(headerText, -1, m_fontHeader, rc, &fmt, &headerTextBrush);

    Gdiplus::Rect content = ToGpRect(m_rcContent);
    if (m_view == View::Home)
        DrawHomeView(g, content);
    else
        DrawSettingsView(g, content);
}

void CMainDlg::DrawHomeView(Graphics& g, const Gdiplus::Rect& content)
{
    int gi = (int)m_selectedGame;

    // Reserve space for the Play button below the image.
    Gdiplus::Rect imgRect(content.X, content.Y, content.Width,
                           content.Height - m_rcPlayBtn.Height() - 16);

    if (m_imgBackground[gi] != nullptr)
    {
        DrawImageCover(g, m_imgBackground[gi], imgRect, 10);
    }
    else
    {
        LinearGradientBrush grad(imgRect, ImagePlaceholderTop(), ImagePlaceholderBot(), LinearGradientModeVertical);
        FillRoundedRect(g, imgRect, 10, grad);

        SolidBrush subtle(SubtleText());
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF rc((REAL)imgRect.X + 20, (REAL)imgRect.Y, (REAL)imgRect.Width - 40, (REAL)imgRect.Height);
        CString msg = m_strBgDiag[gi].IsEmpty()
            ? (CString(L"Drop a background image at res\\") + kGameBgFile[gi] + L"\nto brand this panel.")
            : m_strBgDiag[gi];
        g.DrawString(msg, -1, m_fontSmall, rc, &fmt, &subtle);
    }

    // Play button
    SolidBrush playBrush(m_hoverPlay ? PlayBtnBgHover() : PlayBtnBg());
    FillRoundedRect(g, ToGpRect(m_rcPlayBtn), 8, playBrush);
    SolidBrush playText(Color(255, 255, 255, 255));
    StringFormat pfmt;
    pfmt.SetAlignment(StringAlignmentCenter);
    pfmt.SetLineAlignment(StringAlignmentCenter);
    RectF prc((REAL)m_rcPlayBtn.left, (REAL)m_rcPlayBtn.top, (REAL)m_rcPlayBtn.Width(), (REAL)m_rcPlayBtn.Height());
    g.DrawString(L"PLAY", -1, m_fontButton, prc, &pfmt, &playText);
}

void CMainDlg::DrawSettingsView(Graphics& g, const Gdiplus::Rect& /*content*/)
{
    SolidBrush labelBrush(LabelText());
    StringFormat lfmt;
    lfmt.SetAlignment(StringAlignmentNear);
    lfmt.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < kGameCount; ++i)
    {
        RectF labelRc((REAL)m_rcContent.left, (REAL)m_rcField[i].top, 260.0f, (REAL)FIELD_H);
        g.DrawString(kGameSettingsLabel[i], -1, m_fontLabel, labelRc, &lfmt, &labelBrush);

        SolidBrush fieldBg(FieldBg());
        FillRoundedRect(g, ToGpRect(m_rcField[i]), 8, fieldBg);

        SolidBrush fieldTextBrush(m_strInstallPath[i].IsEmpty() ? SubtleText() : FieldText());
        StringFormat ffmt;
        ffmt.SetAlignment(StringAlignmentNear);
        ffmt.SetLineAlignment(StringAlignmentCenter);
        RectF frc((REAL)m_rcField[i].left + 14, (REAL)m_rcField[i].top, (REAL)m_rcField[i].Width() - 28, (REAL)m_rcField[i].Height());
        CString text = m_strInstallPath[i].IsEmpty() ? CString(L"Not set") : m_strInstallPath[i];
        g.DrawString(text, -1, m_fontLabel, frc, &ffmt, &fieldTextBrush);

        SolidBrush btnBg(m_hoverBrowse[i] ? ButtonBgHover() : ButtonBg());
        FillRoundedRect(g, ToGpRect(m_rcBrowseBtn[i]), 8, btnBg);
        SolidBrush btnText(ButtonText());
        StringFormat bfmt;
        bfmt.SetAlignment(StringAlignmentCenter);
        bfmt.SetLineAlignment(StringAlignmentCenter);
        RectF brc((REAL)m_rcBrowseBtn[i].left, (REAL)m_rcBrowseBtn[i].top, (REAL)m_rcBrowseBtn[i].Width(), (REAL)m_rcBrowseBtn[i].Height());
        g.DrawString(L"Browse", -1, m_fontButton, brc, &bfmt, &btnText);

        SolidBrush updateBg((m_hoverUpdate[i] || (m_updateBusy && m_updateGame == (Game)i)) ? ButtonBgHover() : ButtonBg());
        FillRoundedRect(g, ToGpRect(m_rcUpdateBtn[i]), 8, updateBg);
        SolidBrush updateText(ButtonText());
        RectF urc((REAL)m_rcUpdateBtn[i].left, (REAL)m_rcUpdateBtn[i].top,
                  (REAL)m_rcUpdateBtn[i].Width(), (REAL)m_rcUpdateBtn[i].Height());
        g.DrawString((m_updateBusy && m_updateGame == (Game)i) ? L"Checking..." : L"Check Updates",
                     -1, m_fontButton, urc, &bfmt, &updateText);
    }

    // --- Update Channel ---
    RectF chLabelRc((REAL)m_rcContent.left, (REAL)m_rcRadioStable.top - 12, 260.0f, (REAL)FIELD_H);
    g.DrawString(L"Update Channel", -1, m_fontLabel, chLabelRc, &lfmt, &labelBrush);

    auto drawRadio = [&](const CRect& dot, bool selected, const wchar_t* label, float labelWidth)
    {
        Gdiplus::Rect gdot = ToGpRect(dot);
        if (selected)
        {
            SolidBrush fillBrush(AccentBlue());
            g.FillEllipse(&fillBrush, gdot);
            SolidBrush innerBrush(Color(255, 255, 255, 255));
            int pad = dot.Width() / 4;
            g.FillEllipse(&innerBrush, gdot.X + pad, gdot.Y + pad, gdot.Width - pad * 2, gdot.Height - pad * 2);
        }
        else
        {
            Pen ringPen(RadioRing(), 2.0f);
            g.DrawEllipse(&ringPen, gdot);
        }

        StringFormat rfmt;
        rfmt.SetLineAlignment(StringAlignmentCenter);
        rfmt.SetAlignment(StringAlignmentNear);
        SolidBrush textBrush(LabelText());
        RectF rc((REAL)dot.right + 8, (REAL)dot.top - 10, labelWidth, (REAL)dot.Height() + 20);
        g.DrawString(label, -1, m_fontLabel, rc, &rfmt, &textBrush);
    };

    drawRadio(m_rcRadioStable, m_updateChannel == 0, L"Stable", 60.0f);
    drawRadio(m_rcRadioExperimental, m_updateChannel == 1, L"Experimental", 110.0f);
}

// ---------------------------------------------------------------------------
// Rounded-rect / image helpers
// ---------------------------------------------------------------------------

CString CMainDlg::ResourcePath(const wchar_t* filename)
{
    // Resolve relative to the .exe rather than the current working
    // directory, so this works the same whether launched from Explorer,
    // Visual Studio's debugger, or a shortcut with a different start-in folder.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    CString dir(exePath);
    int pos = dir.ReverseFind(L'\\');
    if (pos >= 0)
        dir = dir.Left(pos);
    CString result;
    result.Format(L"%s\\res\\%s", dir.GetString(), filename);
    return result;
}

bool CMainDlg::FileExists(const CString& path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int EmbeddedResourceIdForName(const wchar_t* filename)
{
    if (_wcsicmp(filename, L"background_s1x.jpg") == 0) return IDR_IMG_BG_S1X;
    if (_wcsicmp(filename, L"background_iw6x.jpg") == 0) return IDR_IMG_BG_IW6X;
    if (_wcsicmp(filename, L"background_iw4x.jpg") == 0) return IDR_IMG_BG_IW4X;
    if (_wcsicmp(filename, L"s1x.png") == 0) return IDR_IMG_S1X;
    if (_wcsicmp(filename, L"iw6x.png") == 0) return IDR_IMG_IW6X;
    if (_wcsicmp(filename, L"iw4x.png") == 0) return IDR_IMG_IW4X;
    if (_wcsicmp(filename, L"hub.png") == 0) return IDR_IMG_HUB;
    if (_wcsicmp(filename, L"settings.png") == 0) return IDR_IMG_SETTINGS;
    if (_wcsicmp(filename, L"nexus_launcher_asset.png") == 0) return IDR_IMG_NEXUS_ASSET;
    return 0;
}

Image* CMainDlg::LoadEmbeddedImage(const wchar_t* filename, CString& diagOut, IStream** streamOut)
{
    if (streamOut) *streamOut = nullptr;

    const int resourceId = EmbeddedResourceIdForName(filename);
    if (!resourceId)
    {
        diagOut.Format(L"No embedded resource mapping exists for %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    HINSTANCE hInst = AfxGetResourceHandle();
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes)
    {
        diagOut.Format(L"Embedded resource not found: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    HGLOBAL hLoaded = LoadResource(hInst, hRes);
    if (!hLoaded)
    {
        diagOut.Format(L"Failed to load embedded resource: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    const DWORD size = SizeofResource(hInst, hRes);
    const void* data = LockResource(hLoaded);
    if (!data || size == 0)
    {
        diagOut.Format(L"Embedded resource is empty: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hCopy)
    {
        diagOut.Format(L"Out of memory loading embedded resource: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    void* dest = GlobalLock(hCopy);
    if (!dest)
    {
        GlobalFree(hCopy);
        diagOut.Format(L"Failed to lock memory for embedded resource: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }
    memcpy(dest, data, size);
    GlobalUnlock(hCopy);

    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(hCopy, TRUE, &stream);
    if (FAILED(hr) || !stream)
    {
        GlobalFree(hCopy);
        diagOut.Format(L"Failed to create image stream: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    Image* img = Image::FromStream(stream, FALSE);
    if (!img || img->GetLastStatus() != Ok)
    {
        if (img) delete img;
        stream->Release();
        diagOut.Format(L"Failed to decode embedded image: %s", filename);
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    if (streamOut)
        *streamOut = stream;
    else
        stream->Release();

    return img;
}

Image* CMainDlg::LoadOptionalImage(const wchar_t* filename, CString& diagOut)
{
    // Kept as a compatibility helper for callers that may still use it.
    // The launcher itself now uses embedded resources exclusively.
    CString path = ResourcePath(filename);
    if (!FileExists(path))
    {
        diagOut.Format(L"%s not found at:\n%s", filename, path.GetString());
        OutputDebugStringW(diagOut + L"\n");
        return nullptr;
    }

    Image* img = new Image(path);
    if (img->GetLastStatus() != Ok)
    {
        diagOut.Format(L"Found %s but couldn't decode it:\n%s", filename, path.GetString());
        OutputDebugStringW(diagOut + L"\n");
        delete img;
        return nullptr;
    }
    return img;
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

void CMainDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (m_rcDotClose.PtInRect(point))
    {
        PostMessage(WM_CLOSE);
        return;
    }
    if (m_rcDotMin.PtInRect(point))
    {
        ShowWindow(SW_MINIMIZE);
        return;
    }

    for (int i = 0; i < kGameCount; ++i)
    {
        if (m_rcIconGame[i].PtInRect(point))
        {
            SelectGame((Game)i);
            return;
        }
    }

    if (m_rcIconGear.PtInRect(point))
    {
        SetView(View::Settings);
        return;
    }

    if (m_view == View::Settings)
    {
        for (int i = 0; i < kGameCount; ++i)
        {
            if (m_rcBrowseBtn[i].PtInRect(point))
            {
                BrowseForInstall((Game)i);
                return;
            }
            if (m_rcUpdateBtn[i].PtInRect(point))
            {
                CheckForUpdates((Game)i);
                return;
            }
        }
        if (m_rcRadioStable.PtInRect(point) || CRect(m_rcRadioStable.left, m_rcRadioStable.top - 10, m_rcRadioStable.right + 60, m_rcRadioStable.bottom + 10).PtInRect(point))
        {
            m_updateChannel = 0;
            SaveSettings();
            Invalidate();
            return;
        }
        if (m_rcRadioExperimental.PtInRect(point) || CRect(m_rcRadioExperimental.left, m_rcRadioExperimental.top - 10, m_rcRadioExperimental.right + 110, m_rcRadioExperimental.bottom + 10).PtInRect(point))
        {
            m_updateChannel = 1;
            SaveSettings();
            Invalidate();
            return;
        }
    }
    else // Home
    {
        if (m_rcPlayBtn.PtInRect(point))
        {
            TryLaunchGame();
            return;
        }
    }

    CDialogEx::OnLButtonDown(nFlags, point);
}

void CMainDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
    CDialogEx::OnLButtonUp(nFlags, point);
}

void CMainDlg::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_trackingMouse)
    {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    bool newHoverMin = m_rcDotMin.PtInRect(point) != FALSE;
    bool newHoverClose = m_rcDotClose.PtInRect(point) != FALSE;
    bool newHoverPlay = (m_view == View::Home) && (m_rcPlayBtn.PtInRect(point) != FALSE);

    bool newHoverBrowse[kGameCount];
    bool newHoverUpdate[kGameCount];
    bool anyChanged = (newHoverMin != m_hoverMin) || (newHoverClose != m_hoverClose) || (newHoverPlay != m_hoverPlay);
    for (int i = 0; i < kGameCount; ++i)
    {
        newHoverBrowse[i] = (m_view == View::Settings) && (m_rcBrowseBtn[i].PtInRect(point) != FALSE);
        newHoverUpdate[i] = (m_view == View::Settings) && !m_updateBusy && (m_rcUpdateBtn[i].PtInRect(point) != FALSE);
        if (newHoverBrowse[i] != m_hoverBrowse[i] || newHoverUpdate[i] != m_hoverUpdate[i])
            anyChanged = true;
    }

    if (anyChanged)
    {
        m_hoverMin = newHoverMin;
        m_hoverClose = newHoverClose;
        m_hoverPlay = newHoverPlay;
        for (int i = 0; i < kGameCount; ++i)
        {
            m_hoverBrowse[i] = newHoverBrowse[i];
            m_hoverUpdate[i] = newHoverUpdate[i];
        }
        Invalidate();
    }

    CDialogEx::OnMouseMove(nFlags, point);
}

void CMainDlg::OnMouseLeave()
{
    m_trackingMouse = false;
    bool any = m_hoverMin || m_hoverClose || m_hoverPlay;
    for (int i = 0; i < kGameCount; ++i)
        any = any || m_hoverBrowse[i] || m_hoverUpdate[i];

    if (any)
    {
        m_hoverMin = m_hoverClose = m_hoverPlay = false;
        for (int i = 0; i < kGameCount; ++i)
        {
            m_hoverBrowse[i] = false;
            m_hoverUpdate[i] = false;
        }
        Invalidate();
    }
}

LRESULT CMainDlg::OnNcHitTest(CPoint point)
{
    LRESULT hit = CDialogEx::OnNcHitTest(point);
    if (hit == HTCLIENT)
    {
        CPoint client = point;
        ScreenToClient(&client);
        if (m_rcTitleBar.PtInRect(client) &&
            !m_rcDotMin.PtInRect(client) && !m_rcDotClose.PtInRect(client))
        {
            return HTCAPTION;
        }
    }
    return hit;
}

HCURSOR CMainDlg::OnQueryDragIcon()
{
    return (HCURSOR)LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
}

void CMainDlg::OnDestroy()
{
    delete m_fontTitle;  m_fontTitle = nullptr;
    delete m_fontHeader; m_fontHeader = nullptr;
    delete m_fontLabel;  m_fontLabel = nullptr;
    delete m_fontButton; m_fontButton = nullptr;
    delete m_fontSmall;  m_fontSmall = nullptr;
    delete m_fontFamily; m_fontFamily = nullptr;

    for (int i = 0; i < kGameCount; ++i)
    {
        delete m_imgBackground[i]; m_imgBackground[i] = nullptr;
        delete m_imgGameIcon[i];   m_imgGameIcon[i] = nullptr;
    }
    delete m_imgHubIcon;      m_imgHubIcon = nullptr;
    delete m_imgSettingsIcon; m_imgSettingsIcon = nullptr;

    // GDI+ image objects may still reference their backing IStream. Release
    // the streams only after every resource-backed image has been destroyed.
    for (IStream*& stream : m_imgStreams)
    {
        if (stream)
        {
            stream->Release();
            stream = nullptr;
        }
    }

    KillTimer(0x4E58);
    KillTimer(0x4E59);
    m_discord.Shutdown();
    m_discordPresenceActive = false;

    CDialogEx::OnDestroy();
}

// ---------------------------------------------------------------------------
// Discord Rich Presence
// ---------------------------------------------------------------------------

void CMainDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 0x4E58)
        UpdateDiscordPresence();
    else if (nIDEvent == 0x4E59 && m_updateBusy)
    {
        // The worker reports completion through WM_APP + 0x421, so this timer
        // only keeps the custom painted UI responsive while the download runs.
        Invalidate(FALSE);
    }

    CDialogEx::OnTimer(nIDEvent);
}

LRESULT CMainDlg::OnUpdateFinished(WPARAM, LPARAM lParam)
{
    auto* result = reinterpret_cast<NexusUpdateResult*>(lParam);
    if (!result)
        return 0;

    m_updateBusy = false;
    m_hoverUpdate[(int)m_updateGame] = false;

    CWinApp* pApp = AfxGetApp();
    if (m_updateGame == Game::IW4X)
    {
        if (result->version1Installed && !result->version1.empty())
            pApp->WriteProfileString(kSection, L"UpdateVersion_IW4X", result->version1.c_str());
        if (result->version2Installed && !result->version2.empty())
            pApp->WriteProfileString(kSection, L"UpdateVersion_IW4X_Rawfiles", result->version2.c_str());
    }
    else if (result->version1Installed && !result->version1.empty())
    {
        const wchar_t* key = (m_updateGame == Game::S1X) ? L"UpdateVersion_S1X" : L"UpdateVersion_IW6X";
        pApp->WriteProfileString(kSection, key, result->version1.c_str());
    }

    CString message(result->message.c_str());
    AfxMessageBox(message, MB_OK | (result->success ? MB_ICONINFORMATION : MB_ICONWARNING));
    delete result;
    Invalidate();
    return 0;
}

void CMainDlg::UpdateDiscordPresence()
{
    if (m_discordClientId.empty())
        return;

    RunningGame running = FindRunningGame(m_strInstallPath);
    if (!m_discord.IsConnected())
    {
        if (!m_discord.Initialize(m_discordClientId))
            return;
        m_discordPresenceActive = true;
    }

    if (running.index < 0)
    {
        // Keep a presence active while Nexus Launcher itself is open.
        // When a supported game starts, the same Discord connection is reused
        // and the activity changes to the game/server presence below.
        if (m_discordLauncherStartUnix == 0)
            m_discordLauncherStartUnix = (std::int64_t)std::time(nullptr);

        m_discordLastGamePid = 0;
        m_discordLastServer.Empty();
        m_discordStartUnix = 0;
        m_discord.UpdateLauncher(m_discordLauncherStartUnix);
        return;
    }

    // A game just started; give that game its own timer.
    m_discordLauncherStartUnix = 0;

    if (running.pid != m_discordLastGamePid || running.index != (int)m_discordLastGame)
    {
        m_discordLastGamePid = running.pid;
        m_discordLastGame = (Game)running.index;
        m_discordLastServer.Empty();
        m_discordStartUnix = (std::int64_t)std::time(nullptr);
    }

    CString server = NormalizeGameTitle(running.windowTitle, running.index);
    if (server.IsEmpty())
        server = ReadHostnameFromLog(running.installPath);

    // If the client exposes no server hostname, keep the presence useful
    // rather than inventing one. The presence publisher will show "In Game".
    m_discordLastServer = server;

    static const wchar_t* const gameNames[3] = { L"S1X", L"IW6X", L"IW4X" };
    m_discord.Update(gameNames[running.index],
                     std::wstring(m_discordLastServer.GetString()),
                     m_discordStartUnix,
                     running.pid);
}

void CMainDlg::SetView(View v)
{
    if (m_view != v)
    {
        m_view = v;
        RecalcLayout();
        Invalidate();
    }
}

void CMainDlg::SelectGame(Game g)
{
    bool gameChanged = (m_selectedGame != g);
    m_selectedGame = g;

    if (m_view != View::Home)
    {
        m_view = View::Home;
        RecalcLayout();
        Invalidate();
    }
    else if (gameChanged)
    {
        Invalidate();
    }
}

void CMainDlg::BrowseForInstall(Game g)
{
    int gi = (int)g;

    BROWSEINFO bi{};
    wchar_t szPath[MAX_PATH] = {};
    bi.hwndOwner = m_hWnd;
    bi.pszDisplayName = szPath;
    bi.lpszTitle = kGameFolderPrompt[gi];
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl != nullptr)
    {
        if (SHGetPathFromIDList(pidl, szPath))
        {
            m_strInstallPath[gi] = szPath;
            SaveSettings();
            Invalidate();
        }
        CoTaskMemFree(pidl);
    }
}

void CMainDlg::CheckForUpdates(Game g)
{
    if (m_updateBusy)
        return;

    const int gi = (int)g;
    if (m_strInstallPath[gi].IsEmpty())
    {
        CString msg;
        msg.Format(_T("Set your %s install folder in Settings first."), kGameSettingsLabel[gi]);
        AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
        return;
    }

    CWinApp* pApp = AfxGetApp();
    const wchar_t* versionKey = (g == Game::S1X) ? L"UpdateVersion_S1X"
        : (g == Game::IW6X) ? L"UpdateVersion_IW6X" : L"UpdateVersion_IW4X";
    CString current1Cs = pApp->GetProfileString(kSection, versionKey, _T(""));
    std::wstring current1(current1Cs.GetString());
    std::wstring current2;
    if (g == Game::IW4X)
    {
        CString current2Cs = pApp->GetProfileString(kSection, L"UpdateVersion_IW4X_Rawfiles", _T(""));
        current2.assign(current2Cs.GetString());
    }

    std::wstring installPath(m_strInstallPath[gi].GetString());
    m_updateGame = g;
    m_updateBusy = true;
    for (int i = 0; i < kGameCount; ++i)
        m_hoverUpdate[i] = false;
    Invalidate();

    const HWND targetWindow = m_hWnd;
    std::thread([targetWindow, g, installPath, current1, current2]()
    {
        const int index = (int)g;
        NexusUpdateResult* result = new NexusUpdateResult(
            NexusUpdater::CheckAndInstall(index, installPath, current1, current2));

        if (!::IsWindow(targetWindow) || !::PostMessageW(targetWindow, WM_APP + 0x421, static_cast<WPARAM>(0), reinterpret_cast<LPARAM>(result)))
            delete result;
    }).detach();
}

void CMainDlg::TryLaunchGame()
{
    int gi = (int)m_selectedGame;

    if (m_strInstallPath[gi].IsEmpty())
    {
        CString msg;
        msg.Format(_T("Set your %s install folder in Settings first."), kGameSettingsLabel[gi]);
        AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
        SetView(View::Settings);
        return;
    }

    // The real launcher hands this off to the AlterWare/iw4x updater to
    // fetch client files before launching; that networking step isn't
    // wired up here, so we just try to start the game executable directly.
    CString exePath = m_strInstallPath[gi] + _T("\\") + kGameExeName[gi];
    HINSTANCE h = ShellExecute(m_hWnd, _T("open"), exePath, nullptr, m_strInstallPath[gi], SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32)
    {
        CString msg;
        msg.Format(_T("Couldn't find %s in:\n%s\n\nMake sure it's installed there."), kGameExeName[gi], m_strInstallPath[gi].GetString());
        AfxMessageBox(msg, MB_OK | MB_ICONWARNING);
    }
}

void CMainDlg::LoadSettings()
{
    CWinApp* pApp = AfxGetApp();
    for (int i = 0; i < kGameCount; ++i)
        m_strInstallPath[i] = pApp->GetProfileString(kSection, kGameInstallKey[i], _T(""));
    m_updateChannel = pApp->GetProfileInt(kSection, kKeyChannel, 1);
}

void CMainDlg::SaveSettings()
{
    CWinApp* pApp = AfxGetApp();
    for (int i = 0; i < kGameCount; ++i)
        pApp->WriteProfileString(kSection, kGameInstallKey[i], m_strInstallPath[i]);
    pApp->WriteProfileInt(kSection, kKeyChannel, m_updateChannel);
}
