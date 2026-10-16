# Build and verify

Use Windows 11 x64, PowerShell, and [Zig](https://ziglang.org/download/).
No Visual Studio, .NET, or Inno Setup is required. Zig 0.15.2 is the toolchain
used for this change; the output is native Win32 using Windows system DLLs.

```powershell
./scripts/build.ps1
# Or specify a portable compiler:
./scripts/build.ps1 -ZigPath C:\Tools\zig\zig.exe
./scripts/package.ps1 -ZigPath C:\Tools\zig\zig.exe
```

Output: `build/multisaver.scr`, then `build/output/MultiSaver-Portable.zip`
and `SHA256SUMS.txt`. The package excludes `engine/`. The build uses size
optimization, a PerMonitorV2 DPI manifest, and warnings treated as errors.

In restricted environments, put Zig and its caches in a short writable path.
Create these directories before setting the process-local environment:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = 'C:\work\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = 'C:\work\zig-local'
$env:TEMP = 'C:\work\tmp'
$env:TMP = $env:TEMP
```

## Automated checks

```powershell
./scripts/test.ps1 -ZigPath C:\Tools\zig\zig.exe
```

The native harness verifies INI fallback/override/black behavior, relative and
environment paths, invalid paths, self/hard-link rejection, and Job Object
cleanup. It writes temporary files in `build/test-work`; it does not select a
system screen saver or open fullscreen windows.

## Manual system checks

These require two or more extended-desktop monitors and independently tested
preview-compatible children; successful compilation does not establish them:

- `/c`, `/c:HWND`, and no arguments open INI; Unicode paths survive editing.
- Windows Settings preview renders inside the parent, and closes its children
  when the preview window closes.
- `/s` renders the right child on each monitor, including negative coordinates
  and mixed DPI; missing children leave black hosts.
- A key, mouse buttons/wheels, and movement after the grace period dismiss all
  children. No child or descendant remains after launcher exit or termination.
- Swap primary display and rearrange screens: assignments follow device IDs.
  Replug/dock changes can create new IDs; rerun `/c` to inspect them.
- Display topology changes during `/s` close the session.
- Verify Windows sign-in-on-resume behavior separately.

Compatibility is child-specific. Do not label an untested `.scr` as supported.
