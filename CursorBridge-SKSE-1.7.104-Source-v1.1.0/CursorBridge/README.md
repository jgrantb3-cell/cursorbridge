# CursorBridge

CursorBridge is an SKSE plugin for Skyrim Special Edition / Anniversary Edition.
While a cursor-driven Skyrim menu is open, it repeatedly releases the Windows
cursor clip so the pointer can cross onto another monitor without Alt+Tab.

Version 1.1 is adapted for the current Steam runtime **1.7.104** and **SKSE
2.3.1**. It must be built against the current CommonLibSSE-NG/vcpkg port (6.7.0
or later) so it understands the Address Library format used by 1.7.104.

## Important Windows limitation

Moving between monitors does not change focus. Clicking another application does:
Windows will give that application mouse focus. Clicking Skyrim again returns menu
control. No Alt+Tab is needed, but two applications cannot receive the same mouse
click simultaneously.

## Requirements

- Steam Skyrim Anniversary Edition 1.7.104
- SKSE64 2.3.1
- Address Library for SKSE Plugins updated for 1.7.104
- Microsoft Visual C++ 2015-2022 Redistributable (x64)

SSE Display Tweaks may remain installed. Use `LockCursor=false` in its `[Window]`
section. Borderless windowed mode is recommended.

## Build

Install Visual Studio 2022 (Desktop development with C++), CMake, Ninja, Git, and
vcpkg. Set `VCPKG_ROOT`, then run from a Developer PowerShell:

```powershell
cmake --preset release
cmake --build --preset release
```

The installable files are produced under `build/package`. Zip the contents of that
folder so `SKSE` is at the archive root, then install through a mod manager.

## Diagnostics

After launching through SKSE, this log should exist:

`Documents/My Games/Skyrim Special Edition/SKSE/CursorBridge.log`

It should contain `Cursor release worker started` after reaching the main menu.

## Compatibility

The 1.1 build target is Steam 1.7.104. The plugin does not modify saves, Papyrus
scripts, game records, or menu files. Because CommonLibSSE-NG 6.7.0 and later is
GPL-3.0-or-later, CursorBridge is distributed under GPL-3.0-or-later as well.

The source intentionally contains no hard-coded relocation IDs or byte-pattern
hooks; it uses CommonLib's UI event interface and the stable Win32 `ClipCursor`
API. A Windows in-game test is still required before treating a build as a
finished public release.
