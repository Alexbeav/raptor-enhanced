//
// Mod system: auto-mounted add-on GLB archives.
//
// A mod is a .GLB file in mods\ next to the game data. Its items override
// base items with the same name (k-th occurrence maps to k-th occurrence,
// so multi-frame art overrides cleanly), items with new names become
// findable engine-wide, and a MODINFO_TXT manifest names the mod and may
// carry ALIAS directives that remap base items to other base items -
// letting a mod re-skin the game without distributing any game data.
//
// rapmenu.glb (shipped next to the exe) carries the engine's own MODS
// window; without it the MODS menu entry stays hidden.
//

#ifndef MODAPI_H
#define MODAPI_H

#define MOD_MAX 8          // toggle rows in MODMENU_SWD (keep in sync)

void MOD_Startup(void);    // discover + mount; call right after GLB_InitSystem
void MOD_ApplyPending(void);            // rebuild overrides; safe points only
int MOD_PendingChanges(void);           // toggles not yet applied?
int MOD_Resolve(int handle);            // apply override/alias redirects
int MOD_MenuAvailable(void);            // rapmenu.glb mounted?
char *MOD_FilterWindowData(int handle, char *data, int *size); // SWD patches
void MOD_DumpDeltaMaps(void);           // DUMPDELTA flag: write synth maps

int MOD_Count(void);
const char *MOD_Name(int i);            // display name from manifest
int MOD_Enabled(int i);
void MOD_Toggle(int i);                 // flips + persists to SETUP.INI

#endif // MODAPI_H
