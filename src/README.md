# src/ - desktop C port (template)

A straight refactor of Balatro's original Lua source into C, running on raylib instead of LÖVE.
It is the template the 3DS port (`../src_3ds/`) was built from: it keeps the base game's behaviour,
layout and logic as close to the Lua as possible, with no 3DS-specific changes.

Compiled, it produces just Balatro - the same game, as a native desktop executable.

## No game files included

This repository contains none of Balatro's own files (art, sounds, fonts, shaders, localization).
You must supply them yourself from your own copy of the game:

```
unzip ~/.local/share/Steam/steamapps/common/Balatro/Balatro.exe -d ../src_lua
```

(unzip warns about the exe header; that is expected.)

## Building

```
cmake -S src -B build -G Ninja && ninja -C build && ./build/balatro
```

(run from the repository root). The build copies `src_lua/resources` and `src_lua/localization` next to
the executable, which loads them from its own directory. Without them the game will not start.

See `PORTING_NOTES.md` for the Lua -> C translation conventions.
