# Flying Toasters 2026 — Multi-Monitor Edition

> A 64 KB native Win32 wrapper that brings the iconic flying-toaster
> screensaver to **every monitor at once** on Windows 10 / 11.

![Flying toasters across four monitors](docs/screenshots/multi-monitor.jpg)

---

## Why this exists

I keep an old [After Dark](https://en.wikipedia.org/wiki/After_Dark_%28software%29)
"Flying Toasters" screensaver around for the nostalgia — it's a
community reconstruction of the 1995 freeware classic. I downloaded it
from a personal blog years ago and reinstalled it after building a new
Windows 11 box.

There was just one problem: my desk has **four monitors**, and the
toasters only flew on one of them. The other three sat awkwardly black
while a tiny swarm of poultry-themed kitchenware flapped around on the
primary display. Not ideal.

I poked at the binary expecting to find a bug, and the issue turned out
to be more fundamental than that: the engine never enumerates monitors
at all. It calls `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`, creates
exactly one window the size of the primary display, and that's the
universe it lives in. There is no `EnumDisplayMonitors` import in the
PE table. It's not a bug, it's a design from a single-monitor era.

Patching the binary to add multi-monitor would be a multi-day adventure
in Ghidra: relocate the imports table, inject new code, rewire the
window-creation path, and even then the engine's render loop assumes a
single swap chain. So I did the obvious thing instead — **wrap it**.

This repo is the wrapper. It enumerates every monitor, creates a
borderless host window per display, and spawns the original engine in
preview mode (`/p HWND`) targeted at each host. The engine renders
inside each one as if it were a giant settings-dialog preview.

I first wrote it in C# / .NET 8 (75 MB of self-contained binary — gross
for what is fundamentally a tiny shim), then rewrote it in pure C / Win32
and got it down to **64 KB**. A 1180× size reduction for the same
behavior. The full installer is **3.4 MB**.

I'm publishing the whole thing — wrapper source, build scripts,
installer script, bundled engine, documentation — for anyone with the
same multi-monitor itch.

If you like this kind of thing I write more of it on my blog:
**[jgz.sh](https://jgz.sh)**.

![Single monitor close-up of the toasters in flight](docs/screenshots/single-monitor.jpg)

---

## Features

- **64 KB** native Win32 executable (no .NET, no MSVCRT redist, no DLLs)
- **Multi-monitor**: an independent toaster swarm on every display
- **PerMonitor V2** DPI awareness — respects per-display scaling on Win11
- **Singleton mutex** prevents double-launch
- **Low-level mouse + keyboard hooks** for system-wide dismissal
- **Mouse-move dead zone** (6 px / 750 ms) so it doesn't quit on cursor jitter
- **Standard `.scr` protocol** (`/s`, `/p HWND`, `/c[:HWND]`) — preview
  in Settings and the Configure button still work
- **3.4 MB installer**, MIT-licensed wrapper, freeware engine

---

## Install

### Installer (recommended)

Grab `FlyingToasters2026-MultiMonitor-1.0.0-Setup.exe` from the
[Releases page](https://github.com/chaoticgeniu5/flying-toasters-2026-multimonitor/releases/latest).
Run, accept the UAC prompt, click through the wizard.

The installer:

- Drops the wrapper + engine into `C:\Program Files (x86)\Flying Toasters 2026\`
- Optionally mirrors them to `C:\Windows\System32\` so the screensaver
  appears in **Settings → Personalization → Lock screen → Screen saver**
- Optionally registers itself as the active screensaver for the
  current user
- Backs up any pre-existing `flyingtoasters2026.scr` in `System32` so
  uninstall can restore it

### Portable (no installer)

Grab `FlyingToasters2026-MultiMonitor-1.0.0-Portable.zip`, unzip
anywhere, right-click `install.ps1` → **Run with PowerShell** (UAC).
Same outcome as the installer, no entry in Add/Remove Programs.

### From source

```powershell
git clone https://github.com/chaoticgeniu5/flying-toasters-2026-multimonitor
cd flying-toasters-2026-multimonitor
.\scripts\build.ps1     # compiles src/wrapper.c with Zig → ~64 KB .scr
.\scripts\package.ps1   # builds Setup.exe + Portable.zip in build/output/
```

`build.ps1` needs [Zig](https://ziglang.org/download/) (`winget install zig.zig`).
`package.ps1` additionally needs [Inno Setup 6](https://jrsoftware.org/isdl.php)
(`winget install JRSoftware.InnoSetup`).

---

## How it works

```
Windows                      Wrapper                       Engine
  │                            │                             │
  │  flyingtoasters2026.scr /s │                             │
  │ ─────────────────────────▶ │                             │
  │                            │ EnumDisplayMonitors()       │
  │                            │ ────────► [mon1, mon2, ...] │
  │                            │                             │
  │                            │ for each monitor:           │
  │                            │   CreateWindowEx(WS_POPUP)  │
  │                            │   CreateProcess(            │
  │                            │     engine, /p HWND_i)      │
  │                            │ ─────────────────────────▶  │
  │                            │                             │   renders
  │                            │   SetWindowsHookEx(WH_*_LL) │   into HWND_i
  │                            │                             │
  │                            │ message loop                │
  │                            │                             │
  │  user moves mouse / types  │                             │
  │ ─────────────────────────▶ │ TerminateProcess(each)      │
  │                            │ DestroyWindow(each)         │
  │                            │ exit                        │
```

### Pieces

| File                        | What it does                                              |
|-----------------------------|-----------------------------------------------------------|
| `src/wrapper.c`             | Everything: argument parsing, host-window class, monitor enumeration, engine spawn, low-level hooks, cleanup. ~400 LOC. |
| `src/wrapper.rc`            | Resource script. Embeds the manifest as RT_MANIFEST and adds VERSIONINFO. |
| `src/app.manifest`          | `requestedExecutionLevel=asInvoker`, `dpiAwareness=PerMonitorV2`, `supportedOS` for Vista→Win11, `activeCodePage=UTF-8`, `longPathAware`. |
| `engine/flyingtoasters2026_engine.scr` | The original toaster engine, renamed. Bundled (see CREDITS). |
| `scripts/build.ps1`         | Calls `zig cc` with size-optimized flags. ~64 KB output. |
| `scripts/package.ps1`       | Runs build + Inno Setup + zips the portable bundle.      |
| `scripts/installer.iss`     | Inno Setup script (English).                              |
| `scripts/install.ps1`       | Portable installer for end users (no Inno).              |
| `scripts/uninstall.ps1`     | Counterpart of install.ps1.                               |

### Operational details

- **Dead zone**: ignore mouse movement for the first 750 ms and within
  6 px of the cursor's starting position. Prevents the saver from
  quitting on the same cursor twitch that triggered it.
- **Mutex**: `Local\FlyingToasters2026_Wrapper_Singleton`. Prevents
  the rare double-invocation cases where Windows fires the saver twice.
- **DPI**: `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` is
  resolved at runtime (Win10 1703+). Falls back to `SetProcessDPIAware`.
  The manifest also declares it as belt-and-suspenders.
- **Cleanup ordering on exit**: unhook → terminate every engine →
  `WaitForMultipleObjects(1500ms)` → close handles → destroy host
  windows → restore cursor → release mutex. Critical that the hooks
  go away before window destruction, otherwise the destroy itself can
  trigger the activity hook in a feedback loop.
- **Cursor**: wrapper hides cursor via `ShowCursor(FALSE)` loop; host
  windows respond `TRUE` to `WM_SETCURSOR` with `NULL` so child
  windows from the engine inherit no cursor either.

### What I deliberately didn't do

- **No binary patching of the engine.** The engine is intentionally
  treated as a black box. If it gets updated upstream, just drop in
  the new file and rename it.
- **No DirectX hooking.** Render path is the engine's problem.
- **No Settings UI extensions.** The engine's `/c` config dialog is
  passed through unchanged; my wrapper has no preferences of its own.
- **No service / scheduled task / autostart.** It's a screensaver,
  Windows handles invocation.

---

## Customize

All knobs live as macros at the top of `src/wrapper.c`:

```c
#define ENGINE_FILENAME    L"flyingtoasters2026_engine.scr"
#define APP_TITLE          L"Flying Toasters 2026"
#define MUTEX_NAME         L"Local\\FlyingToasters2026_Wrapper_Singleton"
#define WND_CLASS_NAME     L"FlyingToasters2026SaverHost"
#define MAX_MONITORS       32
#define MOVE_DEAD_ZONE_PX  6
#define INITIAL_QUIET_MS   750
```

Want a different render engine? Replace `engine/flyingtoasters2026_engine.scr`
with any standard Windows screensaver `.scr` that respects `/p HWND`,
update `ENGINE_FILENAME`, recompile.

Want a "primary monitor renders, others go black" variant instead of
N independent renders? In `RunFullscreen`, skip the per-monitor spawn
loop and instead spawn the engine once with `/s`, then create the
host windows only on non-primary monitors.

---

## Build details

```powershell
# Zig (recommended — single 80 MB toolchain, no system install)
zig cc src/wrapper.c src/wrapper.rc -o build/flyingtoasters2026.scr `
  -Oz -target x86_64-windows-gnu -municode `
  -Wl,--subsystem,windows -Wl,--gc-sections -Wl,-s `
  -ffunction-sections -fdata-sections `
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector `
  -lkernel32 -luser32 -lshell32 -lgdi32 -ladvapi32
```

| Compiler           | Flags                                  | Output size |
|--------------------|----------------------------------------|------------:|
| Zig 0.16, `-Oz`    | full size optimization (above)         |     **64 KB** |
| Zig 0.16, `-O3`    | speed-optimized                        |     ~197 KB |
| MSVC 19, `/Oz /Os` | with `/OPT:REF /OPT:ICF`               |     ~50 KB  |
| MinGW-w64 GCC      | `-Os -s --gc-sections`                 |     ~70 KB  |

Cold start: ~10 ms.

---

## Verifying downloads

```powershell
# In the folder where you downloaded the artifacts:
Get-FileHash -Algorithm SHA256 .\FlyingToasters2026-MultiMonitor-1.0.0-Setup.exe
Get-FileHash -Algorithm SHA256 .\FlyingToasters2026-MultiMonitor-1.0.0-Portable.zip
```

Compare against `SHA256SUMS.txt` in the release.

---

## A note on SmartScreen

The installer is **not code-signed** (signing certificates cost money
and are tied to legal identity). The first time you run the `.exe`,
Windows SmartScreen will say "Windows protected your PC". Click
**More info → Run anyway**. If you'd rather not, build it yourself
from source — it's literally one PowerShell command.

---

## Credits & license

- **Wrapper** (everything in `src/`, `scripts/`, and the compiled
  `flyingtoasters2026.scr`): MIT. See [LICENSE](LICENSE).
- **Engine** (`engine/flyingtoasters2026_engine.scr`): community
  reconstruction of a 1995 freeware screensaver. See [CREDITS.md](CREDITS.md).
  Bundled in good faith on the basis that the original release was
  freeware with no surviving rightsholder claims and the
  reconstruction is non-commercial. Contact me if that's wrong and
  I'll yank it.

---

## More side projects from me

I write about things like this — small Win32 hacks, reverse
engineering, weird software archaeology — on my blog:

### **[jgz.sh →](https://jgz.sh)**

If a 1995 freeware reconstruction wrapped in a 64 KB Win32 launcher
sounds like your kind of thing, you'll probably enjoy the rest.

---

## Contributing

PRs welcome. Especially:

- MSVC build script (currently only Zig is wired up)
- "Plan B" rendering mode (single engine on primary, blackouts on others)
- Optional config UI for dead-zone tuning
- ARM64 build target

Open an issue first if it's a substantial change so we can sanity-check
direction.
