//
// Delta Sector synthesis. C port of the splice logic in
// pkg/release/DeltaSector-optional/install_delta_sector.py; every rule
// here (band cuts, plain-row search order, tie-breaks, spawn-order
// normalization) must match it decision-for-decision, because the two
// paths must produce byte-identical maps. Cells and sprite records are
// carried as raw little-endian bytes so untouched fields round-trip
// exactly.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common.h"
#include "delta.h"
#include "glbapi.h"

#define D_ROWS 150
#define D_COLS 9
#define D_CELL 4                            // int16 flats + int16 fgame
#define D_CSPRITE 24
#define D_MAZESIZE (12 + D_ROWS * D_COLS * D_CELL)

typedef unsigned char DCell[D_CELL];

struct DRecord
{
    unsigned char raw[D_CSPRITE];
};

struct DMap
{
    DCell tiles[D_ROWS][D_COLS];
    std::vector<DRecord> spr;
    bool plain[D_ROWS];                     // rows mostly made of the base tile
};

static int32_t RD32(const unsigned char* p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void WR32(unsigned char* p, int32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static int32_t SprX(const DRecord& r) { return RD32(r.raw + 8); }
static int32_t SprY(const DRecord& r) { return RD32(r.raw + 12); }
static int32_t SprLink(const DRecord& r) { return RD32(r.raw); }

// ---------------------------------------------------------------------------
// base-archive item access (never mods: the canonical campaign must be
// built from pristine data even when map-editing mods are mounted)
// ---------------------------------------------------------------------------

static int FindBaseItem(const char* name, int base_files, int* out_file, int* out_item)
{
    for (int f = 0; f < base_files; f++)
    {
        int items = GLB_FileItemCount(f);

        for (int i = 0; i < items; i++)
        {
            if (!strcmp(GLB_FileItemName(f, i), name))
            {
                *out_file = f;
                *out_item = i;
                return 1;
            }
        }
    }

    return 0;
}

static int LoadMap(const char* name, int base_files, DMap& out)
{
    int f, i;

    if (!FindBaseItem(name, base_files, &f, &i))
    {
        LOG_Printf("delta: source map %s not found", name);
        return 0;
    }

    // read through GLB_Load directly: unlike the handle-based getters it
    // never passes through MOD_Resolve, so the canonical campaign is
    // guaranteed to come from pristine base data regardless of overrides
    int size = GLB_Load(NULL, f, i);

    if (size < 12)
    {
        LOG_Printf("delta: cannot read %s", name);
        return 0;
    }

    unsigned char* data = (unsigned char*)malloc(size);

    if (!data)
        EXIT_Error("delta: memory");

    GLB_Load((char*)data, f, i);

    int32_t sizerec = RD32(data);
    int32_t spriteoff = RD32(data + 4);
    int32_t numspr = RD32(data + 8);

    // all bounds math in uint64_t: a malformed count must not wrap int
    if (sizerec != size || spriteoff != D_MAZESIZE || numspr < 0 ||
        (uint64_t)D_MAZESIZE + (uint64_t)numspr * D_CSPRITE > (uint64_t)size)
    {
        LOG_Printf("delta: %s is not a maze level", name);
        free(data);
        return 0;
    }

    memcpy(out.tiles, data + 12, sizeof(out.tiles));
    out.spr.resize(numspr);

    for (int i2 = 0; i2 < numspr; i2++)
        memcpy(out.spr[i2].raw, data + spriteoff + i2 * D_CSPRITE, D_CSPRITE);

    free(data);

    // plain rows: >= 7 of 9 cells equal to the map's most common tile
    // (ties break to the first-encountered tile, like the installer)
    DCell base;
    {
        std::vector<std::pair<const unsigned char*, int>> counts;

        for (int r = 0; r < D_ROWS; r++)
        {
            for (int c = 0; c < D_COLS; c++)
            {
                bool seen = false;

                for (auto& p : counts)
                {
                    if (!memcmp(p.first, out.tiles[r][c], D_CELL))
                    {
                        p.second++;
                        seen = true;
                        break;
                    }
                }

                if (!seen)
                    counts.push_back({ out.tiles[r][c], 1 });
            }
        }

        const unsigned char* best = counts[0].first;
        int best_n = counts[0].second;

        for (auto& p : counts)
        {
            if (p.second > best_n)
            {
                best = p.first;
                best_n = p.second;
            }
        }

        memcpy(base, best, D_CELL);
    }

    for (int r = 0; r < D_ROWS; r++)
    {
        int n = 0;

        for (int c = 0; c < D_COLS; c++)
        {
            if (!memcmp(out.tiles[r][c], base, D_CELL))
                n++;
        }

        out.plain[r] = (n >= 7);
    }

    return 1;
}

// nearest plain row to target within [lo, hi], preferring the lower side
// at equal distance (search order: target-d, then target+d)
static int NearestPlain(const DMap& m, int target, int lo, int hi)
{
    for (int d = 0; d < D_ROWS; d++)
    {
        int cand[2] = { target - d, target + d };

        for (int k = 0; k < 2; k++)
        {
            int r = cand[k];

            if (r >= lo && r <= hi && r >= 0 && r < D_ROWS && m.plain[r])
                return r;
        }
    }

    return target;
}

// split into spawn groups: consecutive records chained until link 1 / -1
static std::vector<std::pair<int, int>> SpawnGroups(const std::vector<DRecord>& spr)
{
    std::vector<std::pair<int, int>> groups;    // [start, end)
    int start = 0;

    for (int i = 0; i < (int)spr.size(); i++)
    {
        int32_t link = SprLink(spr[i]);

        if (link == 1 || link == -1)
        {
            groups.push_back({ start, i + 1 });
            start = i + 1;
        }
    }

    if (start < (int)spr.size())
        groups.push_back({ start, (int)spr.size() });

    return groups;
}

// copy the spawn groups whose head lies in [src_lo, src_hi) shifted to
// dst_lo, optionally mirrored left-right
static void BandSprites(const DMap& m, int src_lo, int src_hi, int dst_lo,
    int mirror, std::vector<DRecord>& out)
{
    for (const auto& g : SpawnGroups(m.spr))
    {
        int32_t head_y = SprY(m.spr[g.first]);

        if (head_y < src_lo || head_y >= src_hi)
            continue;

        for (int i = g.first; i < g.second; i++)
        {
            DRecord rec = m.spr[i];
            int32_t y = SprY(rec) - src_lo + dst_lo;

            WR32(rec.raw + 12, y < 139 ? y : 139);

            if (mirror)
                WR32(rec.raw + 8, (D_COLS - 1) - SprX(rec));

            out.push_back(rec);
        }
    }
}

// the engine's spawn cursor is forward-only: group heads must descend in y
static void NormalizeSpawnOrder(std::vector<DRecord>& spr)
{
    auto groups = SpawnGroups(spr);
    std::stable_sort(groups.begin(), groups.end(),
        [&](const std::pair<int, int>& a, const std::pair<int, int>& b)
        {
            return SprY(spr[a.first]) > SprY(spr[b.first]);
        });

    std::vector<DRecord> out;
    out.reserve(spr.size());

    for (const auto& g : groups)
    {
        for (int i = g.first; i < g.second; i++)
            out.push_back(spr[i]);
    }

    spr.swap(out);
}

// splice three terrain bands (top / middle / bottom thirds around plain
// cut rows) and carry each band's spawn groups along
static int MakeWave(const char* bot_n, const char* mid_n, const char* top_n,
    int base_files, unsigned char** out_data, int* out_size)
{
    DMap top, mid, bot;

    if (!LoadMap(top_n, base_files, top) ||
        !LoadMap(mid_n, base_files, mid) ||
        !LoadMap(bot_n, base_files, bot))
        return 0;

    int cut1 = NearestPlain(top, 50, 35, 70);
    int mid_start = NearestPlain(mid, 28, 15, 55);

    int cut2 = -1;

    for (int d = 0; d < 16 && cut2 == -1; d++)
    {
        int cand[2] = { 100 - d, 100 + d };

        for (int k = 0; k < 2; k++)
        {
            int r = cand[k];
            int mrow = r - cut1 + mid_start - 1;

            if (r >= 85 && r <= 118 && bot.plain[r] &&
                mrow >= 0 && mrow < D_ROWS && mid.plain[mrow])
            {
                cut2 = r;
                break;
            }
        }
    }

    if (cut2 == -1)
        cut2 = NearestPlain(bot, 100, 85, 115);

    std::vector<DRecord> sprites;
    BandSprites(bot, cut2, D_ROWS, cut2, 0, sprites);
    BandSprites(mid, mid_start, mid_start + (cut2 - cut1), cut1, 1, sprites);
    BandSprites(top, 0, cut1, 0, 0, sprites);
    NormalizeSpawnOrder(sprites);

    int n = (int)sprites.size();
    int size = D_MAZESIZE + n * D_CSPRITE;
    unsigned char* data = (unsigned char*)malloc(size);

    if (!data)
        EXIT_Error("delta: memory");

    WR32(data, size);
    WR32(data + 4, D_MAZESIZE);
    WR32(data + 8, n);

    for (int r = 0; r < D_ROWS; r++)
    {
        const DCell* src;

        if (r < cut1)
            src = top.tiles[r];
        else if (r < cut2)
        {
            int mrow = r - cut1 + mid_start;
            src = mid.tiles[mrow < D_ROWS - 1 ? mrow : D_ROWS - 1];
        }
        else
            src = bot.tiles[r];

        memcpy(data + 12 + r * D_COLS * D_CELL, src, D_COLS * D_CELL);
    }

    for (int i = 0; i < n; i++)
        memcpy(data + D_MAZESIZE + i * D_CSPRITE, sprites[i].raw, D_CSPRITE);

    *out_data = data;
    *out_size = size;

    return 1;
}

// ---------------------------------------------------------------------------
// public entry
// ---------------------------------------------------------------------------

int DELTA_Synthesize(const char* recipe, int recipe_len, int base_files,
    char* end_text, int end_len, int reuse_filenum)
{
    char bot[9][16], mid[9][16], top[9][16];
    int have[9] = { 0 };

    std::string text(recipe, recipe_len);
    size_t pos = 0;

    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos,
            eol == std::string::npos ? std::string::npos : eol - pos);
        pos = eol == std::string::npos ? text.size() : eol + 1;

        int wave;
        char b[16], m[16], t[16];

        if (sscanf(line.c_str(), "WAVE %d %15s %15s %15s", &wave, b, m, t) == 4 &&
            wave >= 1 && wave <= 9)
        {
            strcpy(bot[wave - 1], b);
            strcpy(mid[wave - 1], m);
            strcpy(top[wave - 1], t);
            have[wave - 1] = 1;
        }
    }

    for (int i = 0; i < 9; i++)
    {
        if (!have[i])
        {
            LOG_Printf("delta: recipe is missing wave %d", i + 1);
            free(end_text);
            return -1;
        }
    }

    static char names[10][16];
    const char* name_ptrs[10];
    char* datas[10] = { 0 };
    int sizes[10];
    int count = 9;

    for (int i = 0; i < 9; i++)
    {
        unsigned char* data;
        int size;

        if (!MakeWave(bot[i], mid[i], top[i], base_files, &data, &size))
        {
            for (int j = 0; j < i; j++)
                free(datas[j]);

            free(end_text);
            return -1;
        }

        snprintf(names[i], sizeof(names[i]), "MAP%dG4_MAP", i + 1);
        name_ptrs[i] = names[i];
        datas[i] = (char*)data;
        sizes[i] = size;
    }

    // the campaign's ending text rides in the same archive, so it comes
    // and goes with the toggle and never overrides a disk-installed END4
    if (end_text && end_len > 0)
    {
        strcpy(names[9], "END4_TXT");
        name_ptrs[9] = names[9];
        datas[9] = end_text;
        sizes[9] = end_len;
        count = 10;
    }

    int filenum;

    if (reuse_filenum != -1)
        filenum = GLB_UpdateMemory(reuse_filenum, name_ptrs, datas, sizes, count)
            ? reuse_filenum : -1;
    else
        filenum = GLB_MountMemory(name_ptrs, datas, sizes, count);

    if (filenum == -1)
    {
        LOG_Printf(reuse_filenum != -1 ? "delta: archive update refused"
            : "delta: no archive slot free");

        for (int i = 0; i < count; i++)
            free(datas[i]);

        return -1;
    }

    return filenum;
}
