# Porting notes (Lua -> C)

This file records the conventions used while translating `src_lua/` into C.
Keep it in sync when conventions change.

## Layout
- `core/`   : utilities (vectors, strings, hash maps, LuaJIT PRNG, files), `dval` (Lua table literal parser/serializer).
- `gfx/`    : love.graphics / love.window / love.audio replacement on raylib (vendored in `third_party/raylib`).
- `engine/` : port of `engine/*.lua` (Node/Moveable merged into `Node`, Sprite, Event, UI, DynaText, Particles, Controller, sound).
- `game/`   : port of the game code (card.lua, cardarea.lua, blind.lua, back.lua, tag.lua, card_character.lua, game.lua,
              globals.lua, functions/*.lua) and generated data (`data_*.c`).
- `tools/`  : LuaJIT scripts that generate C data from the original Lua tables.

## Objects
- Every object begins with `Node n` (Node + Moveable fields merged). Subclasses: `Sprite`, `UIBox`, `UIElement`,
  `DynaText`, `Particles`, `Card`, `CardArea`, `Blind`, `CardCharacter`.
- Virtual methods are in `NodeVT`; call through `node_draw()`, `node_remove()`, ... Base implementations are
  `Node_xxx` / `Moveable_xxx` / `Sprite_xxx` functions (equivalent of `Node.draw(self)`).
- `n->T` is a pointer because glued sprites share their card's transform (`Moveable:glue_to_major`).
  `pinch` and `shadow_parrallax` are pointers for the same reason.
- `self.children` -> `Children` vector of (name, node). Named children via `child_get/child_set`; array children use
  `child_push` with NULL name.
- Removed nodes are not freed (Lua kept them alive through GC and closures still reference them).
- Per-instance method overrides in Lua (`sprite.hover = function...`) use the `o_*` override pointers on Node.

## Values
- Lua `nil` for numbers in config tables: UI config uses NAN for `w,h,r,outline` (see `UICFG_DEFAULTS`),
  0 for fields where 0 and nil behave the same. Tri-state booleans use int (0 nil, 1 true, -1 false).
- Colours are `float*` (Col) to 4 floats, shared by reference like Lua tables. `G.C.X` entries are Col pointers.
- `ref_table/ref_value` pairs become typed `Ref` (REFN/REFS/REFB/REFNODE/REFCOL...).
- Strings stored in structs are heap strings owned by the struct (`str_set`) unless documented as interned/const.
- `G.FUNCS.name` become C functions `G_FUNCS_name(UIElement *e)`; UI configs hold function pointers. A name registry
  (`funcs_register`) exists for the few places that look functions up by string.

## Closures / events
- `G.E_MANAGER:add_event(Event({...}))` -> `add_event(EVENT(.trigger=..., .func=fn, .ctx=ctx))`.
  Captured locals go into a heap struct allocated with `CTX(T)`; it is freed when the event is destroyed.
- Shared captured variables across several events use a context without free (`.ctx_free = noop`) or globals.

## Randomness
- `math.random`/`math.randomseed` are bit-exact LuaJIT 2.0 (`lj_random*` in core.c), so seeded runs match the
  original. `pseudorandom_element` sorts candidates the same way the Lua does.
- Iteration over Lua hash tables (`pairs`) is reproduced using the order LuaJIT 2.0 produces where it affects RNG
  (e.g. the poker hand list built for To Do List).

## Saves
- Saves live in `$XDG_DATA_HOME/balatro_c` (default `~/.local/share/balatro_c`), same file names and the same
  deflate-compressed Lua table literal format as the original (`settings.jkr`, `N/profile.jkr`, `N/meta.jkr`, `N/save.jkr`).
- All file writes go through `dv_save_file`, which copies its path first: callers pass `tfmt()` temporaries and the
  serializer reuses that ring buffer.

## 3DS build (this folder)
This is the desktop port in `../src` retargeted to the Nintendo 3DS. Game logic is unchanged; the
platform layer is new.

### Building
Needs devkitPro with `3ds-dev` and `3ds-zlib` (DEVKITPRO set), plus `makerom` and `bannertool` on PATH
for the .cia.
```
cd src_3ds
make            # ../build_3ds/balatro_c.3dsx and ../build_3ds/balatro_c.cia
make 3dsx       # only the Homebrew Launcher build
make clean
```
Assets are copied from `../src_lua` into `../build_3ds/romfs` and embedded in both files: 1x textures,
sounds, shader files (only their names are used), fonts except the GoNoto fallbacks, localization.
`meta/` holds the icon (Balatro.exe's own 32x32 icon, scaled to 48x48), banner, banner jingle and the makerom RSF (80MB mode on Old 3DS, 124MB on New 3DS).

### Running
- New 3DS only for now: on an Old 3DS (`APT_CheckNew3DS`) the game is replaced by a lockout screen
  (`o3ds_lockout` in main.c): scrolling "Nope!" tiles on top, an explanation and the notice on the
  bottom; START or HOME exits. `FORCE_O3DS=1` in debug.txt shows it on a New 3DS.
- Saves: `sdmc:/3ds/balatro_c/` (same .jkr format as the desktop build, so saves can be copied across).
- Sound needs the DSP firmware dump at `sdmc:/3ds/dspfirm.cdc` (made by the DSP1 homebrew); without it the
  game runs silent.
- Top screen: the game at 400x240. Bottom screen: the same picture scaled to 320x192; touching it is the
  mouse, using the game's own touch mode (`HID 'touch'`, as LOVE reports touches).
- Buttons are the gamepad (the game's controller support): A/B/X/Y, L/R shoulders, ZL/ZR triggers,
  Start/Select, D-pad, Circle Pad = left stick, C-stick (New 3DS) = right stick.
- Button glyphs are always shown (also when playing by touch), always from the Nintendo row of
  gamepad_ui.png with the face buttons mapped to the 3DS button that triggers them (A confirm, B back,
  X play, Y discard). `meta/gamepad_ui_3ds.png` is a copy of the 1x atlas with START / SELECT pills in
  place of Switch "+" / "-"; the Makefile installs it into romfs. The game never prompts for L3/R3.

### What was replaced
- raylib -> `gfx/gfx_3ds.c` (citro3d) and `gfx/audio_3ds.c` (ndsp). `gfx.h` is the same interface.
- Rendering: one vertex shader (`gfx/vshader.v.pica`); the love.graphics transform stack runs on the CPU and
  draws are batched into one linear-memory vertex buffer per frame. Everything renders into a 400x240
  target. Each frame is recorded as a list of draw batches tagged with a screen mask and replayed once per
  screen (`gfx_set_screen_mask`), so objects can be limited to one screen without changing draw order:
  the title logo, its card and the version label are top-screen only, the opening Jimbo is bottom-screen
  only, and on the main menu the buttons and every menu opened from them (with their cards and tooltips)
  are bottom-screen only. During a run everything is on both screens (`screen_for` in game_update.c).
- Main menu layout (3DS): the title is vertically centred on the top screen; Play/Options/Quit/Collection
  are centred on the bottom screen and, with the profile and language buttons, scaled by `MENU_SCALE`
  (ui_setup.c). The language button is its own box (bottom right) instead of part of `G.MAIN_MENU_UI`.
- In a run, the HUD row under the score is split per screen: the round stats (hands, discards, money,
  ante, round) are a box inside G.HUD (`G.HUD_stats`, top screen) and Run Info / Options are a separate
  box centred over that row (`G.HUD_buttons`, bottom screen). `UIBox.screen_mask` limits a box to one
  screen (and `UIConfig.screen_mask` one element subtree); the `hud()` lookups also search G.HUD_buttons.
  Also per screen: the blind panel (G.HUD_blind, the blind select prompt and the Blind chip) is top only;
  round score and chips x mult are bottom only. Run Info / Options are wider (3.6) with larger text.
  Tooltips and drag popups (G.I.POPUP) use the screen of the element they belong to. Card tooltips are drawn 1.6x
  (scaled about the point nearest their card, kept on screen) so the description text is readable.
  Tutorial: Jimbo (his card, speech bubble and Next button) is on one screen, the bottom unless the step
  highlights something only the top screen shows (`tutorial_jimbo_screen`); each highlight is drawn on its
  own screen. His speech text is 1.8x (`LocArgs.text_scale`). When he is on the top screen, Next is
  pressed with A.
  Blind select: the bottom screen shows only each blind's Select (or Upcoming) button and, for small/big
  blinds, "or" + skip tag + Skip Blind; the top screen shows only the blind info (name, chip, score,
  reward, boss "Up the Ante"). One layout, drawn per screen with `UIConfig.draw3ds`: SPLIT_BOX draws the
  box with only its first child's height on the bottom and without it on top; SHIFT draws an element moved
  up by what that removed. On the bottom screen taps (Node_collides_with_point) and tooltips follow the
  shifted drawing (ui3ds_bottom_shift).
  Cards in hand (and the hand area, which draws them) are bottom screen only. While picking cards the hand is
  centred on the screen. Cards are 10% larger (`CARD_SCALE_3DS`, game.h); the hand, play, joker and
  consumable areas keep the original widths, so the layout does not move. Played cards (G.play) are top screen
  only; while still far from their place they are drawn further down, so they fly in from below the top
  screen. Discarded cards stay on the screen they were last on. Attention text (scoring popups, the
  "-1" on hands/discards) follows the screen of the element it is attached to.
- The main menu shows `PORT_VERSION` ("3DS-b.0.1.4", game.h); `G.VERSION` stays the game's own version
  because it is written into saves. Debug screenshots also write `shot_NN_bottom.png`.
- Textures: PNGs are decoded with stb_image on first draw (lazy, to save memory) and cut into power-of-two
  tiles of at most 1024x1024; a quad that spans tiles is drawn in pieces. The two studio logos are
  downscaled. Texture rows are stored top row first: row 0 is sampled at v = 1, and render targets also
  put y = 0 in row 0.
- Fonts: stb_truetype, rasterised at 32px into 512x512 LA8 pages and scaled (the game asks for ~200px).
- Shaders: the PICA200 has no fragment shaders, so each LOVE shader maps to a fixed-function approximation
  (`shader_look` in gfx_3ds.c): dissolve/negative are plain draws with alpha fade and burn tint, shadows are
  black silhouettes, foil/holo/polychrome/hologram/debuff/played are tinted overlays, the splash and
  background shaders are flat colour gradients, flash is a white fade. CRT, flame, voucher/booster/
  gold-seal/negative shine overlays are not drawn. There is no G.CANVAS; the game draws straight to the
  screen target.
- Audio: stb_vorbis. Short sounds are decoded once into a 6MB LRU cache; music and long sounds stream. All
  five music tracks still "play" (the game crossfades them), but a silent track gives up its DSP channel and
  only keeps a clock, seeking back into sync when it becomes audible.
- Saves: zlib raw deflate (same format as LOVE/raylib). The SD card cannot rename over a file, so the old
  file is removed first.
- Main thread stack raised to 2MB (`__stacksize__` in main.c); the default 32KB overflows in UI layout.
- Fixed settings: 1x textures, CRT off, video settings hidden, external links off.

### Known limits
- Removed nodes are reclaimed by a small conservative collector (`core/heap.c`): every x* allocation is
  registered, and removed nodes that nothing reachable from the globals still points at are freed together
  with the blocks only they reach. Code still checks `->REMOVED` on leftovers as in the Lua, which stays safe
  because referenced nodes are never freed. Supporting changes: allocations start zeroed, vacated vector
  slots are cleared, DVal arrays are scanned by type (their doubles look like addresses on 32-bit), and
  strings are never scanned. Bugs it exposed (also present in the desktop port, where they only leak):
  UIElement re-init registered elements twice, nodes were only unlisted from the current stage's
  STAGE_OBJECTS, and Blind never left G.I.CARD.
  A pass runs after 300 new removals (or 15s with 100+). In the emulator it takes 45-70ms in play
  (up to ~135ms right after loading a saved run), and the heap stays around 2-4MB instead of growing.
  A few hundred removed nodes stay pinned by false or genuine references (e.g. the splash card static).
  Debug keys: `GC_STRESS=1` collects every frame, `GC_EXPLAIN=n` logs why n survivors are still held.
- Only tested in the Azahar emulator, not on hardware; real Old 3DS speed is unknown.
- CJK fonts are 6-10MB each and loaded into RAM when those languages are picked.

### Debug / test hooks
Put `KEY=value` lines in `sdmc:/3ds/balatro_c/debug.txt` (same keys as the desktop env vars without the
`BALATRO_` prefix): `SHOTS`, `QUIT_AFTER`, `SCRIPT`, `TEST_SEED`, `TEST_EASY`, `FPS`, plus `TEST_LOADOUT=1` (5 negative
random Tarot/Planet cards and 5 random jokers when a run first reaches blind select) and `TEST_STRAIGHT=1`
(the first hand's five leftmost cards become a 5-9 straight of foil Mult cards) and `TEST_BIGCARD=1` (the
first hand's leftmost card gets a permanent chip bonus making it worth 10^6 chips; it unlocks the score
achievements in whatever profile it is used with). `TEST_BIGCARD_EXP=n` makes that card worth 10^n chips
instead, and `TEST_NANEINF=1` gives it 10^308 chips plus the Mult enhancement so the score overflows to
infinity and shows "naneinf" as in the original (a save then holds `inf`, which loads as nil, also as in
the original). `TEST_NOPE=1` makes The Wheel the boss of every ante, makes The Wheel of Fortune always fail
("Nope!") and hands out a Wheel of Fortune plus a Joker for it to target when a boss round starts. While that file exists,
stderr goes to `log.txt` next to it, and screenshots (`shot_NN.png`, read back from the 400x240 target)
are written there too. `SCRIPT` also accepts `t:button=name` (LOVE gamepad names: a, b, x, y, dpup,
leftshoulder, start...). FPS lines include heap and free linear memory.

The rest of this file describes the desktop port that this is based on.

## Building and running
```
cmake -S src -B build -G Ninja      # from balatro_c/
ninja -C build
./build/balatro
```
The build copies `src_lua/resources` and `src_lua/localization` next to the executable; the game loads its
textures, shaders, sounds, fonts and localization from there at runtime.

## What was replaced or left out
- LOVE 11.5 -> raylib 5.5 (`gfx/`): transform stack, quads, canvases, stb_truetype fonts, raylib audio. Shaders are
  the original `resources/shaders/*.fs` files, wrapped from LOVE GLSL into GLSL 330 at load time.
  Differences that mattered: backface culling is disabled, and the vertex wrapper forces clip z = 0 (raylib's
  depth counter otherwise clips the card tilt effect, which changes w).
- Sound and save threads run on the main thread.
- Steam (stats, achievements upload), the HTTP high score client, crash reporter, demo/CTA screens, the dev sandbox
  and `test_functions.lua` are not ported. Achievements are tracked locally in settings.
- The video settings screen is simplified (window mode, display, resolution and vsync go straight to raylib).

## Debug / test hooks (environment variables, see main.c)
- `BALATRO_SHOTS="t1,t2,..."`   save `shot_NN.png` in the working directory at those times (seconds)
- `BALATRO_QUIT_AFTER=t`         exit after t seconds (pending saves are flushed)
- `BALATRO_SCRIPT="t:click=x,y;t:move=x,y;t:key=name;..."`  scripted input (the real mouse is ignored while it runs)
- `BALATRO_TEST_SEED=n`          fixed seed for the startup RNG
- `BALATRO_TEST_EASY=1`          every blind needs 1 chip (for reaching shops and later antes quickly)
- `BALATRO_FPS=1`                print fps / state / event queue length every second
- Point `XDG_DATA_HOME` at a scratch directory to keep test saves out of the real profile.
