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

namespace fs = std::filesystem;

struct ModEntry
{
    std::string stem;      // filename without extension = SETUP.INI key
    std::string display;   // manifest line 1 (falls back to stem)
    std::string path;
    std::vector<std::pair<std::string, std::string>> aliases;
    int enabled;
    int filenum;           // -1 only if the mount failed at startup
};

static std::vector<ModEntry> mod_list;
static std::vector<std::pair<int, int>> overrides;  // base handle -> new handle
static int base_files;
static int menu_available;
static int pending_changes;
static int override_generation;

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

static void BuildItemOverrides(const ModEntry& mod)
{
    int items = GLB_FileItemCount(mod.filenum);

    for (int i = 0; i < items; i++)
    {
        const char* name = GLB_FileItemName(mod.filenum, i);

        if (!name[0] || !strcmp(name, "MODINFO_TXT"))
            continue;

        // occurrence of this name so far within THIS mod
        int k = 0;

        for (int j = 0; j < i; j++)
        {
            if (!strcmp(GLB_FileItemName(mod.filenum, j), name))
                k++;
        }

        int base = OccHandle(name, k);

        if (base != -1)
            AddOverride(base, (mod.filenum << 16) | i);
        // unmatched names are additions; GLB_GetItemID finds them normally
    }
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
        mod_list.push_back(mod);
    }

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

    for (auto& mod : mod_list)
    {
        if (mod.filenum != -1)
        {
            GLB_SetFileEnabled(mod.filenum, mod.enabled);

            if (mod.enabled)
            {
                for (const auto& a : mod.aliases)
                {
                    int nsrc = OccCount(a.first.c_str());
                    int ndst = OccCount(a.second.c_str());

                    for (int k = 0; k < nsrc; k++)
                    {
                        int d = OccHandle(a.second.c_str(), k < ndst ? k : ndst - 1);
                        AddOverride(OccHandle(a.first.c_str(), k), d);
                    }
                }

                BuildItemOverrides(mod);
            }
        }
    }

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
}

// ---------------------------------------------------------------------------
// MAIN_SWD runtime patch: appends an invisible text button "MODS"
// (field index MAIN_MODS). Same transform as the Delta installer's
// SHIPCOMP patch, done in memory on the disk-format (SFIELD32) data.
// ---------------------------------------------------------------------------

char* MOD_FilterWindowData(int handle, char* data, int* size)
{
    static char* cached;
    static int cached_size;
    static int cached_generation = -1;

    if (!menu_available || handle != FILE132_MAIN_SWD)
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
