# Modding Raptor Reclawed

## Two different tools — pick the right one first

Most confusion here comes from there being *two* separate modding systems
that do not talk to each other:

| | **Engine mods** (this document) | **[Raptor Map Editor](https://github.com/Alexbeav/raptor-map-editor)** |
|---|---|---|
| What it changes | art, sounds, the player's gun, item aliases | levels, enemy stats, flight paths, art, music |
| How it ships | a `.glb` file in `mods\` | patched copies of `FILE0000`–`FILE0004.GLB`, or a `.rapmod` patch |
| Toggleable in-game | yes, from the MODS menu | no — it edits your data files |
| Needs | a text editor (+ Python only to pack) | a browser, nothing else |

The map editor **cannot build engine mods**, and engine mods cannot change
level layouts or enemy stats. If you want to retune an enemy or repaint a
map, stop reading and go use the editor. If you want something that shows
up in the MODS menu with a switch next to it, keep going.

## What an engine mod is

A `.GLB` archive dropped into a `mods\` folder next to the game data. The
main menu's **MODS** entry (or `M`) lists them; toggles save to `SETUP.INI`
and apply instantly at the main menu (mid-mission toggles apply when the
mission ends). Up to 8 mods load at once, in filename order — later files
win conflicts.

A mod can do four things:

1. **Replace art** (or any item): an item whose name matches a base game
   item overrides it everywhere — menus, HUD, sprites. Multi-frame art
   works: the *k*-th same-named item in your mod replaces the *k*-th in the
   game (the player ship, for example, is 7 items all named `LPLAYER_PIC`).
2. **Add new items**: names the game doesn't have become available
   engine-wide — this is how an add-on campaign's maps would ship.
3. **Alias** existing game items to each other, via the manifest — no data
   copied at all. `NightOps.glb` is 84 bytes: it points every `LPLAYER_PIC`
   (day player ship) at `DPLAYER_PIC` (night ship) and nothing else.
4. **Retune the player's forward cannon** and airframe, via a
   `PLAYRGUN_TXT` item — see below.

## Your first mod, in about two minutes

No images, no dependencies — just Python 3.10+ as it installs.

**1.** Make a folder called `MyGun` with two text files in it.

`modinfo.txt` — line 1 is the name shown in the MODS menu, line 2 the
description:

```
My First Gun
One fat centered barrel that hits like a truck.
```

`PLAYRGUN_TXT.txt` — the gun itself:

```
RATE 1
DAMAGE 4
MUZZLE 0 0
```

**2.** Pack it:

```
python tools/make_mod.py MyGun -o MyGun.glb
```

**3.** Drop `MyGun.glb` into the game's `mods\` folder, start the game, open
**MODS**, switch it on. That's the whole loop.

> Any `NAME.txt` in the folder becomes an item called `NAME` — that's why
> the file is `PLAYRGUN_TXT.txt` and not `playrgun.txt`. `modinfo.txt` is
> the one exception; it always becomes `MODINFO_TXT`.

## The manifest

`modinfo.txt` becomes an item named `MODINFO_TXT`:

```
My Mod Name
One line describing it.
ALIAS LPLAYER_PIC DPLAYER_PIC
```

Line 1 is the menu display name, line 2 a description; every `ALIAS A B`
line remaps all occurrences of base item A to base item B.

## Player gun config

A `PLAYRGUN_TXT` item redefines the standard forward cannon and some
airframe properties:

| Line | Range | Stock | Effect |
|---|---|---|---|
| `RATE n` | 1–35 | 2 | ticks between volleys; lower is faster |
| `DAMAGE n` | 1–50 | 1 | damage per shot |
| `SPEED n` | 25–200 | 100 | movement speed, percent |
| `ARMOR n` | 50–400 | 100 | effective hitpoints, percent (incoming damage divided by `ARMOR/100`, remainders carried) |
| `MUZZLE x y` | x ±160, y ±100 | see below | adds a barrel at that offset from the player's center; up to 8 lines |
| `EXHAUST x y` | x 0–16, y ±16 | `3 15` | engine flame spread from center, and vertical offset |

The stock guns are not at a fixed offset: they sit at ±`o_gun1[frame]`
(`rap.cpp:176` — `1, 3, 5, 6, 5, 3, 1, 0`), so the barrels are 6 apart with
the wings level and converge toward the nose as the ship banks. `MUZZLE`
offsets are fixed and do not bank, which is why a modded gun feels more
rigid than the stock pair.

Only the first muzzle spawns the muzzle-flash animation, so stacked barrels
read as one gun. Omit the item, or give it no `MUZZLE` lines, and the stock
twin guns are untouched. Changes hot-apply with the mod toggle.

The shipped `BulletHose.glb` is a worked example: one centered barrel,
maximum cyclic rate, double damage.

**The special weapons are not moddable.** Plasma, micro and dumb missiles,
the minigun, turret, missile pods, bombs, the pulse cannon, forward laser
and death ray are all defined in `shot_lib[]` in `src/shots.cpp` and can
only be changed by editing and rebuilding the engine. `PLAYRGUN_TXT`
reaches the forward cannon only.

## Building a mod with art

```
python tools/make_mod.py my-mod-folder --game "C:\path\to\Raptor" -o MyMod.glb
```

Put `modinfo.txt` plus images named after the items they replace
(`CURSOR_PIC.png`; for multi-frame items `LPLAYER_PIC.png`,
`LPLAYER_PIC.2.png`, …) in the folder. Images are quantized to the game
palette (alpha < 128 = transparent) and encoded to match the base item's
format automatically. `--game` is only needed when the folder contains
images — that is where the palette is read from.

**Pillow (`pip install pillow`) is required only for `.png` packing.**
Text-only and sound-only mods build with a stock Python install. See
`tools/demo-mod/` — the source of the shipped `DemoCursor.glb`.

Item names come from the game's data. To browse them — or to edit maps,
enemies and art, which engine mods cannot do — use the
[Raptor Map Editor](https://github.com/Alexbeav/raptor-map-editor).

## Sounds and unnamed items

Sound effects live in unnamed items following a `*_FX` label; the
digitized sample is always **4 slots after the label**. A mod item named
`GUN_FX+4` therefore replaces the machine-gun sound — `make_mod.py`
converts a WAV for you (name it `GUN_FX+4.wav`; 8/16-bit mono or stereo,
any rate up to 32 kHz). The same `LABEL+K` addressing reaches any
unnamed item, tiles included (`STARTG1TILES+K`).

## Delta Sector recipes

The shipped `DeltaSector.glb` is a fourth kind of zero-data mod: it
carries a `DELTARCP_TXT` item and the engine *synthesizes* the 4th
campaign's nine maps from the player's own base data at startup,
mounting them in memory (nothing on disk changes). Each recipe line is

```
WAVE 1 MAP1G3_MAP MAP1G2_MAP MAP1G1_MAP
```

— the wave number and the three source maps spliced bottom/middle/top
into the new level. All nine waves must be present. Because each wave
inherits its length from the three maps it splices, mission lengths vary
quite a bit; that is the recipe, not a bug. If the maps already exist in
the data files (the classic file installer, or a map-editor
customization), the disk copies win and no synthesis happens — so
editing Delta still works exactly as before. A `DELTAEND_TXT` item in
the mod supplies the campaign's ending text; it enters the game as
`END4_TXT` only through the synthesized archive, so a disk-installed
ending is never overridden.

## Ground rules

- Item and file names: 15 ASCII characters max; `.glb` extension.
- Never ship extracted game data. Aliases reference the player's own
  files; replacement art must be yours.
- `SETUP.INI` gets a `[Mods]` section: `YourMod=1/0` per file.
