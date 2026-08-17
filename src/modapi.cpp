//
// Mod system implementation. See modapi.h for the model.
//
// Override resolution happens at the GLB handle level: every item fetch in
// glbapi funnels through MOD_Resolve(), so overrides apply equally to
// name lookups and to the hardcoded fileids.h handles the game uses for
// art. Same-named items map occurrence-to-occurrence (the k-th LPLAYER_PIC
// in a mod overrides the k-th in the base data), which is what makes
// multi-frame sprite overrides work.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "common.h"
#include "entypes.h"
#include "modapi.h"
#include "glbapi.h"
#include "prefapi.h"
#include "swdapi.h"
#include "fileids.h"
#include "kbdapi.h"
#include "shots.h"
#include "delta.h"
#include "rap.h"

namespace fs = std::filesystem;

struct ModEntry
{
    std::string stem;      // filename without extension = SETUP.INI key
    std::string display;   // manifest line 1 (falls back to stem)
    std::string path;
    std::vector<std::pair<std::string, std::string>> aliases;
    std::vector<std::pair<int, int>> mappings;   // base handle -> new handle
    std::vector<std::string> additions;          // item names new to the game
    int enabled;
    int filenum;           // -1 only if the mount failed at startup
};

static std::vector<ModEntry> mod_list;
static std::vector<std::pair<int, int>> overrides;  // base handle -> new handle
static int base_files;
static int menu_available;
static int pending_changes;
static int override_generation;
static int delta_filenum = -1;   // memory archive of synthesized Delta maps
static int delta_owner = -1;     // mod whose toggle gates that archive

// ---------------------------------------------------------------------------
// base-data occurrence lookups
// ---------------------------------------------------------------------------

static int OccHandle(const char* name, int k)
{
    int c = 0;

    for (int f = 0; f < base_files; f++)
    {
        int items = GLB_FileItemCount(f);

        for (int i = 0; i < items; i++)
        {
            if (!strcmp(GLB_FileItemName(f, i), name))
            {
                if (c == k)
                    return (f << 16) | i;
                c++;
            }
        }
    }

    return -1;
}

static int OccCount(const char* name)
{
    int c = 0;

    for (int f = 0; f < base_files; f++)
    {
        int items = GLB_FileItemCount(f);

        for (int i = 0; i < items; i++)
        {
            if (!strcmp(GLB_FileItemName(f, i), name))
                c++;
        }
    }

    return c;
}

static void AddOverride(int from, int to)
{
    for (auto& p : overrides)
    {
        if (p.first == from)
        {
            p.second = to;  // later mods win
            return;
        }
    }

    overrides.push_back({ from, to });
}

// ---------------------------------------------------------------------------
// manifest
// ---------------------------------------------------------------------------

static void ParseManifest(ModEntry& mod)
{
    int items = GLB_FileItemCount(mod.filenum);

    for (int i = 0; i < items; i++)
    {
        if (strcmp(GLB_FileItemName(mod.filenum, i), "MODINFO_TXT"))
            continue;

        int handle = (mod.filenum << 16) | i;
        int size = GLB_ItemSize(handle);
        char* data = GLB_GetItem(handle);

        if (!data || size <= 0)
            return;

        std::string text(data, size);
        GLB_FreeItem(handle);

        int lineno = 0;
        size_t pos = 0;

        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = eol == std::string::npos ? text.size() : eol + 1;

            while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
                line.pop_back();

            if (lineno == 0 && !line.empty())
                mod.display = line;

            char src[16], dst[16];

            if (sscanf(line.c_str(), "ALIAS %15s %15s", src, dst) == 2)
            {
                int nsrc = OccCount(src);
                int ndst = OccCount(dst);

                if (!nsrc || !ndst)
                {
                    LOG_Printf("mod %s: ALIAS %s %s skipped (item not found)",
                        mod.stem.c_str(), src, dst);
                }
                else
                {
                    mod.aliases.push_back({ src, dst });
                }
            }

            lineno++;
        }

        return;
    }
}

// Computed once at mount: every base item this mod would redirect (and
// every brand-new name it adds). ApplyPending replays the enabled mods'
// mappings in order; MOD_Toggle uses the same data for conflict checks.
static void ComputeMappings(ModEntry& mod)
{
    int items = GLB_FileItemCount(mod.filenum);

    for (const auto& a : mod.aliases)
    {
        int nsrc = OccCount(a.first.c_str());
        int ndst = OccCount(a.second.c_str());

        for (int k = 0; k < nsrc; k++)
        {
            int d = OccHandle(a.second.c_str(), k < ndst ? k : ndst - 1);
            mod.mappings.push_back({ OccHandle(a.first.c_str(), k), d });
        }
    }

    for (int i = 0; i < items; i++)
    {
        const char* name = GLB_FileItemName(mod.filenum, i);

        if (!name[0] || !strcmp(name, "MODINFO_TXT"))
            continue;

        // "LABEL+K" targets the (unnamed) item K slots after a label -
        // that is how sounds (GUN_FX+4 = the digitized sample) and tiles
        // (STARTG1TILES+K) are addressed, since those items carry no name
        const char* plus = strchr(name, '+');

        if (plus && plus != name)
        {
            char label[16];
            int off = atoi(plus + 1);
            int len = (int)(plus - name);

            if (off > 0 && len < (int)sizeof(label))
            {
                memcpy(label, name, len);
                label[len] = 0;

                int base = OccHandle(label, 0);

                if (base != -1 &&
                    ((base & 0xffff) + off) < GLB_FileItemCount(base >> 16))
                {
                    mod.mappings.push_back({ base + off, (mod.filenum << 16) | i });
                    continue;
                }
            }

            LOG_Printf("mod %s: cannot resolve %s", mod.stem.c_str(), name);
            continue;
        }

        // occurrence of this name so far within THIS mod
        int k = 0;

        for (int j = 0; j < i; j++)
        {
            if (!strcmp(GLB_FileItemName(mod.filenum, j), name))
                k++;
        }

        int base = OccHandle(name, k);

        if (base != -1)
            mod.mappings.push_back({ base, (mod.filenum << 16) | i });
        else if (k == 0)
            mod.additions.push_back(name);
        // additions are found by GLB_GetItemID's normal search
    }
}

// Two mods conflict when they would redirect the same base item or add
// an item under the same new name.
static int ModsConflict(const ModEntry& a, const ModEntry& b)
{
    for (const auto& ma : a.mappings)
        for (const auto& mb : b.mappings)
            if (ma.first == mb.first)
                return 1;

    for (const auto& na : a.additions)
        for (const auto& nb : b.additions)
            if (na == nb)
                return 1;

    return 0;
}

// ---------------------------------------------------------------------------
// Delta Sector synthesis: a mod carrying a DELTARCP_TXT recipe gets the
// 4th campaign's nine maps built from the player's own base data and
// mounted as a memory archive gated by the mod's toggle. A disk install
// (the classic installer, or a map-editor customization) always wins:
// base-file items shadow the memory archive in every name search.
// ---------------------------------------------------------------------------

static int FindModItem(const ModEntry& mod, const char* name)
{
    int items = GLB_FileItemCount(mod.filenum);

    for (int j = 0; j < items; j++)
    {
        if (!strcmp(GLB_FileItemName(mod.filenum, j), name))
            return j;
    }

    return -1;
}

// raw read of one of a mod's own items (never MOD_Resolved; adopted copy)
static char* ReadModItem(const ModEntry& mod, int itemnum, int* out_size)
{
    int size = GLB_Load(NULL, mod.filenum, itemnum);

    if (size <= 0)
        return NULL;

    char* data = (char*)malloc(size);

    if (!data)
        EXIT_Error("mods: memory");

    GLB_Load(data, mod.filenum, itemnum);
    *out_size = size;

    return data;
}

static int DiskDeltaMaps(void)
{
    char name[16];
    int disk = 0;

    for (int w = 1; w <= 9; w++)
    {
        snprintf(name, sizeof(name), "MAP%dG4_MAP", w);

        if (OccHandle(name, 0) != -1)
            disk++;
    }

    return disk;
}

// Every recipe-carrying mod gets the campaign's names as additions BEFORE
// conflict enforcement, so two Delta providers register as conflicting.
static void RegisterDeltaAdditions(void)
{
    if (DiskDeltaMaps() == 9)
        return;

    char name[16];

    for (auto& mod : mod_list)
    {
        if (FindModItem(mod, "DELTARCP_TXT") == -1)
            continue;

        for (int w = 1; w <= 9; w++)
        {
            snprintf(name, sizeof(name), "MAP%dG4_MAP", w);

            if (OccHandle(name, 0) == -1)
                mod.additions.push_back(name);
        }

        if (OccHandle("END4_TXT", 0) == -1)
            mod.additions.push_back("END4_TXT");
    }
}

// Startup counterpart of MOD_Toggle's guard: mods enabled from SETUP.INI
// (or by default) that fight over the same items must not start together.
// Earlier-mounted (alphabetical) wins, deterministically.
static void EnforceStartupConflicts(void)
{
    for (size_t i = 0; i < mod_list.size(); i++)
    {
        if (!mod_list[i].enabled)
            continue;

        for (size_t j = 0; j < i; j++)
        {
            if (!mod_list[j].enabled)
                continue;

            if (ModsConflict(mod_list[i], mod_list[j]))
            {
                mod_list[i].enabled = 0;
                INI_PutPreferenceLong("Mods", mod_list[i].stem.c_str(), 0);
                LOG_Printf("mod %s: disabled at startup, conflicts with %s",
                    mod_list[i].stem.c_str(), mod_list[j].stem.c_str());
                break;
            }
        }
    }
}

// build (or rebuild, at a safe point) the memory archive from one mod's
// recipe; returns the archive filenum or -1
static int SynthFromMod(int owner, int reuse_filenum)
{
    ModEntry& mod = mod_list[owner];
    int rcp = FindModItem(mod, "DELTARCP_TXT");

    if (rcp == -1)
        return -1;

    int size = 0;
    char* recipe = ReadModItem(mod, rcp, &size);

    if (!recipe)
        return -1;

    char* end_text = NULL;
    int end_len = 0;
    int end_item = FindModItem(mod, "DELTAEND_TXT");

    if (end_item != -1)
        end_text = ReadModItem(mod, end_item, &end_len);

    int fn = DELTA_Synthesize(recipe, size, base_files, end_text, end_len,
        reuse_filenum);
    free(recipe);

    if (fn != -1)
        LOG_Printf("mod %s: Delta Sector synthesized from base data",
            mod.stem.c_str());

    return fn;
}

static void SynthDelta(void)
{
    if (DiskDeltaMaps() == 9)
        return;

    // owner: the first enabled recipe mod, else the first recipe mod
    // (so a mod disabled now can still be toggled on later this session)
    int first = -1, owner = -1;

    for (int i = 0; i < (int)mod_list.size(); i++)
    {
        if (FindModItem(mod_list[i], "DELTARCP_TXT") == -1)
            continue;

        if (first == -1)
            first = i;

        if (owner == -1 && mod_list[i].enabled)
            owner = i;
        else if (owner != -1 || i != first)
            LOG_Printf("mod %s: Delta recipe ignored, %s already provides the campaign",
                mod_list[i].stem.c_str(), mod_list[owner != -1 ? owner : first].stem.c_str());
    }

    if (owner == -1)
        owner = first;

    if (owner == -1)
        return;

    // the ending text is deliberately NOT named END4_TXT in the mod file:
    // it enters the game only through the synthesized archive, so a
    // disk-installed (possibly customized) END4_TXT always wins
    delta_filenum = SynthFromMod(owner, -1);

    if (delta_filenum != -1)
        delta_owner = owner;
}

// all nine campaign maps resolvable right now (disk or enabled synthesis)?
static int DeltaAvailable(void)
{
    char name[16];

    for (int w = 1; w <= 9; w++)
    {
        snprintf(name, sizeof(name), "MAP%dG4_MAP", w);

        if (GLB_GetItemID(name) == -1)
            return 0;
    }

    return 1;
}

// ---------------------------------------------------------------------------
// public api
// ---------------------------------------------------------------------------

// same resolution order as GLB_FindFile: working directory first, then the
// path the base archives were mounted from
static std::string FindGamePath(const char* relative)
{
    std::error_code ec;

    if (fs::exists(relative, ec))
        return relative;

    std::string alt = std::string(GLB_GetExePath()) + relative;

    if (fs::exists(alt, ec))
        return alt;

    return "";
}

void MOD_Startup(void)
{
    base_files = GLB_NumFiles();

    // the engine's own MODS window ships as a separate archive; menus stay
    // hidden without it, everything else still works
    std::string menu_glb = FindGamePath("rapmenu.glb");
    menu_available = !menu_glb.empty() && GLB_MountPath(menu_glb.c_str()) != -1;

    std::string mods_dir = FindGamePath("mods");
    std::error_code ec;
    std::vector<fs::path> found;

    if (mods_dir.empty())
        return;

    fs::directory_iterator it(mods_dir, ec);

    if (ec)
    {
        LOG_Printf("mods: cannot scan %s: %s", mods_dir.c_str(), ec.message().c_str());
        return;
    }

    try
    {
        for (const auto& e : it)
        {
            std::string ext = e.path().extension().string();
            for (auto& c : ext)
                c = (char)std::tolower((unsigned char)c);

            if (ext == ".glb")
                found.push_back(e.path());
        }
    }
    catch (const fs::filesystem_error& err)
    {
        LOG_Printf("mods: directory scan error: %s", err.what());
    }

    std::sort(found.begin(), found.end());

    for (const auto& path : found)
    {
        ModEntry mod;
        mod.stem = path.stem().string();
        mod.display = mod.stem;
        mod.path = path.string();
        mod.enabled = INI_GetPreferenceLong("Mods", mod.stem.c_str(), 1) != 0;
        mod.filenum = -1;

        if ((int)mod_list.size() >= MOD_MAX)
        {
            LOG_Printf("mod %s: ignored (limit %d)", mod.stem.c_str(), MOD_MAX);
            continue;
        }

        mod.filenum = GLB_MountPath(mod.path.c_str());

        if (mod.filenum == -1)
        {
            // not listed, no INI entry: a dead file must not occupy a menu
            // slot or masquerade as an enabled mod
            LOG_Printf("mod %s: mount failed, skipping", mod.stem.c_str());
            continue;
        }

        INI_PutPreferenceLong("Mods", mod.stem.c_str(), mod.enabled);
        ParseManifest(mod);
        ComputeMappings(mod);
        mod_list.push_back(mod);
    }

    RegisterDeltaAdditions();
    EnforceStartupConflicts();
    SynthDelta();

    MOD_ApplyPending();

    if (!mod_list.empty())
        LOG_Printf("mods: %d found, %d overrides active",
            (int)mod_list.size(), (int)overrides.size());
}

// Mount newly-enabled mods and rebuild the override table from the current
// enabled set. ONLY call at a safe point: no window may hold locks taken
// through the old table (the main-menu teardown/rebuild in WIN_MainMenu),
// or at startup before anything is locked. Disabled mods stay mounted but
// contribute nothing; GLB name lookups skip their files.
void MOD_ApplyPending(void)
{
    int was_pending = pending_changes;

    pending_changes = 0;
    overrides.clear();
    override_generation++;

    int on = 0;

    for (auto& mod : mod_list)
    {
        if (mod.filenum != -1)
        {
            GLB_SetFileEnabled(mod.filenum, mod.enabled);

            if (mod.enabled)
            {
                on++;

                for (const auto& m : mod.mappings)
                    AddOverride(m.first, m.second);
            }
        }
    }

    // the synthesized Delta archive follows its owning mod's toggle; if a
    // DIFFERENT recipe mod is now the enabled provider, rebuild the archive
    // from its recipe (this is a safe point - GLB_UpdateMemory refuses if
    // any of the archive's items is still locked)
    if (delta_filenum != -1)
    {
        int want = -1;

        for (int i = 0; i < (int)mod_list.size(); i++)
        {
            if (mod_list[i].enabled &&
                FindModItem(mod_list[i], "DELTARCP_TXT") != -1)
            {
                want = i;
                break;
            }
        }

        if (want != -1 && want != delta_owner)
        {
            if (SynthFromMod(want, delta_filenum) == delta_filenum)
                delta_owner = want;
            else
                pending_changes = 1;   // safe point violated? retry later
        }

        GLB_SetFileEnabled(delta_filenum,
            delta_owner >= 0 && mod_list[delta_owner].enabled);
    }

    RAP_DetectSector4();

    LOG_Printf("mods: applied - %d of %d enabled, %d overrides",
        on, (int)mod_list.size(), (int)overrides.size());

    // configs derived from moddable items re-read the current set
    if (was_pending)
        SHOTS_LoadGunConfig();
}

int MOD_PendingChanges(void)
{
    return pending_changes;
}

int MOD_Resolve(int handle)
{
    // follow redirect chains (alias to an item another mod overrides); a
    // chain cannot be longer than the table without a cycle, so bound there
    for (size_t depth = 0; depth <= overrides.size(); depth++)
    {
        int next = handle;

        for (const auto& p : overrides)
        {
            if (p.first == handle)
            {
                next = p.second;
                break;
            }
        }

        if (next == handle)
            break;

        handle = next;
    }

    return handle;
}

int MOD_MenuAvailable(void)
{
    return menu_available;
}

int MOD_Count(void)
{
    return (int)mod_list.size();
}

const char* MOD_Name(int i)
{
    if (i < 0 || i >= (int)mod_list.size())
        return "";

    return mod_list[i].display.c_str();
}

int MOD_Enabled(int i)
{
    if (i < 0 || i >= (int)mod_list.size())
        return 0;

    return mod_list[i].enabled;
}

void MOD_Toggle(int i)
{
    if (i < 0 || i >= (int)mod_list.size())
        return;

    mod_list[i].enabled ^= 1;
    pending_changes = 1;
    INI_PutPreferenceLong("Mods", mod_list[i].stem.c_str(), mod_list[i].enabled);

    // conflict guard: enabling a mod turns off enabled mods that would
    // fight over the same items (the menu re-render shows them [OFF])
    if (mod_list[i].enabled)
    {
        for (int j = 0; j < (int)mod_list.size(); j++)
        {
            if (j == i || !mod_list[j].enabled)
                continue;

            if (ModsConflict(mod_list[i], mod_list[j]))
            {
                mod_list[j].enabled = 0;
                INI_PutPreferenceLong("Mods", mod_list[j].stem.c_str(), 0);
                LOG_Printf("mod %s: disabled, conflicts with %s",
                    mod_list[j].stem.c_str(), mod_list[i].stem.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MAIN_SWD runtime patch: appends an invisible text button "MODS"
// (field index MAIN_MODS). Same transform as the Delta installer's
// SHIPCOMP patch, done in memory on the disk-format (SFIELD32) data.
// ---------------------------------------------------------------------------

static char* FilterMainMenu(char* data, int* size)
{
    static char* cached;
    static int cached_size;
    static int cached_generation = -1;

    if (!data || *size < (int)sizeof(SWIN))
        return data;

    if (!cached || cached_generation != override_generation)
    {
        // a mod may have overridden MAIN_SWD itself since the last build;
        // the old buffer can still back a live window, so leak it (same
        // lifetime model as SWD_ReformatFieldData's copies)
        cached = NULL;
        cached_generation = override_generation;
    }

    if (!cached)
    {
        SWIN* h = (SWIN*)data;
        int fldofs = LE_LONG(h->fldofs);
        int n = LE_LONG(h->numflds);

        // a mod may override MAIN_SWD with arbitrary data; only patch a
        // window whose header describes a well-formed field table
        // (64-bit math: file-supplied offsets must not wrap int)
        if (n < 1 || n > 256 ||
            fldofs < (int)sizeof(SWIN) || fldofs > *size ||
            (int64_t)fldofs + (int64_t)n * (int64_t)sizeof(SFIELD32) > (int64_t)*size)
            return data;

        int textofs = fldofs + n * (int)sizeof(SFIELD32);
        int textlen = *size - textofs;
        const char label[] = "MODS";

        cached_size = *size + (int)sizeof(SFIELD32) + (int)sizeof(label);
        cached = (char*)calloc(1, cached_size);

        if (!cached)
            EXIT_Error("MOD_FilterWindowData: memory");

        // header (including any gap bytes up to the field table)
        memcpy(cached, data, fldofs);
        SWIN* nh = (SWIN*)cached;
        nh->numflds = LE_LONG(n + 1);
        nh->txtofs = LE_LONG(fldofs + (n + 1) * (int)sizeof(SFIELD32));

        // existing fields: records keep their position, text moves away
        SFIELD32* srcf = (SFIELD32*)(data + fldofs);
        SFIELD32* dstf = (SFIELD32*)(cached + fldofs);

        for (int i = 0; i < n; i++)
        {
            memcpy(&dstf[i], &srcf[i], sizeof(SFIELD32));
            dstf[i].txtoff = LE_LONG(LE_LONG(dstf[i].txtoff) + (int)sizeof(SFIELD32));
        }

        // the new button, cloned defaults from scratch (invisible text style)
        SFIELD32* nf = &dstf[n];
        memset(nf, 0, sizeof(SFIELD32));
        nf->opt = LE_LONG(2);                      // FLD_BUTTON
        nf->id = LE_LONG(n);
        nf->hotkey = LE_LONG(SC_M);
        strcpy(nf->name, "MODS");
        nf->item = LE_LONG(-1);
        strcpy(nf->font_name, "FONT1_FNT");
        nf->fontid = LE_LONG(-1);
        nf->fontbasecolor = LE_LONG(82);
        nf->maxchars = LE_LONG(5);
        nf->picflag = LE_LONG(4);                  // INVISABLE -> text only
        nf->lite = LE_LONG(15);
        nf->selectable = LE_LONG(1);
        nf->x = LE_LONG(8);
        nf->y = LE_LONG(186);
        nf->lx = LE_LONG(48);
        nf->ly = LE_LONG(10);
        nf->txtoff = LE_LONG((int)sizeof(SFIELD32) + textlen);

        // text area + our label
        memcpy(cached + fldofs + (n + 1) * sizeof(SFIELD32), data + textofs, textlen);
        memcpy(cached + cached_size - (int)sizeof(label), label, sizeof(label));
    }

    *size = cached_size;
    return cached;
}

// ---------------------------------------------------------------------------
// SHIPCOMP_SWD runtime patch: when the Delta campaign is available but the
// window on disk is unpatched (12 fields), append the GAME4 row and respace
// the sector buttons - the exact transform install_delta_sector.py applies
// on disk, done in memory. Field index 12 = winids.h COMP_GAME4.
// ---------------------------------------------------------------------------

static char* FilterShipComp(char* data, int* size)
{
    static char* cached;
    static int cached_size;
    static int cached_generation = -1;

    if (!data || *size < (int)sizeof(SWIN))
        return data;

    if (!DeltaAvailable())
        return data;

    SWIN* h = (SWIN*)data;
    int n = LE_LONG(h->numflds);

    if (n != 12)
        return data;      // already carries the installer's disk patch

    if (cached_generation != override_generation)
    {
        // old buffer may back a live window; leak it (see FilterMainMenu)
        cached = NULL;
        cached_generation = override_generation;
    }

    if (!cached)
    {
        int fldofs = LE_LONG(h->fldofs);
        int txtofs = LE_LONG(h->txtofs);

        // same caution as FilterMainMenu: the item may come from a mod.
        // Text must start right after the field table (every shipped and
        // installer-patched SHIPCOMP does) - the rebuild does not preserve
        // a gap between the two, so any other layout is left alone
        if (fldofs < (int)sizeof(SWIN) || fldofs > *size ||
            (int64_t)fldofs + 12 * (int64_t)sizeof(SFIELD32) > (int64_t)*size ||
            txtofs != fldofs + 12 * (int)sizeof(SFIELD32) ||
            txtofs > *size)
            return data;

        int textlen = *size - txtofs;
        int new_txtofs = fldofs + 13 * (int)sizeof(SFIELD32);
        const char label[] = "DELTA SECTOR";

        cached_size = new_txtofs + textlen + (int)sizeof(label);
        cached = (char*)calloc(1, cached_size);

        if (!cached)
            EXIT_Error("FilterShipComp: memory");

        memcpy(cached, data, fldofs);
        SWIN* nh = (SWIN*)cached;
        nh->numflds = LE_LONG(13);
        nh->txtofs = LE_LONG(new_txtofs);

        SFIELD32* srcf = (SFIELD32*)(data + fldofs);
        SFIELD32* dstf = (SFIELD32*)(cached + fldofs);

        for (int i = 0; i < 12; i++)
        {
            memcpy(&dstf[i], &srcf[i], sizeof(SFIELD32));
            dstf[i].txtoff = LE_LONG(LE_LONG(dstf[i].txtoff) + (int)sizeof(SFIELD32));
        }

        // the new row is a clone of the OUTER REGIONS button; the engine
        // re-resolves its pic and font by name at window init
        memcpy(&dstf[12], &srcf[10], sizeof(SFIELD32));
        memset(dstf[12].name, 0, sizeof(dstf[12].name));
        strcpy(dstf[12].name, "GAME4");

        // respace the five stacked rows to make room
        dstf[4].y = LE_LONG(39);
        dstf[5].y = LE_LONG(61);
        dstf[10].y = LE_LONG(83);
        dstf[12].y = LE_LONG(105);
        dstf[11].y = LE_LONG(127);

        dstf[12].txtoff = LE_LONG((new_txtofs + textlen) - (fldofs + 12 * (int)sizeof(SFIELD32)));

        memcpy(cached + new_txtofs, data + txtofs, textlen);
        memcpy(cached + new_txtofs + textlen, label, sizeof(label));
    }

    *size = cached_size;
    return cached;
}

char* MOD_FilterWindowData(int handle, char* data, int* size)
{
    if (handle == FILE132_MAIN_SWD && menu_available)
        return FilterMainMenu(data, size);

    if (handle == FILE133_SHIPCOMP_SWD)
        return FilterShipComp(data, size);

    return data;
}

// hidden DUMPDELTA support: write each synthesized map next to the exe so
// the release test can diff them against the Python installer's output
void MOD_DumpDeltaMaps(void)
{
    if (delta_filenum == -1)
    {
        printf("delta: nothing synthesized (disk install present, or no recipe mod)\n");
        return;
    }

    int items = GLB_FileItemCount(delta_filenum);

    for (int i = 0; i < items; i++)
    {
        int handle = (delta_filenum << 16) | i;
        int isize = GLB_ItemSize(handle);
        char* data = GLB_GetItem(handle);
        char fname[32];

        snprintf(fname, sizeof(fname), "%s.bin", GLB_FileItemName(delta_filenum, i));
        GLB_SaveFile(fname, data, isize);
        printf("%s: %d bytes\n", fname, isize);
        GLB_FreeItem(handle);
    }

    // also dump the runtime-patched ship computer window for diffing
    // against the installer's on-disk transform
    {
        int isize = GLB_ItemSize(FILE133_SHIPCOMP_SWD);
        char* data = GLB_GetItem(FILE133_SHIPCOMP_SWD);
        char* patched = MOD_FilterWindowData(FILE133_SHIPCOMP_SWD, data, &isize);
        char fname[32] = "SHIPCOMP_SWD.bin";

        GLB_SaveFile(fname, patched, isize);
        printf("%s: %d bytes\n", fname, isize);
        GLB_FreeItem(FILE133_SHIPCOMP_SWD);
    }
}
