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

No other libraries. Image and sound loaders are written by hand.


GAMEPLAY
--------
Clear the level of guards. You start with 100% health and 12 bullets.

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
plays them through its own built-in FM synthesizer, in the spirit of the
AdLib sound cards of the time. There is no sound font: instruments are
picked by General MIDI program family, and channel 10 is a drum kit.
Drop any format 0 or 1 MIDI file in as assets/music/e1m1.mid to change
the music.

If no audio device is found, the game runs without sound.


REQUIREMENTS
------------
  Linux on x86-64
  A GPU with OpenGL 3.3 support

  On Arch Linux:
    pacman -S clang cmake nasm glfw glew openal glm


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

  To hear a MIDI file through the engine's synthesizer without the game:
    ./build/render_midi assets/music/e1m1.mid out.wav


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


DIRECTORY LAYOUT
----------------
  include/    headers
  src/        C++ sources
  src/asm/    NASM assembly routines
  shaders/    GLSL 330 core shaders
  assets/     maps, textures, sprites, sounds, music, palette
  tests/      tests that check the assembly against the C++ code
  tools/      generators for the placeholder art, sounds and music
  lib/        reserved for bundled libraries


MAP FORMAT (PLANNED)
--------------------
Maps are plain text files. Each character is one grid cell:

  #  wall (texture 1)  1-9  wall with texture 1-9 (wall1.pcx .. wall9.pcx)
  .  empty floor       D    door
  S  secret push wall  P    player start (facing east)
  E  enemy             +    health
  a  ammo              b    barrel
  l  hanging lamp

  The outer border must be solid wall (not a door or secret wall) so rays
  always hit something. A door needs walls on exactly two opposite sides;
  that decides which way it slides. Doors close by themselves after a few
  seconds. A secret wall looks like wall texture 1 and slides up to two
  tiles away from the player when pushed.
  Every row must be the same width. The game starts on
  assets/maps/e1m1.txt.

  ##########
  #P.......#
  #..##D##.#
  #..#..E#.#
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
