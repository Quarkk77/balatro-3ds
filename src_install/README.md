# Installing balatro_c

balatro_c is a fan port of Balatro to the New Nintendo 3DS. It is not affiliated with LocalThunk
or Playstack, and it does **not** include any of Balatro's files: you need your own copy of
Balatro, bought and installed through Steam, on the computer you run the installer on.

## What you need

- A New 3DS / New 3DS XL / New 2DS XL with custom firmware and the Homebrew Launcher.
  (The original 3DS is not supported.)
- Its SD card, connected to your computer.
- Balatro installed through Steam on that computer (version 1.0.1o, the current Steam version).
- Linux (including the Steam Deck), macOS, or Windows with Git Bash, MSYS2 or WSL.

## Installing

1. Put the SD card in your computer.
2. Run the installer from the folder you unpacked balatro_c into:

   ```
   ./install_balatro_c.sh
   ```

   It finds Balatro in your Steam libraries and your 3DS SD card (any drive with a `3ds` folder),
   asks before copying, and puts everything in `/3ds/balatro_c/` on the SD card:

   ```
   /3ds/balatro_c/balatro_c.3dsx
   /3ds/balatro_c/assets/         textures, sounds, fonts and text from your copy of Balatro
   ```

3. Eject the SD card safely, put it back in the 3DS and start balatro_c from the Homebrew Launcher.

If balatro_c is started without the assets, it shows a screen explaining how to install them.

## Options

```
--sd PATH       the SD card's root folder, if it is not found (or several are)
--game PATH     Balatro.exe, Balatro.love or the Balatro folder, if it is not in Steam's usual place
--3dsx PATH     a different balatro_c.3dsx
--no-3dsx       copy only the assets (e.g. after updating balatro_c through Universal-Updater)
-y              do not ask for confirmation
```

## Sound

The 3DS needs a dump of its DSP firmware for homebrew to play sound: run the DSP1 homebrew once,
which creates `/3ds/dspfirm.cdc`. Without it balatro_c runs silent.

## Saves

Saves are kept in `/3ds/balatro_c/` next to the game, in the same format as the PC version.
Reinstalling or updating does not touch them.
