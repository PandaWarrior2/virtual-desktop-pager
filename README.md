# VDesktop Pager

A native virtual desktop pager for the Windows 11 taskbar. See your desktops, switch between them, and create or remove a desktop without opening Task View.

**Version:** 1.0 · **Author:** Moonl1ght  
**Repository:** [PandaWarrior2/virtual-desktop-pager](https://github.com/PandaWarrior2/virtual-desktop-pager)

![VDesktop Pager icon](assets/vdesktop.png)

## Features

- A widget on the primary taskbar and every additional monitor's taskbar.
- Window layout thumbnails and window counts specific to each monitor and desktop.
- Click and mouse wheel navigation, plus a button to create a desktop.
- Quick removal of a desktop from the context menu.
- Automatic positioning, DPI scaling, and Windows light and dark themes.
- An English interface and a Windows 11 style About window with Mica, rounded cards, author details, and a repository link.

Thumbnails are **window layout diagrams**, not live captures of window contents. Minimized windows count toward the total but are not drawn. A window spanning monitors belongs to the monitor containing the largest part of it; its diagram is clipped to that monitor's work area. Windows with an unknown desktop ID, shell utility windows, and windows hidden by their application are excluded.

## Requirements

- Windows 11, x64.
- A supported virtual desktop COM interface: builds 26100 with UBR 2605 or later, 26200, or 26300.
- For multiple monitors, enable **Show my taskbar on all displays** in Windows taskbar settings.

Build **26300.9711** was tested locally. Other allowed builds have not been tested locally. The virtual desktop API uses internal Windows interfaces, so Windows updates can require compatibility changes. Unknown build families are rejected before calling internal methods.

## Build and run

Install Visual Studio Build Tools with the **Desktop development with C++** workload, x64 tools, and a Windows SDK. The project was built with Visual Studio 18 Build Tools.

From the repository directory:

```powershell
.\build.bat
Start-Process .\bin\vdesktop.exe -WindowStyle Hidden
```

No administrator privileges are required. The app runs without a console window. Starting it again does not create a second widget. Windows startup registration is not installed automatically.

Before rebuilding a running copy, unload the widget and close `bin\preview.exe` if it is open:

```powershell
Start-Process .\bin\vdesktop.exe -ArgumentList /stop -Wait -WindowStyle Hidden
.\build.bat
Start-Process .\bin\vdesktop.exe -WindowStyle Hidden
```

To stop, choose **Exit widget** from the context menu or run:

```powershell
Start-Process .\bin\vdesktop.exe -ArgumentList /stop -Wait -WindowStyle Hidden
```

Stopping unloads the DLL from Explorer; Explorer does not need to restart.

Open **About…** from the widget's context menu, or run:

```powershell
Start-Process .\bin\vdesktop.exe -ArgumentList /about -WindowStyle Hidden
```

About also works when the widget is not running. Select **View on GitHub** to open the repository in your default browser.

## Controls

| Action | Result |
| --- | --- |
| Click a desktop | Switch to it |
| Scroll up / down over the widget | Switch to the previous / next desktop |
| Click **+** | Create a desktop and switch to it |
| Hover over a desktop | Show its name and window count |
| Right-click | Open the desktop list and widget commands |
| Right-click a tile → **Remove desktop…** | Immediately remove that desktop |
| Click a page arrow | Show more desktop tiles when space is limited |

When the menu is opened outside a tile, removal targets the current desktop. The last remaining desktop cannot be removed. Windows moves windows from a removed desktop to its previous neighbor, or to the next desktop when removing the first one. The target desktop ID is captured when the menu opens, so changes in desktop order cannot change the removal target.

A blue outline and bottom indicator mark the active desktop. Desktop names supplied by Windows or the user are preserved in their original language.

## Placement and configuration

The widget attaches to all Windows taskbars. Each monitor has its own placement, DPI, window counts, and diagrams. Taskbar appearance and disappearance are detected automatically.

The controller refreshes state and placement every 100 ms and notifies the widgets immediately. Commands from the widget trigger an immediate refresh. Actual response time also depends on Explorer's taskbar updates. Existing taskbar buttons are not moved. If space is limited, desktop tiles are paginated; if even one tile cannot fit, the widget hides.

Edit `vdesktop.ini` in the repository root. Dimensions are device-independent pixels (DIP), scaled to the taskbar's DPI.

```ini
[Pager]
AutoPosition=1
Left=300
MaxWidth=520
TileWidth=52
```

- `AutoPosition=0` enables a manual position using `Left`.
- `MaxWidth` limits the widget width.
- `TileWidth` sets each desktop tile's width.
- Restart the widget after changing the placement mode. Width values are reloaded automatically.

Automatic placement reads taskbar button bounds through UI Automation. If those bounds are temporarily unavailable, the last known position for that monitor is retained.

## Validation

Build and run the layout and window-model checks:

```powershell
.\build.bat test
```

This also builds the integration test executables. With the widget running and at least two desktops:

```powershell
.\bin\widget-test.exe
```

With two monitors connected:

```powershell
.\bin\multimonitor-test.exe
```

These integration tests briefly switch desktops. The widget test checks click, wheel, and rapid navigation, then restores the original desktop. The multi-monitor test creates its own temporary windows and desktop, checks per-monitor placement and window transfer, removes its temporary desktop, and restores the original desktop count and selection.

Inspect the running widget:

```powershell
.\bin\vdesktop.exe /status | Out-String
```

An additional COM integration test creates a temporary desktop, switches to it, restores the original desktop, and removes only its own temporary desktop:

```powershell
.\bin\vdesktop.exe /self-test | Out-String
```

If cleanup fails, it reports the remaining temporary desktop's ID. This test also briefly switches desktops.

Validation on Windows **26300.9711**, October 17, 2026 included:

- An x64 build without compiler warnings.
- 87,936 layout combinations, plus click target and overflow checks.
- Desktop enumeration, creation, switching, restoration, and removal.
- Click and wheel handling in the actual Explorer child window.
- Start, repeated start, stop, and recovery after controller termination.
- Two taskbars with independent placement and window diagrams.
- Moving a test window between monitors and verifying its diagram follows it.
- Desktop switching and repositioning of both widgets in 437 ms in a local test.
- Quick removal of a temporary desktop while preserving its window on a neighboring desktop.

`bin\preview.exe` opens the same widget renderer in a standalone window for visual inspection.

## Implementation

The application is C++/Win32. `vdesktop.exe` enumerates desktops, monitors, and windows, handles commands, and watches Explorer. One injected `vdesktop-widget.dll` creates child windows for `Shell_TrayWnd` and every `Shell_SecondaryTrayWnd` and draws them using Direct2D, DirectWrite, and DirectComposition. Local shared memory contains a separate state snapshot for each monitor.

Injection and transparent rendering were adapted from the user-provided `C:\wndinject` implementation. The injector uses `OpenProcess → VirtualAllocEx → WriteProcessMemory → CreateRemoteThread(LoadLibraryW)`, with x64 validation, taskbar-owner process selection, remote module address resolution, and duplicate-load protection. The original files in `C:\wndinject` were left unchanged; this repository contains the adapted implementation and does not require that directory to build.

The running controller reloads the widget after Explorer restarts. The widget unloads automatically if the controller exits. About is a modeless native window rendered with Direct2D and DirectComposition over the Windows 11 DWM backdrop; it does not block widget updates.

Virtual desktop interface definitions were checked against [MScholtes/VirtualDesktop for Windows 11 24H2](https://github.com/MScholtes/VirtualDesktop/blob/master/VirtualDesktop11-24H2.cs). Attribution and the upstream license are in [THIRD_PARTY.md](THIRD_PARTY.md) and `reference/`.

Limitations: x64 only, horizontal taskbars, up to 16 monitors and 128 displayed desktops. Dragging windows between desktops and live content previews are not implemented.

Controller log: `bin\vdesktop.log`.

## Screenshots
![alt text](image.png)

## Icon assets

`assets/vdesktop.svg` is the vector source. The repository also includes the PNG and multi-resolution ICO used by the application. To regenerate them, install Python and Pillow, then run:

```powershell
python tools/build_icon.py
```

The ICO contains 16, 20, 24, 32, 40, 48, 64, 96, 128, and 256 pixel variants. Both the application and widget DLL embed the icon and version metadata.
