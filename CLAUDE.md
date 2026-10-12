# balatro_c

C port of Balatro (from its Lua source), with a desktop build and a Nintendo 3DS build.
The 3DS port is the active work.

## Layout
- `src_lua/` - the game's Lua source and assets, extracted from Balatro.exe. Read-only: never modify.
- `src/` - desktop C port (raylib). Only touch it for bugs shared with the 3DS port (apply the fix to both).
- `src_3ds/` - 3DS port (devkitARM, libctru, citro3d, ndsp). `src_3ds/PORTING_NOTES.md` is the detailed
  design doc (screen layout rules, shader approximations, memory collector, debug keys). Read it first.
- `build/`, `build_3ds/` - build outputs (gitignored).
- `tools/devkitpro/install_devkitpro.sh` - one-time system install of devkitPro (sudo).
- `tools/3ds_test/` - emulator test helper + a tutorial-completed settings save.
- The Steam install (`~/.local/share/Steam/steamapps/common/Balatro`) must never be modified.

## Building
- 3DS: `cd src_3ds && make` -> `build_3ds/balatro_c.3dsx` and `.cia` (`make 3dsx` for just the 3dsx).
  Needs DEVKITPRO/DEVKITARM (set by /etc/profile.d/devkit-env.sh after the devkitPro install) and
  `makerom` + `bannertool` (installed in ~/.local/bin).
- Desktop: `cmake -S src -B build -G Ninja && ninja -C build && ./build/balatro`.

## Testing the 3DS build (Azahar emulator, installed system-wide as `azahar`)
- `tools/3ds_test/run_emu.sh '<debug.txt lines>' <seconds> out.png` (`NOTUT=1` = skip the tutorial).
  It wipes Azahar's `sdmc/3ds/balatro_c` test folder, writes `debug.txt`, runs, kills Azahar, tiles the
  top-screen shots; bottom-screen shots (320x240, the window) are `shot_NN_bottom.png` next to them.
  Click coordinates are game coordinates: bottom-shot x + 80 in a run, + 40 on menus/overlays. Delete the sdmc
  `balatro_c` folder when done so the user's next launch is clean.
- debug.txt keys: SHOTS, QUIT_AFTER, SCRIPT (`t:click=x,y[,hold]`, `t:button=a`, `t:key=..`; steps run in
  order, keep them time-sorted), FPS (log with heap/linear memory), TEST_SEED, TEST_EASY, TEST_LOADOUT,
  TEST_STRAIGHT, TEST_BIGCARD(_EXP), TEST_NANEINF, TEST_NOPE, TEST_JUMBO_ARCANA, FORCE_O3DS, GC_STRESS,
  GC_EXPLAIN. Also: script taps on the run setup PLAY are more reliable than button a.
- Emulator speed varies run to run; give scripted taps generous spacing. Emulator fps is not hardware fps.
- The user tests on real New 3DS hardware; Old 3DS shows a lockout screen.

## Working with this user
- Tokens are limited: do straightforward tests, use few emulator runs and mid-test screenshots only when needed.
- Commit only when asked. Version scheme: tags `3DS-b.0.x[.y]` (latest: `3DS-b.0.3`); bump
  `PORT_VERSION` in `src_3ds/game/game.h` and the note in PORTING_NOTES.md to match when committing.
  Commit messages end with the Co-Authored-By line.
- Ask before decisions that are genuinely theirs (e.g. layout tradeoffs); otherwise act.
- Keep PORTING_NOTES.md updated with each change.

## Known open points
- Memory collector passes cause ~45-160ms hitches every ~20s of heavy play (emulator timing).
- Shop and booster pack card areas still use widths scaled by the 10% larger cards (unchecked).
- When tutorial Jimbo is on the top screen, his Next button needs the A button (top screen has no touch).
- Bottom screen is a 1:1 320px window (x 80..400 in a run, 40..360 otherwise). Not yet checked in it:
  game over screen, settings tabs, card tooltips at the window edges, Run Info's other tabs.
- Shop contents vary slightly between runs with the same seed (also on desktop).
