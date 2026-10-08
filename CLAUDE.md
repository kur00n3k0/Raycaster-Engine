# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A Wolfenstein 3D / Doom-inspired raycaster FPS engine, written from scratch.
The rendering is done **in software** on the CPU into an 8-bit indexed
framebuffer. OpenGL is used only as a "video card": the framebuffer is uploaded
as a texture every frame and drawn as one fullscreen triangle. Hot inner loops
are written in hand-rolled x86-64 assembly (NASM), everything else in C-style C++.

Purist rules:

- No engine frameworks, no image/audio loader libraries. Loaders are written by hand.
- No ImGui in the game. The map editor (`editor/`) uses Dear ImGui, docking branch,
  vendored in `lib/imgui` (user decision). Never link ImGui into `raycaster`.
- The only external libraries are the ones listed under "Stack".
- The 3D view is never drawn with GL geometry. GL draws exactly one triangle.
- Every ASM routine has a C++ reference implementation with the same signature.

## Stack

| Tool / lib      | Role                                                    |
|-----------------|---------------------------------------------------------|
| Clang (C++17)   | Compiler for all C++ sources                            |
| NASM            | Assembler for `src/asm/*.asm` (x86-64, ELF64)           |
| CMake >= 3.20   | Build system (`enable_language(ASM_NASM)`)              |
| OpenGL 3.3 Core | Present the framebuffer, palette lookup in the shader   |
| GLFW 3          | Window, GL context, input, timing                       |
| GLEW            | GL function loading (`glewExperimental = GL_TRUE`)      |
| OpenAL          | Output for SFX (3D sources) and streamed MIDI music     |
| GLM             | Vector math for the player, camera and game logic       |
| FluidSynth 2    | MIDI synth for the music (General MIDI .sf2 SoundFont)  |
| Dear ImGui      | Map editor UI only (docking branch, `lib/imgui`)        |

GLM is a C++ header library, which is why the "C" side of the project is C++
written in a plain, C-like style (see Conventions). Target platform is Linux
x86-64; the ASM follows the System V AMD64 ABI.

## Build

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/raycaster                    # run from the repo root so assets/ resolves
cmake -B build -DRC_USE_ASM=OFF      # build with the C++ reference routines only
ctest --test-dir build               # ASM vs C++ equivalence tests
./build-release/bench_routines      # ASM vs C++ timing (configure a Release tree first)
```

`RC_USE_ASM` (default ON) selects the NASM routines; OFF links the C++ reference
versions instead. Both must always build and must produce identical output.

## Architecture

```
 GLFW input ──> Game tick (fixed 70 Hz, like Doom's 35 Hz x2)
                    │
                    ▼
 Raycaster (CPU) ──> 8-bit framebuffer (320x200, scalable to 640x400)
   - DDA per column              │
   - wall columns   [ASM]        │  glTexSubImage2D (GL_R8)
   - floor/ceiling  [ASM]        ▼
   - sprites        [ASM]   Fragment shader: index -> palette texture (256x1 RGBA)
                                 │  + palette "flash" tint (damage, pickup)
                                 ▼
                         Fullscreen triangle, letterboxed, nearest filtering
 OpenAL: listener follows the player, one pooled source per active sound.
 Music: .mid -> hand-written SMF parser -> FluidSynth -> streamed buffer queue.
```

Frame flow: `poll input -> fixed-step update(s) -> render to framebuffer -> upload -> present`.

### Planned source layout

```
include/            headers (one per module, include guards: FOO_H); SpriteIds.h is shared
src/
  main.cpp          entry point, main loop
  Window.cpp        GLFW window + GL 3.3 core context + input state    (done)
  GLCheck.cpp       GL_CHECK() error reporting, Debug builds only       (done)
  Video.cpp         framebuffer, GL texture, shader, present, test pattern (done)
  Raycaster.cpp     camera, DDA, z-buffer, wall pass + floor/ceiling span pass
  Sprites.cpp       sprite projection, far-to-near sort, z-buffer clip (done)
  Map.cpp           map loading (plain-text grid), tile queries, door/pushwall data
  Textures.cpp      hand-written PCX loader -> column-major palettized textures (done)
  Palette.cpp       PLAYPAL-style palette + COLORMAP light tables (built-in palette)
  Audio.cpp         OpenAL device/context, WAV loader, SFX source pool, music stream
  Midi.cpp          Standard MIDI File parser, tempo map, looping MusicPlayer
  Synth.cpp         FluidSynth wrapper: SoundFont search, MIDI messages, s16 render
  Game.cpp          player, collision, doors, pushwalls, pickups, enemy AI, hitscan
  Hud.cpp           status bar, 5x7 bitmap font, first-person weapon, messages, death screen
  Config.cpp        raycaster.cfg: video/audio settings and key bindings
  Reference.cpp     C++ reference versions of every ASM routine (`*_ref`)
  asm/
    draw_column.asm     textured vertical wall span (16.16 fixed-point step) (done)
    draw_span.asm       textured horizontal floor/ceiling span (done)
    draw_sprite_col.asm masked sprite column (skip transparent index 0xFF) (done)
    fb_clear.asm        fast framebuffer fill (done)
shaders/  present.vert, present.frag
assets/   maps/*.txt, textures/*.pcx, sprites/*.pcx, sounds/*.wav (SFX), music/*.mid, palette.pal
tests/    asm_equivalence.cpp
tools/    gen_textures.cpp (placeholder textures + sprites), gen_sounds.cpp (SFX .wav + songs, `SONGS[]`),
          bench_routines.cpp (ASM vs C++ timing),
          render_midi.cpp (render a .mid through FluidSynth to .wav for offline checks)
editor/   map_editor: MapDoc.cpp (char grid + @settings, load/save, undo, validation), Editor.cpp (ImGui UI)
lib/      vendored deps: imgui/ (Dear ImGui v1.92.9b-docking + GLFW/OpenGL3 backends, see VERSION.txt)
```

### Key design decisions

- **Column-major texture storage.** Textures are stored transposed (column after
  column) so the wall drawer walks memory linearly, exactly like Wolf3D.
- **Fixed point in the inner loops.** World/player math is float (`glm::vec2`);
  texture stepping inside the ASM routines is 16.16 fixed point.
- **8-bit indexed color + light tables.** Distance shading uses 32 precomputed
  colormaps (`colormap[light][index]`), so shading is a table lookup, not math.
- **Palette in the shader.** The framebuffer is uploaded as `GL_R8`; the
  fragment shader looks up a 256x1 palette texture. Damage/pickup flashes are a
  palette swap, not a framebuffer pass.
- **Fixed timestep** game logic; rendering is capped at 60 FPS (`FRAME_RATE` in main,
  sleep + short spin), with or without vsync.
- **Non-square pixels.** 320x200 is shown at 4:3 like on a VGA monitor (each pixel is
  1.2x taller than wide). The letterbox math in `Video.cpp` uses 4:3, not 16:10.
- **Palette index 255 is reserved** as the transparent key for sprites. Loaders remap
  any opaque pixel that lands on 255 to the closest other entry.
- **Framebuffer is row-major** (`fb[y * pitch + x]`), so it can go straight to
  `glTexSubImage2D`. Column drawers step by `pitch`. Set `GL_UNPACK_ALIGNMENT` to 1.

## ASM <-> C++ interface

- All ASM entry points are declared in `include/AsmRoutines.h` inside `extern "C" { }`.
- Naming: the ASM symbol is `rc_draw_column`, its C++ reference in `Reference.cpp` is
  `rc_draw_column_ref` with an identical parameter list. Both are always linked into the
  test binary so they can be compared. Engine code never calls either directly: it calls
  `RC_draw_column(...)`, a macro in `AsmRoutines.h` that resolves to the ASM or `_ref`
  version depending on `RC_USE_ASM`.
- `tests/asm_equivalence.cpp` runs each pair on the same random and edge-case inputs
  (zero height, clipped at top and bottom, texture coordinates that wrap) and `memcmp`s the
  framebuffers. It is built only when `RC_USE_ASM=ON`.
- ABI: System V AMD64. Args in `rdi, rsi, rdx, rcx, r8, r9`; preserve `rbx, rbp, r12-r15`.
  Keep the stack 16-byte aligned if calling anything (ASM routines should be leaf functions).
- Pass a single `const struct` pointer when a routine needs more than 6 arguments.
  The struct layout is mirrored in a NASM `struc` block, with a `static_assert` on
  `offsetof` in C++ for every field, so the two never drift.
- Each `.asm` file starts with `default rel`, `section .text`, `global <name>`
  and a header comment listing arguments, clobbers and the C++ reference function.
- Plain x86-64 + SSE2 only (baseline for every x86-64 CPU). No AVX unless guarded.
- Add `section .note.GNU-stack noalloc noexec nowrite progbits` to every file.

## Conventions

- C-style C++17: structs and free functions preferred, classes only where they
  own a resource (e.g. `Window`). No exceptions, no RTTI, no STL in the render
  loop (std::vector fine for loading). No `new` in the frame loop.
- Indentation: **tabs**. Headers use `#ifndef FOO_H / #define FOO_H` guards.
- File names: `PascalCase.cpp/.h` for C++ modules, `snake_case.asm` for assembly.
- Fixed-width types (`uint8_t`, `int32_t`) for anything shared with ASM.
- Every GL call path checks errors in Debug builds (`glGetError` macro).
- Compile with `-Wall -Wextra -Wpedantic`; keep it warning-free.

## Roadmap

1. **Skeleton** - CMakeLists with NASM enabled, GLFW window, GL 3.3 core context,
   clear screen, ESC quits.
2. **Video** - 320x200 GL_R8 framebuffer, palette texture, fullscreen triangle,
   aspect-correct letterbox. Test pattern on screen.
3. **Flat raycaster** - text map, player movement, DDA, flat-shaded walls with
   side darkening (all in C++ first).
4. **Textured walls** - PCX loader, column-major textures, `draw_column` in C++
   then ASM, equivalence test.
5. **Floors & ceilings** - per-row floor casting, `draw_span` in ASM, colormap shading.
6. **Sprites** - billboards, z-buffer per column, sorting, `draw_sprite_col` ASM.
7. **Doors & pushwalls** - Wolf3D-style sliding doors and secret walls.
8. **Audio** - OpenAL init, WAV loader, positional SFX, looping music.
9. **Game** - collision, pickups, enemies with simple state machines, hitscan weapon, HUD.
10. **Polish** - palette flashes, config file (keyboard rebinding), profiling.
    The game is keyboard only by user decision: no mouse look, no mouse buttons.
    of ASM vs C++ paths.

Do the C++ reference version of a routine first, get it on screen, then write the ASM
and prove equivalence with a test before switching it on.

## Notes for Claude

- All 10 roadmap steps are done. CMake builds `rc_core` (reference + ASM), `raycaster`,
  `gen_textures`, `gen_sounds`, `render_midi`, `imgui` + `map_editor`, and with ASM on `asm_equivalence` (ctest) and
  `bench_routines`. ASM routines: `fb_clear`, `draw_column`, `draw_span`, `draw_sprite_col`.
- Keyboard only (user decision): no mouse input anywhere. Input goes through `Config::keys`
  (`action_down` in main); add new actions to `Action`, `ACTION_NAMES` and the defaults.
- Palette flashes follow Doom's ST_doPaletteStuff: `Player::damageFlash` / `bonusFlash`
  (+damage, +6 per pickup, fade 35/s) -> `game_flash_palette` -> one of
  `palette_build_flashes` (0 normal, 1-8 red, 9-12 gold), uploaded only when it changes.
- Release numbers on this machine (`bench_routines`, ns per pixel, C++ ref -> ASM):
  fb_clear 0.007 -> 0.008 (memset is already optimal), draw_column 0.82 -> 0.47 (1.8x),
  draw_span 0.99 -> 0.66 (1.5x), draw_sprite_col 0.90 -> 0.55 (1.6x).
  Whole frame in game (Release, F1 line): ASM 0.08 ms vs C++ 0.09 ms at 320x200; the DDA,
  frame setup and GL upload dominate, so the inner-loop gains shrink to ~10%. Frame rate is
  capped at 60 (user requirement), so compare draw times, never FPS. With the cap the CPU
  idles and clocks down, which roughly doubles the F1 draw time (~0.15 ms): for ASM vs C++
  numbers use `bench_routines`, which runs flat out.
- Gameplay lives in `Game.cpp`: entities carry their own `sprite` (a `SpriteId` from
  `SpriteIds.h`) chosen by game logic; the renderer only draws `active` entities.
  Enemy states: IDLE -> CHASE -> AIM -> SHOOT -> CHASE, plus PAIN and DEAD. Enemies see
  by grid DDA line of sight (`trace_distance`; doors at least half open let sight and
  bullets through), hear shots within `ENEMY_HEARING` steps, and walk down a BFS distance
  field from the player (`update_paths`, every tick), opening doors in the way.
- All randomness goes through `Game::rng` (xorshift, fixed seed) so ticks stay
  deterministic for a given input stream.
- The 3D view is `Raycaster::height` rows (framebuffer height minus `HUD_BAR_HEIGHT * scale`);
  the HUD draws the status bar below it and the weapon over it.
- `static Game game;` in main: `Game` is large (entity array), keep it off the stack.
- Audio: music is MIDI (user decision): `.mid` -> `midi_load` (format 0/1, tempo map, events
  in sample time) -> `Synth` (FluidSynth, user decision; replaced the old hand-written FM synth)
  -> 4 x 2048-frame stereo buffers queued on one relative source, refilled in `audio_update`
  each frame. SFX stay as mono 16-bit WAVs (OpenAL only spatialises mono). New sounds go in
  `Sfx` + `SFX_PATHS` (a static_assert checks the count) + `gen_sounds`.
- FluidSynth is used only as a renderer: no FluidSynth audio driver, no `fluid_player` (our SMF
  parser stays, per the hand-written-loaders rule). `synth_init` registers only the "file" driver
  before `new_fluid_settings()`, otherwise ALSA/SDL probing spams the console. SoundFont: config
  `soundfont`, else first `.sf2` in assets/music, FluidSynth's `synth.default-soundfont`,
  /usr/share/soundfonts, /usr/share/sounds/sf2. None found = warning, game runs without music.
  `SYNTH_GAIN` 0.9 matches the old FM synth's loudness (render_midi: ~-21.5 dB RMS, -3.4 dB peak).
- Game code never calls audio: it queues `SoundEvent`s in `Game::sounds`; `main` plays and
  clears them after the ticks. Map (x, y) is OpenAL (x, 0, y), listener up = +y.
- No device = silent run (`Audio::enabled` false). Check audio without speakers or recording
  the user's system: `ALSOFT_CONF` with `[general] drivers = wave` and `[wave] file = out.wav`
  makes OpenAL Soft write the game's exact mix to a file.
- Doors: `Map::doors` + per-cell `Map::doorIndex`. Panel on the cell mid-plane, slides by
  `open` (0..1); rays pass through the open part. Walls hit from inside a door cell use
  `WallTextures::doorJamb`. Doors block movement until `open == 1` (`map_blocks`), never
  close while the player overlaps the cell, and auto-close after `DOOR_OPEN_TIME`.
- Pushwall: one at a time (`Map::pushwall`), a 1x1 box covering two `TILE_PUSHWALL` cells
  while moving; rays use a slab test clipped to the current cell. It rests as `TILE_WALL_FIRST`.
- Use (Space) is edge-triggered in `game_tick` and targets the cell ahead along the dominant
  view axis (Wolf3D style).
- Sprites: map characters become `Thing`s (`Map::things`), copied into `Game::entities` at
  start. Each is a 1x1 unit billboard, feet on the floor, indexed by `ThingType` into
  `SpriteTextures`. `draw_sprite_col` reuses `ColumnArgs`. Clipping is whole-column against
  `Raycaster::zbuffer`, which is exact because every wall is full height.
- Render order: `draw_walls` (one ray per column, records `wallTop`/`wallBottom`), then
  `draw_flats` (per row, spans over the column runs no wall covers; no overdraw).
- Lighting: level = distance * `LIGHT_PER_UNIT` (capped at `LIGHT_MAX`), +`LIGHT_SIDE_Y` for
  north/south faces. One colormap per wall column and per floor/ceiling span.
- `draw_span` uses callee-saved rbx/rbp/r12/r13 (push/pop) because it needs 13 registers
  and the shift count has to be in cl.
- Renderer inputs that it does not own go in `RenderAssets` (walls, flats, sprites, colormaps).
- Frame: `raycaster_render` (walls, then flats) then `sprites_render`, same camera.
- `ColumnArgs` in `AsmRoutines.h` is the template for struct-passing routines: C++ struct with
  `static_assert(offsetof(...))` per field, NASM `struc` mirror at the top of the `.asm` file.
- Textures: `Texture::columns[u * height + v]`, power-of-two sizes. Wall texture u is flipped
  for x-side hits with `ray.x < 0` and y-side hits with `ray.y > 0` so nothing is mirrored.
- PCX files use the default palette; the loader still remaps any other palette to the nearest
  colours. With `transparent`, pure magenta (255,0,255) becomes index 255 (for sprites).
- `./build/raycaster [-config <path>] [-map <path>] [-warp <x> <y> <degrees>] [-nomusic]`: `-warp` starts at a fixed spot
  for deterministic screenshots, `-map` loads test maps (`assets/maps/test_pushwall.txt`,
  `test_arena.txt`).
  Diff the `RC_USE_ASM=ON` and `OFF` builds (`magick compare -metric AE`), which
  must be 0 pixels apart.
- World coordinates: x east, y south (map row 0 is north). Angle 0 = east, positive turns
  clockwise on the map (towards +y). Camera right = (-dir.y, dir.x).
- Projection: `focalY = focalX / pixelAspect` (1.2 at 320x200) so walls are not stretched
  by the 4:3 display. A 1-unit wall at distance d is `focalY / d` rows tall.
- Doors (`D`) and secret walls (`S`) are plain solid tiles until step 7.
- `main.cpp` reads `Input` from the window once per frame and runs `game_tick` at a fixed
  70 Hz; `game_tick` must stay deterministic given the same `Input`.
- Default palette: index = ramp * 16 + shade (ramp 0 gray, 1-15 hues, shade 15 brightest).
- Verify visuals by screenshot: run the game in the background, find the window with
  `xdotool search --name "^Raycaster$"`, capture it with `import -window <id> out.png`.
  Drive it with `xdotool keydown/keyup --window <id> <key>`.
- Add new ASM files to the `RC_USE_ASM` block of `CMakeLists.txt` and a test to
  `tests/asm_equivalence.cpp` in the same change.
- Keep to the stack above; do not add dependencies without asking.
- When touching an ASM routine, update its C++ reference and the struct mirrors together.
- Map editor (`map_editor`, user request): ImGui docking, top-down 2D only. The document is the
  map's own character grid (`MapDoc`), so saving writes exactly the game's format and keeps
  `#` vs `1`. `doc_validate` mirrors `map_load` (border, one P, door walls, door/thing limits) and
  adds warnings (unreachable things via flood fill from P, no guards); if `map_load` gains a
  rule, add it to `doc_validate` too. Every user action is wrapped in `doc_begin_edit` /
  `doc_end_edit` (snapshot undo, no-op edits dropped). Brushes and tools are tables at the top
  of `Editor.cpp`; single-character keys use the map characters. F5 saves and `posix_spawn`s
  `raycaster -map` from the editor's own directory; F6 adds `-warp` at the hovered cell. Default
  dock layout via `DockBuilder` when `map_editor.ini` has none. The editor uses the mouse; the
  keyboard-only rule is for the game. Verify by screenshots with xdotool as for the game
  (window name ends in "Raycaster Map Editor"); don't `pkill -f` a pattern that matches your own shell.
- Per-map music (user request): `@name value` lines before the grid are map settings
  (`apply_setting` in Map.cpp; unknown name or a setting after the grid = load error).
  `@music <file.mid>|none` -> `Map::music` ("" = silence), default `MAP_MUSIC_DEFAULT`
  (e1m1.mid) when absent. main starts the song after `game_init`; a missing file only
  disables music. Map error messages use real file lines (`rowLine`), not grid rows.
  `MapDoc::music` keeps the raw value ("" = no line written), is part of undo snapshots, and
  `doc_validate` checks it (bad name = error, missing file = warning). The editor previews
  through the game's own Audio/Midi/Synth code, using `raycaster.cfg`'s SoundFont and volume.
  New settings: add to `apply_setting`, `doc_load`/`doc_save`, `DocState` and the README.
- See `README.txt` for the user-facing description.
