# MultiSaver — native Windows per-monitor .scr launcher

A small C/Win32 adaptation of [Flying Toasters 2026 Multi-Monitor](https://github.com/chaoticgeniu5/flying-toasters-2026-multimonitor).
Each monitor can run a different traditional screen saver selected in an INI.
No service, startup manager, .NET runtime, or installer is required.

The original architecture is retained: enumerate monitors, create one borderless
host per display, and start compatible screen savers with `/p HWND`.
Children must support embedded preview and multiple instances. Input dismisses
all children together. Arbitrary `.scr` files are not guaranteed to work.

## Quick start

1. Build with `./scripts/build.ps1` using Zig, or unpack the portable ZIP into a writable folder.
2. Run `multisaver.scr /c`. This creates a UTF-16 `multisaver.ini`, adds sections with connected monitors' device interface IDs, and opens Notepad.
3. Set `[Launcher] DefaultSaver` and each monitor section's `Saver` to trusted `.scr` paths. Blank per-monitor values inherit the default; `none` leaves that screen black. See `multisaver.example.ini`.
4. Run `multisaver.scr /s` to try the layout. Move the mouse or press a key to exit. Use Windows Screen Saver Settings to select the launcher and manage idle time and sign-in on resume.

`/s`, `/c[:HWND]`, and `/p HWND` remain supported. Configure edits launcher
settings; preview uses `DefaultSaver`. Configure individual children with their
own `/c` commands. Mapping uses device interface identity, never enumeration
index. Port/dock or driver changes may still change identities.

See [configuration and compatibility](docs/CONFIGURATION.md) and [build and verification](docs/BUILD.md).
The portable ZIP places both documents at its root.

## License and upstream provenance

The wrapper, scripts, and new documentation/examples are MIT licensed; the
original copyright and credits are retained. The historical third-party binary
under `engine/` is not MIT licensed and its redistribution permission is not
established by the upstream credits. It is unused by default and excluded from
this fork's package. Supply your own licensed screen savers.

The former engine-specific installation scripts are removed in this branch.
Packaging now creates only a portable launcher ZIP and checksums.
