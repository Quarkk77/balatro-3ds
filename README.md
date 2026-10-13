# balatro-3ds

A fan port of [Balatro](https://www.playbalatro.com/) to the New Nintendo 3DS, written in C from the
game's original Lua source. Not affiliated with LocalThunk or Playstack.

**This repository contains none of Balatro's files** (art, sounds, fonts, shaders, text). To play or
build it you need your own copy of Balatro, bought through Steam.

Current version: `3DS-b.0.4.2` (beta).

## Playing on a 3DS

Needs a New 3DS / New 3DS XL / New 2DS XL with custom firmware and the Homebrew Launcher
(the original 3DS is not supported), and Balatro 1.0.1o installed through Steam on your computer.

1. Get `balatro_c.3dsx` (build it, see below) and `src_install/install_balatro_c.sh`.
2. Put the 3DS's SD card in your computer and run `./install_balatro_c.sh`. It finds Balatro in your
   Steam libraries, extracts the assets the port needs, and copies everything to `/3ds/balatro_c/`.
3. Start balatro_c from the Homebrew Launcher.

For sound, run the DSP1 homebrew once to create `/3ds/dspfirm.cdc`. Full details and options:
[`src_install/README.md`](src_install/README.md).

## Repository layout

| Folder | What it is |
| --- | --- |
| `src_3ds/` | The 3DS port (devkitARM, libctru, citro3d, ndsp). Design notes: [`src_3ds/PORTING_NOTES.md`](src_3ds/PORTING_NOTES.md). |
| `src/` | The desktop C port (raylib): a straight refactor of the base game that the 3DS port was built from. See [`src/README.md`](src/README.md). |
| `src_install/` | The installer that copies the assets from your own copy of Balatro to the SD card. |
| `tools/` | devkitPro install script and emulator test helpers. |

## Building

The desktop build reads Balatro's assets from `src_lua/`, which you extract from your own copy
(the 3DS build needs nothing from it; the 3DS reads the assets the installer puts on the SD card):

```
unzip ~/.local/share/Steam/steamapps/common/Balatro/Balatro.exe -d src_lua
```

(unzip warns about the exe header; that is expected.)

**3DS** - needs devkitPro (devkitARM, libctru, citro3d; `tools/devkitpro/install_devkitpro.sh` installs
it), plus `makerom` and `bannertool` for the .cia:

```
cd src_3ds && make        # build_3ds/balatro_c.3dsx and .cia  (make 3dsx for just the .3dsx)
```

**Desktop**:

```
cmake -S src -B build -G Ninja && ninja -C build && ./build/balatro
```

## Status

Beta. The full game runs on New 3DS hardware, with the top screen showing the play area and the
bottom screen a 1:1 touch window. Known limits are listed in `src_3ds/PORTING_NOTES.md`.
