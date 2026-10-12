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
