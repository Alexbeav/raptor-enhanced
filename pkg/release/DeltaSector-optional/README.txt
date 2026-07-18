======================================================================
 DELTA SECTOR  -  optional bonus 4th campaign
======================================================================

Adds a selectable 4th sector (9 new waves) to Raptor, remixed from the
game's own three campaigns - lava, ocean and city terrain woven into
new levels, with enemies drawn from across the game.

IMPORTANT
  * This modifies YOUR game data files (FILE0001.GLB and FILE0004.GLB).
    The installer backs them up first (.bak) - keep those backups.
  * It requires the Enhanced raptor.exe from this package (the new
    engine adds the 4th-sector support). The original DOS/Steam
    executable will simply ignore the extra maps.
  * It needs the FULL game (FILE0001 through FILE0004), not shareware.


HOW TO INSTALL
  Windows, easiest:
    1. Copy this whole "DeltaSector-optional" folder into your Raptor
       folder (next to FILE0000.GLB).
    2. Double-click  install_delta_sector.bat
       (If Windows asks, allow it - it just runs the Python script.)

  Any system, from a terminal:
    python install_delta_sector.py  "path\to\your\Raptor folder"
    (run with no path if you copied it into the Raptor folder first)

  Requires Python 3.10 or newer: https://www.python.org/downloads/


HOW TO PLAY IT
  Launch raptor.exe, start or load a pilot, go to the hangar and open
  the Ship Computer (the sector-select screen). Pick DELTA SECTOR
  (or press the D key).


HOW TO UNDO
  Delete FILE0001.GLB and FILE0004.GLB and rename the .bak files back
  (remove the ".bak"). Delta Sector is gone, everything else intact.


This installer contains no game data. It only transforms the files you
already own on your own machine.
