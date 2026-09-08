# Nexus Launcher — Linux support

This folder adds a native Linux build of Nexus Launcher while leaving the existing
Windows MFC project unchanged.

The Linux build uses Qt 6 Widgets and keeps the same launcher concepts:

- Nexus Launcher branding and three-game sidebar: S1X, IW6X and IW4X
- Install-folder settings with Browse buttons
- Check Updates buttons with a live progress bar
- GitHub update checks/downloads for the same repositories used by the Windows launcher
- IW4X client DLL + `iw4x-rawfiles` `release.zip`
- Embedded launcher artwork via Qt resources (`.qrc`) — no `res` directory is needed at runtime
- Native Discord Rich Presence using Discord IPC over Linux Unix sockets
- Launcher-open presence and game presence
- Best-effort server-name detection from `xdotool` window titles or common game log files
- Windows `.exe` clients launched through Wine

## Requirements

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-tools-dev unzip wine pgrep xdotool
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel unzip wine procps-ng xdotool
```

Arch:

```bash
sudo pacman -S --needed base-devel cmake qt6-base unzip wine procps-ng xdotool
```

`xdotool` is optional and is only used for richer Discord server-name detection.
The launcher still works without it.

## Build

From this directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The executable is:

```text
build/NexusLauncher
```

Or use:

```bash
./scripts/build-linux.sh
```

## Run

```bash
./build/NexusLauncher
```

The first time you run it, open Settings and select the installation folder for
each game. The game clients themselves are Windows `.exe` files, so Linux launch
uses Wine by default.

You can override Wine:

```bash
NEXUS_WINE_COMMAND=/usr/bin/wine ./build/NexusLauncher
```

Proton can also be used by pointing `NEXUS_WINE_COMMAND` to the Proton/Wine
launcher you normally use.

## Data

Qt stores launcher settings using the native Linux configuration backend. The
launcher does not create a `discord_client_id.txt` file.

Update downloads are stored temporarily under:

```text
$TMPDIR/NexusLauncher
```

and removed after installation.

## Discord Rich Presence

The Discord application Client ID is compiled into the Linux launcher source,
matching the Windows build. Update the `kDiscordClientId` constant in
`src/mainwindow.cpp` before distributing a release.

Rich Presence uses:

```text
nexus_launcher
```

as the large image key, so create the same Rich Presence asset in your Discord
application.

While only Nexus is open, Discord shows the launcher activity. When S1X, IW6X or
IW4X is detected, it switches to the corresponding game activity and attempts
to show the server/window title.

## Windows vs Linux

The original `IW4xLauncher.sln` remains the Windows build.

The Linux build is intentionally separate because MFC, GDI+, WinHTTP and
Windows process APIs used by the existing application are Windows-only. The
Linux version replaces those platform pieces with Qt, Linux process launching,
Qt networking and Qt resources while keeping the launcher behavior and
branding consistent.

## GitHub Actions

`.github/workflows/linux.yml` builds the Linux executable on an Ubuntu runner and
publishes a tarball as a workflow artifact. It is suitable for turning the Linux
build into a GitHub Release later.

