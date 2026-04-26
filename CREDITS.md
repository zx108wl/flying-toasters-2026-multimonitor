# Credits

## The wrapper

Everything under `src/` and `scripts/`, and the compiled
`flyingtoasters2026.scr`, is original work licensed under MIT
(see [LICENSE](LICENSE)).

## The engine

The bundled `engine/flyingtoasters2026_engine.scr` is a **community
reconstruction** of a 1995 freeware screensaver depicting flying
toasters and slices of toast. The original concept dates back to the
*After Dark* screensaver suite by Berkeley Systems (released
1989–1996); various freeware reimplementations have circulated over
the decades.

This repository:

- **Does not claim authorship** over the engine binary.
- **Does not commercialize it** in any way.
- **Bundles it** solely so that this multi-monitor wrapper is useful
  as a turnkey download, rather than requiring users to source the
  engine separately from a defunct blog.

The engine is shared on the basis that:

1. The original 1995 release was freeware.
2. There is no surviving rightsholder asserting active claims that I
   am aware of.
3. The reconstruction itself is a non-commercial community effort.

If you are an original author or rightsholder and have an objection,
please open an issue or contact me directly and I will remove the
binary from the bundle immediately.

## Acknowledgements

- The original *After Dark* team at Berkeley Systems for the cultural
  artifact that made this whole thing meaningful in the first place.
- The anonymous author of the 2017 reconstruction, whose binary still
  works perfectly thirty years after the originals.
- Everyone who built the modern Win32 documentation on
  [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/),
  which made this wrapper possible without owning a Windows SDK book.
- The [Zig project](https://ziglang.org/), for shipping a complete
  C compiler + mingw toolchain in a single 80 MB download with no
  system install required.
