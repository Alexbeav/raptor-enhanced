# Modding Raptor Enhanced

Raptor Enhanced loads **mod files**: `.GLB` archives dropped into a `mods\`
folder next to the game data. The main menu's **MODS** entry (or `M`) lists
them; toggles save to `SETUP.INI` and apply instantly at the main menu
(mid-mission toggles apply when the mission ends). Up to 8 mods load at
once, in filename order — later files win conflicts.

A mod can do three things:

1. **Replace art** (or any item): an item whose name matches a base game
   item overrides it everywhere — menus, HUD, sprites. Multi-frame art
   works: the *k*-th same-named item in your mod replaces the *k*-th in the
   game (the player ship, for example, is 7 items all named `LPLAYER_PIC`).
2. **Add new items**: names the game doesn't have become available
   engine-wide — this is how an add-on campaign's maps would ship.
3. **Alias** existing game items to each other, via the manifest — no data
   copied at all. `NightOps.glb` is 84 bytes: it points every `LPLAYER_PIC`
   (day player ship) at `DPLAYER_PIC` (night ship) and nothing else.

## The manifest

Include an item named `MODINFO_TXT` (plain text):

```
My Mod Name
One line describing it.
ALIAS LPLAYER_PIC DPLAYER_PIC
```

Line 1 is the menu display name, line 2 a description; every `ALIAS A B`
line remaps all occurrences of base item A to base item B.

## Building a mod from PNGs

```
python tools/make_mod.py my-mod-folder --game "C:\path\to\Raptor" -o MyMod.glb
```

Put `modinfo.txt` plus images named after the items they replace
(`CURSOR_PIC.png`; for multi-frame items `LPLAYER_PIC.png`,
`LPLAYER_PIC.2.png`, …) in the folder. Images are quantized to the game
palette (alpha < 128 = transparent) and encoded to match the base item's
format automatically. Requires Python 3.10+ and Pillow. See
`tools/demo-mod/` — the source of the shipped `DemoCursor.glb`.

Item names come from the game's data — browse them (and edit maps, art and
menus) with the [Raptor Map Editor](https://github.com/Alexbeav/raptor-map-editor).

## Sounds and unnamed items

Sound effects live in unnamed items following a `*_FX` label; the
digitized sample is always **4 slots after the label**. A mod item named
`GUN_FX+4` therefore replaces the machine-gun sound — `make_mod.py`
converts a WAV for you (name it `GUN_FX+4.wav`; 8/16-bit mono or stereo,
any rate up to 32 kHz). The same `LABEL+K` addressing reaches any
unnamed item, tiles included (`STARTG1TILES+K`).

## Player gun config

A mod may include a `PLAYRGUN_TXT` item to redefine the standard forward
cannon (the machine gun, not the special weapons):

```
RATE 1
DAMAGE 2
MUZZLE -6 0
MUZZLE 6 0
```

`RATE` is ticks between volleys (stock: 2, lower = faster). `DAMAGE` is
per-shot damage (stock: 1). Each `MUZZLE x y` line (up to 8) adds a
barrel at that offset from the player's center. `EXHAUST x y` moves the
two engine flames: x is the spread from center, y the vertical offset
(stock: 3 and 15) — position them under your airframe's engines.
`SPEED n` scales movement (percent, 25–200) and `ARMOR n` scales
effective hitpoints (percent, 50–400; incoming damage is divided by
ARMOR/100, remainders carried). Only the first muzzle spawns the
muzzle-flash animation, so stacked barrels read as one gun. The shipped
`BulletHose.glb` is a worked example: one centered barrel, maximum
cyclic rate, double damage. Changes hot-apply with the mod toggle.

## Delta Sector recipes

The shipped `DeltaSector.glb` is a third kind of zero-data mod: it
carries a `DELTARCP_TXT` item and the engine *synthesizes* the 4th
campaign's nine maps from the player's own base data at startup,
mounting them in memory (nothing on disk changes). Each recipe line is

```
WAVE 1 MAP1G3_MAP MAP1G2_MAP MAP1G1_MAP
```

— the wave number and the three source maps spliced bottom/middle/top
into the new level. All nine waves must be present. If the maps already
exist in the data files (the classic file installer, or a map-editor
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
