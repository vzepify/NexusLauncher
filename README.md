# Nexus Launcher (Win32/MFC)

A **frameless C++/MFC launcher** for the AlterWare family: **S1x**
(Advanced Warfare), **IW6x** (Ghosts), and **IW4x** (Modern Warfare 2),
styled after the screenshots you shared of the XLabs launcher: a custom
title bar (colored min/close dots), a left icon sidebar (hub icon, then
S1x/IW6x/IW4x game icons, then a settings gear), and a rounded "card" on
the right that shows either the selected game's **Multiplayer** panel or a
**Settings** panel with one install-path field per game.

There are no dialog controls in this project at all — every pixel (title
bar, sidebar tiles, card, text fields, buttons, radio dots) is hand-drawn
with GDI+ in `MainDlg.cpp`, and clicks are resolved with plain hit-testing.
That's what makes it possible to match the flat/rounded look exactly instead
of fighting default Win32 control chrome.

## Opening it

1. Install **Visual Studio 2022** with the **"Desktop development with C++"**
   workload, and make sure the **"MFC and ATL support (x86 and x64)"**
   optional component is checked (it's not installed by default).
2. Open `IW4xLauncher.sln`.
3. Build/Run (`F5`) — Debug or Release, x86 or x64 all work.

This project was authored outside of Windows, so it hasn't been compiled by
an MFC toolchain here — if VS flags anything on first open (uncommon, but
possible with hand-written `.vcxproj`/`.rc` files), it's almost always a
missing MFC/ATL workload component rather than a code issue.

## What's implemented

- Frameless custom window with rounded corners (drag by the title bar,
  minimize/close dots)
- Sidebar navigation: hub icon (decorative), S1x/IW6x/IW4x icons switch the
  Home view to that game, gear icon → Settings
- **Home**: "Multiplayer" card for whichever game is selected, with that
  game's background image and a **PLAY** button that launches its exe
  (`s1x.exe` / `iw6x.exe` / `iw4x.exe`) from its configured install folder
  via `ShellExecute`
- **Settings**: one install-folder picker per game (Advanced Warfare,
  Ghosts, Modern Warfare 2) plus a shared Stable/Experimental update-channel
  toggle, matching the reference app's layout
- All settings persist between runs (stored under
  `HKCU\Software\IW4xLauncher\Settings` via `CWinApp::WriteProfileString`)

## What's intentionally *not* implemented

- **No update/download logic.** The real IW4x/AlterWare launcher's actual
  job is checking GitHub releases and downloading/patching client files
  before launch — that's networking work (WinINet/WinHTTP or libcurl) that's
  out of scope for a UI-focused rebuild. `TryLaunchGame()` just tries to run
  the selected game's exe directly from its configured install folder as a
  stand-in.
- **No copyrighted assets.** The "Multiplayer" background image and the
  hub/game icons in the original screenshots are the real project's branded
  artwork and a copyrighted game screenshot — I didn't copy those. Instead,
  both spots support your own images (see "Adding your own art" below), and
  fall back to a drawn placeholder if the files aren't there.

## Adding your own art

Drop these files into the project's `res\` folder and rebuild:

- `res\hub.png` — the top "X" tile image, **512x512 recommended**.
- `res\s1x.png`, `res\iw6x.png`, `res\iw4x.png` — each game's sidebar tile
  image, **512x512 recommended**.
- `res\settings.png` — the gear/settings tile image, **512x512 recommended**.
- `res\background_s1x.jpg`, `res\background_iw6x.jpg`,
  `res\background_iw4x.jpg` — each game's Home/Multiplayer panel image.

All of these are "cover"-scaled (cropped to fill their tile, not squished)
and fall back to the original drawn placeholder if the file isn't found or
can't be decoded.

A **Post-Build step** copies the whole `res\` folder next to the built
`.exe` every time the project actually builds, so the app finds them at
`<exe folder>\res\...` regardless of where you launch it from.

**Important:** if you only add/replace files in `res\` without changing any
source file, Visual Studio's incremental build may decide the project is
already "up to date" and skip the build entirely — including the copy step.
If your images aren't showing up, use **Build → Rebuild Solution** (not
just Build) after dropping files in, which always re-runs everything.

If they still don't show, the Home panel itself will now tell you exactly
what went wrong instead of silently falling back — it prints either "not
found at: `<path>`" (the copy step didn't run, or the file's misnamed) or
"couldn't decode it" (the file's there but isn't a format GDI+ can read).
That path is also worth checking by hand in Explorer to confirm the file
actually landed next to the `.exe`.

The top "X" hub icon is decorative only (not clickable, never highlighted);
only the S1x/IW6x/IW4x tiles switch the Home view (to that game).

## Project layout

```
IW4xLauncher.sln
IW4xLauncher/
  IW4xLauncher.vcxproj(.filters)   Project file (MFC, GDI+, x86/x64, Debug/Release)
  IW4xLauncher.rc                  Borderless dialog resource + app icon
  resource.h
  IW4xLauncher.h / .cpp            CWinApp - starts/stops GDI+, runs the dialog
  MainDlg.h / .cpp                 The entire UI: layout, drawing, input
  DiscordPresence.h / .cpp         Native Discord IPC Rich Presence client
  MainDlg.cpp             Your Discord application Client ID
  UITheme.h                        All colors + layout constants in one place
  pch.h / pch.cpp, framework.h, targetver.h   Standard MFC precompiled header setup
  res/IW4xLauncher.ico             App icon
```

To retint or reflow the UI, start in `UITheme.h` — every color and spacing
constant used by `MainDlg.cpp` lives there.


## Discord Rich Presence

Nexus Launcher now includes a lightweight native Discord Rich Presence client. It monitors **S1X**, **IW6X**, and **IW4X** while the launcher is running and publishes:

- `Nexus Launcher` as the Discord application name (configured in the Discord Developer Portal).
- The active game as the details line, such as `IW4X - Multiplayer`.
- The detected server name as the state line when the game exposes it through its main window title or multiplayer log.
- A session start timestamp so Discord shows the elapsed play time.

### Configure the Discord application

1. Create a Discord application and use **Nexus Launcher** as its name. Discord's Rich Presence documentation uses the application's Client ID for native RPC connections.
2. Copy the application's Client ID into `IW4xLauncher\MainDlg.cpp` next to `IW4xLauncher.vcxproj`.
3. In the Discord Developer Portal, open **Rich Presence -> Art Assets** and upload `res\nexus_launcher_asset.png`. Give the asset the key/name `nexus_launcher`.
4. Optionally upload separate images with the keys `nexus_s1x`, `nexus_iw6x`, and `nexus_iw4x` if you want different artwork for each game.
5. Rebuild the solution. The post-build step copies `MainDlg.cpp` next to the executable.

The launcher presence references the `nexus_launcher` asset, so the question-mark placeholder in Discord means Discord could not find an uploaded asset with that key yet. Discord recommends high-resolution Rich Presence art (1024x1024 is recommended).

The Discord integration uses Discord's native Windows IPC transport (`\\?\pipe\discord-ipc-0` through `discord-ipc-9`) and the `SET_ACTIVITY` RPC command, so there is no extra Discord DLL to install. Discord documents native RPC over IPC and `SET_ACTIVITY` as the Rich Presence command.

> **Server-name note:** server name detection is best-effort from the active game window title and recent multiplayer logs. The launcher cannot guarantee a server name if a specific client build does not expose that information to external processes.

## GitHub game updates

Settings now has a **Check Updates** button beside each **Browse** button. The update worker runs in the background so the launcher UI does not freeze while GitHub files are being downloaded or extracted.

- **S1X:** checks `CBServers/s1x-client`. That repository is archived and its README points to the CBServers updater executable rather than a GitHub Release asset, so Nexus uses the repository's newest tag as the version marker and downloads the linked latest `s1x.exe` updater binary.
- **IW6X:** checks `CBServers/iw6x-client` the same way and installs the linked latest `iw6x.exe` binary.
- **IW4X:** checks `iw4x/iw4x-client` for its latest release and installs `iw4x.dll`.
- **IW4X Rawfiles:** the IW4X button also checks `iw4x/iw4x-rawfiles` and downloads/extracts its latest `release.zip` into the configured MW2/IW4x directory.

Nexus stores the last successfully installed release/tag in the launcher registry settings so clicking the button again does not download the same version unnecessarily. The temporary download files are written under `%TEMP%\\NexusLauncher` and the rawfiles ZIP is deleted after a successful extraction.


## Discord Client ID
The Discord Application Client ID is compiled into the launcher in `MainDlg.cpp` (`kDiscordClientId`). Replace the placeholder before building. No `discord_client_id.txt` file is required.


## Linux build

The source now also contains `LinuxNexusLauncher/`, a native Qt 6 Linux version
that follows the same Nexus Launcher flow instead of trying to compile the
Windows-only MFC/GDI+ code on Linux.

It supports S1X, IW6X and IW4X, GitHub updates, live update progress, embedded
artwork, Discord Rich Presence, and launching the Windows game clients through
Wine. See `LinuxNexusLauncher/README.md`.

The original `IW4xLauncher.sln` remains the Windows build.
