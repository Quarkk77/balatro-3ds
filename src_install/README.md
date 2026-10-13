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

## Playing in the Azahar emulator

No 3DS? balatro_c also runs in [Azahar](https://azahar-emu.org/) on the same computer.

1. Install Azahar and start it once (this creates its virtual SD card).
2. Run the installer with `--azahar`:

   ```
   ./install_balatro_c.sh --azahar
   ```

   It finds Azahar's virtual SD card (Linux, including the Flatpak, macOS and Windows) and installs
   balatro_c there. If yours is somewhere else (e.g. a portable Azahar), give it with
   `--azahar --sd PATH/TO/sdmc`.
3. In Azahar, choose **File > Load File** and open `sdmc/3ds/balatro_c/balatro_c.3dsx`
   (the installer prints the full path).

New 3DS mode must be on (Emulation > Configure > System), otherwise balatro_c shows its Old 3DS
screen. Touch works with the mouse on the bottom screen.

## Options

```
--sd PATH       the SD card's root folder, if it is not found (or several are)
--azahar        install into the Azahar emulator instead of an SD card
--game PATH     Balatro.exe, Balatro.love or the Balatro folder, if it is not in Steam's usual place
--3dsx PATH     a different balatro_c.3dsx
--no-3dsx       copy only the assets (e.g. after updating balatro_c through Universal-Updater)
-y              do not ask for confirmation
```

## Sound

The 3DS needs a dump of its DSP firmware for homebrew to play sound: run the DSP1 homebrew once,
which creates `/3ds/dspfirm.cdc`. Without it balatro_c runs silent. In Azahar, copy that file from your
3DS's SD card to `sdmc/3ds/dspfirm.cdc` in Azahar's virtual SD card for sound (it comes from the console
and cannot be included).

## Saves

Saves are kept in `/3ds/balatro_c/` next to the game, in the same format as the PC version.
Reinstalling or updating does not touch them.
