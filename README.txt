================================================================================
                              RAYCASTER ENGINE
          A Wolfenstein 3D / Doom style raycaster, built from scratch
================================================================================

ABOUT
-----
A first person shooter engine in the old style. The 3D view is drawn by
the CPU into an 8-bit, 256 color framebuffer, one column at a time, the way
Wolfenstein 3D drew it in 1992. The hottest loops are hand-written x86-64
assembly. Everything else is plain, C-like C++.

OpenGL does almost nothing here. Every frame the framebuffer is uploaded to a
texture and drawn as a single fullscreen triangle. The fragment shader turns
each palette index into a color.


STACK
-----
  Clang ............ C++17 compiler
  NASM ............. x86-64 assembler for the inner loops
  CMake ............ build system
  OpenGL 3.3 Core .. presents the framebuffer
  GLFW ............. window, context, keyboard
  GLEW ............. OpenGL function loader
  OpenAL ........... sound output
  GLM .............. vector math
  FluidSynth ....... MIDI synthesizer for the music (SoundFont based)
  Dear ImGui ....... user interface of the map editor only (bundled in lib/)

No other libraries. Image, sound and MIDI file loaders are written by hand.
The game itself does not use ImGui; only the map editor does.


GAMEPLAY
--------
Find the exit door (the steel door with the green EXIT sign) and press
Space in front of it to finish the floor. The guards are in your way.
You start with 100% health and 12 bullets.

  Episode 1 has two floors: E1M1 and E1M2. After each floor a tally shows
  how many guards you killed, secrets you found and items you picked up,
  and how long it took. Health and ammo carry over to the next floor.
  After the last floor, Space starts the episode again.

  Guards stand still until they see you or hear a shot nearby. Then they
  hunt you down, find their way around walls and open doors on their own,
  stop to aim and fire. The closer they are, the more often they hit.
  A dead guard drops a few bullets.

  Medkits (+25%) and ammo boxes (+8) are picked up by walking over them,
  but only if you need them. Barrels and guards block your way.

  The status bar shows health, guards killed out of the total, and ammo.
  The screen flashes red when you are hit and gold when you pick
  something up, like Doom: the palette itself is swapped.
  If your health reaches 0%, press Space to restart the level.


SOUND AND MUSIC
---------------
Sound effects are short WAV files played as 3D sources: a door to your
right is heard on the right, and far away sounds fade out.

Music is MIDI, like Doom. The engine reads standard .mid files itself and
sends the notes to FluidSynth, which plays them with a General MIDI
SoundFont (.sf2), the way a Sound Canvas or a wavetable card would have.
The rendered audio is streamed to OpenAL. Each map picks its own song
with an @music line (see MAP FORMAT); any format 0 or 1 MIDI file dropped
into assets/music/ can be used. Two songs come with the engine:
e1m1.mid (fast, A minor, the default) and e1m2.mid (slow, D minor).

The SoundFont is not included. Set one with "soundfont =" in
raycaster.cfg, or leave it empty and the game uses the first .sf2 it
finds in assets/music, then FluidSynth's default SoundFont, then
/usr/share/soundfonts and /usr/share/sounds/sf2. Without one the game
runs without music.

If no audio device is found, the game runs without sound.


REQUIREMENTS
------------
  Linux on x86-64
  A GPU with OpenGL 3.3 support

  On Arch Linux:
    pacman -S clang cmake nasm glfw glew openal glm fluidsynth soundfont-fluid


BUILDING
--------
  cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++
  cmake --build build -j

  To build without assembly (portable C++ reference code only):
    cmake -S . -B build -DRC_USE_ASM=OFF

  To run the tests that check the assembly against the C++ code:
    ctest --test-dir build


RUNNING
-------
  Run from the project root so the engine can find the assets folder:
    ./build/raycaster

  Start at a given spot (map units, angle in degrees, 0 = east, 90 = south):
    ./build/raycaster -warp 5.5 9.5 0

  Load another map:
    ./build/raycaster -map assets/maps/test_pushwall.txt
    ./build/raycaster -map assets/maps/test_arena.txt

  Play without music:
    ./build/raycaster -nomusic

  The textures in assets/textures and sprites in assets/sprites are
  generated. To regenerate them:
    ./build/gen_textures

  The sound effects and the music are generated too:
    ./build/gen_sounds

  To hear a MIDI file through the game's music path without the game:
    ./build/render_midi assets/music/e1m1.mid out.wav [seconds [soundfont.sf2]]


MAP EDITOR
----------
A top-down map editor comes with the engine. Run it from the project root:
    ./build/map_editor                          start a new 32x32 map
    ./build/map_editor assets/maps/e1m1.txt     edit an existing map

The window is split into dockable panels (drag a tab to move it; View >
Reset window layout puts them back):
  Palette ...... tools and brushes, shown with the game's own textures
  Map .......... the grid you draw on
  Properties ... file, size, music, what the map contains, view options, test
  Problems ..... everything the game would reject, live; click to jump

Mouse:
  Left button ............ paint with the current tool and brush
  Right button ........... the same tool, but paints floor (erases)
  Alt + left button ...... pick the brush from a cell
  Middle button drag ..... pan (or hold Space and drag)
  Wheel .................. zoom around the cursor

Tools:
  Q  Pencil   paint cells one by one, or drag
  W  Line     drag a straight line
  R  Room     drag a rectangle outline: four walls in one go
  F  Box      drag a filled rectangle
  G  Fill     flood fill an area of identical cells
  I  Pick     take the brush from a cell

Brushes (the same characters as the map file):
  1-9 walls, D door, X exit, S secret wall, . floor, P player start,
  E guard, + medkit, a ammo, b barrel, l lamp

Keys:
  Ctrl+N / Ctrl+O / Ctrl+S   new / open / save   (Ctrl+Shift+S: save as)
  Ctrl+Z / Ctrl+Y            undo / redo
  F5                         save and play the map in the game
  F6                         play from the cell under the mouse
  Home                       fit the map in the view
  T / H                      textures / grid on or off
  Ctrl+Q                     quit (asks before losing changes)

Next level: pick the map that follows this one's exit in Properties
(None = last level). Problems warns when a map has no exit, the exit
cannot be reached, or the next map is missing.

Music: pick the map's song in Properties (Default, None or any .mid in
assets/music) and press Preview to hear it with the game's SoundFont. It is
saved as the @music line. Undo covers it like any other change.

Doors are drawn as a thin panel showing which way they face. Cells with
problems get a red (error) or orange (warning) outline. "Shade unreachable
floor" shows what the player can never walk to. Panel positions are kept
in map_editor.ini.


CONTROLS
--------
The game is played with the keyboard only. These are the default keys;
every one of them can be changed in raycaster.cfg (see CONFIGURATION).

  W / S, Up / Down . move forward / back
  A / D ............ strafe left / right
  , / . ............ turn left / right
  Shift ............ run
  Space ............ open / close the door ahead, push a secret wall,
                     restart after dying
  Ctrl ............. fire (hold to keep firing)
  F1 ............... show render time and frames per second
  Esc .............. quit (cannot be rebound)


CONFIGURATION
-------------
Settings live in raycaster.cfg in the folder you run the game from. If
it does not exist, the game writes one with the defaults on start. It
is plain text, one "name = value" per line:

  scale = 1               framebuffer 320x200 (1) or 640x400 (2)
  window_width = 960      window size, ignored when fullscreen
  window_height = 720
  fullscreen = no         borderless, at the desktop resolution
  vsync = yes             the frame rate is capped at 60 either way
  fov = 70                horizontal field of view, 50 - 110 degrees
  sfx_volume = 1          0 - 1
  music_volume = 1        0 - 1
  music = yes
  soundfont =             .sf2 for the music; empty = search for one
  key_forward = W UP      up to two keys per action

Mistakes are reported with their line number and that setting keeps its
default. Delete the file to start over. Use another file with:
    ./build/raycaster -config my.cfg


FEATURES (ROADMAP)
------------------
  [x] 1.  Window and OpenGL 3.3 core context
  [x] 2.  320x200 8-bit framebuffer, palette shader, letterboxing
  [x] 3.  Flat-shaded walls using the DDA grid raycast
  [x] 4.  Textured walls (assembly column drawer)
  [x] 5.  Textured floors and ceilings with distance shading
  [x] 6.  Sprites: items, decorations, enemies
  [x] 7.  Sliding doors and secret push walls
  [x] 8.  Sound effects and MIDI music through OpenAL
  [x] 9.  Gameplay: collision, pickups, enemies, weapons, HUD
  [x] 10. Polish: palette flashes, config file, profiling
  [x] Map editor: top-down, docked ImGui panels, draw walls, place
      guards and items, live checks, play test with F5
  [x] Objective: exit door, floor complete tally, next level (@next),
      episode of two floors


DIRECTORY LAYOUT
----------------
  include/    headers
  src/        C++ sources
  src/asm/    NASM assembly routines
  shaders/    GLSL 330 core shaders
  assets/     maps, textures, sprites, sounds, music, palette
  tests/      tests that check the assembly against the C++ code
  tools/      generators for the placeholder art, sounds and music
  editor/     the map editor (MapDoc: map model, Editor: ImGui UI)
  lib/        bundled libraries (lib/imgui: Dear ImGui, docking branch)


MAP FORMAT
----------
Maps are plain text files. Each character is one grid cell:

  #  wall (texture 1)  1-9  wall with texture 1-9 (wall1.pcx .. wall9.pcx)
  .  empty floor       D    door
  S  secret push wall  P    player start (facing east)
  E  enemy             +    health
  a  ammo              b    barrel
  l  hanging lamp     X    exit door (use it to finish the level)

  The outer border must be solid wall (not a door or secret wall) so rays
  always hit something. A door needs walls on exactly two opposite sides;
  that decides which way it slides. Doors close by themselves after a few
  seconds. A secret wall looks like wall texture 1 and slides up to two
  tiles away from the player when pushed. The exit door is solid like a
  wall and can sit in the border; press Space while facing it.
  Every row must be the same width. The game starts on
  assets/maps/e1m1.txt.

  Settings go on lines starting with @, before the grid:

    @music e1m2.mid     song from assets/music/, or "none" for silence;
                        without the line the map plays e1m1.mid
    @next e1m2.txt      map loaded after this one's exit, from the same
                        folder; without it this is the last map

  An unknown setting, or one after the grid, is an error.

  @music e1m2.mid
  @next e1m3.txt
  ##########
  #P.......#
  #..##D##.#
  #..#..E#.X
  #..S.+.#.#
  ##########


PERFORMANCE
-----------
Every assembly routine has a C++ twin. To time them against each other,
build a Release tree and run the benchmark:

    cmake -S . -B build-release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release -j
    ./build-release/bench_routines

In the game, F1 shows how long the CPU takes to draw a frame and which
path (ASM or C++) is running. The average is printed when you quit.
The game never runs faster than 60 frames per second, so compare the
draw time, not the frame rate.


CREDITS
-------
Inspired by id Software's Wolfenstein 3D (1992) and Doom (1993), and by
Fabien Sanglard's Game Engine Black Books.

================================================================================
