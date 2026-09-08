#pragma once
#include <gdiplus.h>

// ---------------------------------------------------------------------------
// UITheme.h
//
// Central place for every color and layout constant used by MainDlg's custom
// painting. Tweak values here to retint or reflow the whole UI without
// touching the drawing code itself.
// ---------------------------------------------------------------------------

namespace UITheme
{
    // ---- Window ----
    constexpr int WIN_W = 1200;
    constexpr int WIN_H = 720;
    constexpr int WINDOW_CORNER_RADIUS = 14;  // outer window shape (SetWindowRgn)

    // ---- Title bar ----
    constexpr int TITLEBAR_H = 44;
    constexpr int DOT_DIAM = 14;
    constexpr int DOT_GAP = 24;      // spacing between the two dots
    constexpr int DOT_MARGIN = 24;      // distance of the close dot from the right edge

    // ---- Sidebar ----
    constexpr int SIDEBAR_W = 148;
    constexpr int ICON_SIZE = 96;
    constexpr int ICON_TOP = 56;
    constexpr int ICON_GAP = 32;
    constexpr int GEAR_SIZE = 64;
    constexpr int GEAR_BOTTOM_MARGIN = 24;  // gear sits pinned near the window bottom, not right below the icon group

    // ---- Content card ----
    constexpr int CARD_MARGIN = 24;      // outer margin around the card
    constexpr int CARD_GAP = 16;      // gap between sidebar and card
    constexpr int CARD_RADIUS = 12;
    constexpr int HEADER_H = 64;
    constexpr int CONTENT_PAD = 24;
    constexpr int SIDEBAR_DIVIDER_MARGIN = 24; // inset of the hub/games and games/settings divider lines

    // ---- Controls ----
    constexpr int FIELD_H = 44;
    constexpr int BROWSE_BTN_W = 110;
    constexpr int ROW_GAP = 68;
    constexpr int RADIO_DIAM = 20;

    // ---- Colors (ARGB) ----
    inline Gdiplus::Color WindowBg()       { return Gdiplus::Color(255, 24, 27, 31); }
    inline Gdiplus::Color TitleBarBg()     { return Gdiplus::Color(255, 20, 23, 26); }
    inline Gdiplus::Color TitleText()      { return Gdiplus::Color(255, 200, 203, 207); }
    inline Gdiplus::Color DotMinimize()    { return Gdiplus::Color(255, 217, 164, 65); }
    inline Gdiplus::Color DotMinimizeHi()  { return Gdiplus::Color(255, 235, 182, 85); }
    inline Gdiplus::Color DotClose()       { return Gdiplus::Color(255, 196, 68, 58); }
    inline Gdiplus::Color DotCloseHi()     { return Gdiplus::Color(255, 216, 88, 78); }

    inline Gdiplus::Color CardBg()         { return Gdiplus::Color(255, 36, 39, 43); }
    inline Gdiplus::Color CardHeaderBg()   { return Gdiplus::Color(255, 30, 33, 36); }
    inline Gdiplus::Color CardHeaderText() { return Gdiplus::Color(255, 225, 227, 230); }

    inline Gdiplus::Color IconIdleBg()     { return Gdiplus::Color(255, 33, 36, 40); }
    inline Gdiplus::Color IconActiveRing() { return Gdiplus::Color(255, 66, 148, 214); }
    inline Gdiplus::Color IconHubGradTop() { return Gdiplus::Color(255, 58, 130, 196); }
    inline Gdiplus::Color IconHubGradBot() { return Gdiplus::Color(255, 34, 84, 138); }
    inline Gdiplus::Color IconGameGradTop(){ return Gdiplus::Color(255, 60, 110, 74); }
    inline Gdiplus::Color IconGameGradBot(){ return Gdiplus::Color(255, 28, 58, 40); }
    inline Gdiplus::Color GearIdle()       { return Gdiplus::Color(255, 150, 154, 160); }
    inline Gdiplus::Color GearActive()     { return Gdiplus::Color(255, 235, 237, 239); }
    inline Gdiplus::Color SidebarDivider() { return Gdiplus::Color(28, 255, 255, 255); } // subtle light-grey hairline over the dark sidebar

    inline Gdiplus::Color LabelText()      { return Gdiplus::Color(255, 205, 208, 212); }
    inline Gdiplus::Color SubtleText()     { return Gdiplus::Color(255, 130, 134, 140); }
    inline Gdiplus::Color FieldBg()        { return Gdiplus::Color(255, 20, 22, 25); }
    inline Gdiplus::Color FieldText()      { return Gdiplus::Color(255, 190, 193, 197); }
    inline Gdiplus::Color ButtonBg()       { return Gdiplus::Color(255, 44, 47, 51); }
    inline Gdiplus::Color ButtonBgHover()  { return Gdiplus::Color(255, 54, 58, 63); }
    inline Gdiplus::Color ButtonText()     { return Gdiplus::Color(255, 225, 227, 230); }

    inline Gdiplus::Color AccentBlue()     { return Gdiplus::Color(255, 47, 143, 209); }
    inline Gdiplus::Color RadioRing()      { return Gdiplus::Color(255, 100, 104, 110); }

    inline Gdiplus::Color PlayBtnBg()      { return Gdiplus::Color(255, 47, 143, 209); }
    inline Gdiplus::Color PlayBtnBgHover() { return Gdiplus::Color(255, 63, 158, 224); }
    inline Gdiplus::Color ImagePlaceholderTop() { return Gdiplus::Color(255, 46, 50, 55); }
    inline Gdiplus::Color ImagePlaceholderBot() { return Gdiplus::Color(255, 22, 24, 27); }
}
