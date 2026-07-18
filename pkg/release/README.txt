======================================================================
 RAPTOR: CALL OF THE SHADOWS  -  ENHANCED
 Native Windows engine build  (drop-in for your own copy)
======================================================================

This is a build of the open-source Raptor engine port. It runs the
1994 game natively on modern Windows - no DOSBox - with the original
look, speed and feel, and adds support for the optional community-made
DELTA SECTOR campaign (see below).

It runs on YOUR existing game data. It contains no game files and it
never modifies yours - it only reads them.


----------------------------------------------------------------------
 WHAT YOU NEED
----------------------------------------------------------------------
An installed copy of Raptor (1994 / v1.2 or later) - the Steam, GOG,
or DOS release. Specifically the data files FILE0000.GLB ... FILE0004.GLB
(shareware's FILE0000+FILE0001 also work).


----------------------------------------------------------------------
 INSTALL  (about 30 seconds)
----------------------------------------------------------------------
1. Find your Raptor folder - the one that contains FILE0000.GLB
   (or file0000.glb).
     - Steam: right-click Raptor in your library -> Manage ->
       Browse local files. The classic game is usually under a
       "Raptor - Call of the Shadows" subfolder.
     - GOG/DOS: wherever you installed it.

2. Copy these three files from this package into that folder:
       raptor.exe
       SDL2.dll
       raptorsetup.exe

3. Run raptor.exe.  Done.

Your original launcher (Steam "Play", the DOS EXE, DOSBox) is untouched
and still works. This just adds a second, native way to launch.


----------------------------------------------------------------------
 OPTIONS
----------------------------------------------------------------------
raptorsetup.exe configures sound, music and controls.

To start in fullscreen: open SETUP.INI (created in your Raptor folder
the first time you run raptor.exe) and set, under [Video]:
       fullscreen=1

A ready-made SETUP.INI (fullscreen, sensible defaults) is included in
this package. Copy it into your Raptor folder ONLY if you have not
already configured your own - it would replace your sound and control
settings otherwise.


----------------------------------------------------------------------
 OPTIONAL: DELTA SECTOR - a bonus 4th campaign
----------------------------------------------------------------------
A community-made 4th sector (9 new waves) can be added to YOUR copy.
Because it modifies game data, it is applied to your own files locally
- see DeltaSector-optional\README.txt. It requires the raptor.exe from
this package.


----------------------------------------------------------------------
 CREDITS & LICENSE
----------------------------------------------------------------------
Raptor: Call of the Shadows (c) Scott Host / Mountain King Studios.
Buy the game and support Scott: https://www.mking.com

This build is based on the GPL-2 open-source Raptor engine port by
nukeykt and skynettx (https://github.com/skynettx/raptor) and is
likewise released under the GNU General Public License v2 (see
COPYING.txt).

Because this software is GPL-2, the complete corresponding source code
must be made available to you. See SOURCE.txt.

Built by a Raptor fan with Claude (Anthropic). No game assets or game
code by Scott Host are included in this package.
