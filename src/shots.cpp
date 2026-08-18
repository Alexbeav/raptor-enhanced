#include <string.h>
#include <stddef.h>
#include "common.h"
#include "shots.h"
#include "glbapi.h"
#include "fx.h"
#include "rap.h"
#include "enemy.h"
#include "anims.h"
#include "tile.h"
#include "eshot.h"
#include "objects.h"
#include "fileids.h"
#include "entypes.h"

SHOTS shots[MAX_SHOTS];

SHOTS first_shots, last_shots;

SHOTS *free_shots;

int shotnum;
int shothigh;

char *detpow[4];
char *laspow[4];
char *lashit[4];

SHOT_LIB shot_lib[LAST_WEAPON + 1];

/***************************************************************************
SHOTS_Clear () * Clears out SHOTS Linklist
 ***************************************************************************/
void 
SHOTS_Clear(
    void
)
{
    int loop;
    
    shotnum = 0;
    
    first_shots.prev = NULL;
    first_shots.next = &last_shots;
    
    last_shots.prev = &first_shots;
    last_shots.next = NULL;
    
    free_shots = shots;
    
    memset(shots, 0, sizeof(shots));

    for (loop = 0; loop < MAX_SHOTS; loop++)
    {
        if (loop == 69) // FIXME
        {
            shots[loop].next = &first_shots;
            continue;
        }
        
        shots[loop].next = &shots[loop + 1];
    }
}

/*-------------------------------------------------------------------------*
SHOTS_Get () - gets a Free SHOT OBJECT from linklist
 *-------------------------------------------------------------------------*/
SHOTS
*SHOTS_Get(
    void
)
{
    SHOTS *news;
    
    if (!free_shots)
        return NULL;
    
    shotnum++;
    if (shotnum > shothigh)
        shothigh = shotnum;
    
    news = free_shots;
    free_shots = free_shots->next;
    
    memset(news, 0, sizeof(SHOTS));
    
    news->next = &last_shots;
    news->prev = last_shots.prev;
    last_shots.prev = news;
    news->prev->next = news;
    
    return news;
}

/*-------------------------------------------------------------------------*
SHOTS_Remove () - Removes SHOT OBJECT from linklist
 *-------------------------------------------------------------------------*/
SHOTS
*SHOTS_Remove(
    SHOTS *sh
)
{
    SHOTS *next;
    
    shotnum--;
    
    next = sh->prev;
    
    sh->next->prev = sh->prev;
    sh->prev->next = sh->next;
    
    memset(sh, 0, sizeof(SHOTS));
    
    sh->next = free_shots;
    
    free_shots = sh;
    
    return next;
}

/***************************************************************************
Moddable player cannon: a mod may provide a PLAYRGUN_TXT item that
replaces the standard forward guns' muzzle layout and fire cadence:

    RATE 1            ticks between volleys ( stock: 2 )
    DAMAGE 2          per-shot damage ( stock: 1 )
    MUZZLE 0 0        up to 8 lines: x and y offset from the player center
    EXHAUST 4 5       engine flame positions: x spread and y offset from
                      the player center ( stock: 3 and 15 )
    SPEED 80          movement speed percent ( stock: 100, 25-200 )
    ARMOR 150         effective hitpoints percent: incoming damage is
                      scaled by 100/ARMOR ( stock: 100, 50-400 )

Loaded at startup and again whenever the mod set hot-applies. Without
the item (or with no MUZZLE lines) the stock twin guns are untouched.
 ***************************************************************************/
#define MAX_MUZZLES 8
#define NEWLINE_CH 0x0a

static struct
{
    int active;
    int rate;
    int damage;
    int exhaust_set;
    int exhaust_dx;
    int exhaust_dy;
    int speed_pct;
    int armor_pct;
    int armor_acc;
    int count;
    int mx[MAX_MUZZLES];
    int my[MAX_MUZZLES];
} playrgun;

#define STOCK_FORWARD_HITS 1     // shot_lib[S_FORWARD_GUNS].hits in SHOTS_Init

// Copy of the shot table as SHOTS_Init built it, so a WEAPONS_TXT override
// can be undone when its mod is switched off. Also restores pic[]/h, which
// a LUMP or FRAMES override re-points.
static SHOT_LIB shot_lib_stock[LAST_WEAPON + 1];
static int      shot_lib_saved;

static void
LoadPlayerGun(
    void
)
{
    char line[64];
    char *data;
    int item, size, pos, len, dx, dy, rate, damage;

    memset(&playrgun, 0, sizeof(playrgun));
    playrgun.rate = 2;
    playrgun.damage = STOCK_FORWARD_HITS;
    playrgun.speed_pct = 100;
    playrgun.armor_pct = 100;

    // damage lives in the shared shot lib (collision code reads
    // lib->hits), so restore stock here in case a config was active
    // before this reload and is now gone or disabled
    shot_lib[S_FORWARD_GUNS].hits = STOCK_FORWARD_HITS;

    item = GLB_GetItemID("PLAYRGUN_TXT");

    if (item == -1)
        return;
    
    size = GLB_ItemSize(item);
    data = GLB_GetItem(item);
    
    if (!data || size <= 0)
        return;
    
    for (pos = 0; pos < size; )
    {
        for (len = 0; pos + len < size && data[pos + len] != NEWLINE_CH && len < (int)sizeof(line) - 1; len++)
            line[len] = data[pos + len];
        
        line[len] = 0;
        
        while (pos + len < size && data[pos + len] != NEWLINE_CH)
            len++;
        
        pos += len + 1;
        
        if (sscanf(line, "RATE %d", &rate) == 1)
        {
            if (rate < 1)
                rate = 1;
            if (rate > 35)
                rate = 35;
            playrgun.rate = rate;
        }
        else if (sscanf(line, "DAMAGE %d", &damage) == 1)
        {
            if (damage < 1)
                damage = 1;
            if (damage > 50)
                damage = 50;
            playrgun.damage = damage;
        }
        else if (sscanf(line, "SPEED %d", &dx) == 1)
        {
            if (dx < 25) dx = 25;
            if (dx > 200) dx = 200;
            playrgun.speed_pct = dx;
        }
        else if (sscanf(line, "ARMOR %d", &dx) == 1)
        {
            if (dx < 50) dx = 50;
            if (dx > 400) dx = 400;
            playrgun.armor_pct = dx;
        }
        else if (sscanf(line, "EXHAUST %d %d", &dx, &dy) == 2)
        {
            if (dx < 0) dx = 0;
            if (dx > 16) dx = 16;
            if (dy < -16) dy = -16;
            if (dy > 16) dy = 16;
            playrgun.exhaust_dx = dx;
            playrgun.exhaust_dy = dy;
            playrgun.exhaust_set = 1;
        }
        else if (sscanf(line, "MUZZLE %d %d", &dx, &dy) == 2 &&
                 playrgun.count < MAX_MUZZLES)
        {
            if (dx < -160) dx = -160;
            if (dx > 160) dx = 160;
            if (dy < -100) dy = -100;
            if (dy > 100) dy = 100;
            playrgun.mx[playrgun.count] = dx;
            playrgun.my[playrgun.count] = dy;
            playrgun.count++;
        }
    }
    
    GLB_FreeItem(item);
    
    if (playrgun.count)
    {
        playrgun.active = 1;
        shot_lib[S_FORWARD_GUNS].hits = playrgun.damage;
        LOG_Printf("player gun config: %d muzzle(s), rate %d, damage %d",
            playrgun.count, playrgun.rate, playrgun.damage);
    }
}

/***************************************************************************
Moddable special weapons: a mod may provide a WEAPONS_TXT item whose lines
are each

    <WEAPON> <FIELD> <value>

e.g. "PLASMA DAMAGE 4" or "DEATHRAY RATE 3". Every field of SHOT_LIB that
is not runtime state or derived from the art is writable, including the
lump number - so a mod can point a weapon at completely different sprites.
Nothing here is balanced or sanity-checked beyond memory safety: bad
numbers make bad weapons, which is the point.

Applied over the stock table on every mod hot-apply, so switching the mod
off restores the originals.
 ***************************************************************************/
static const struct
{
    const char *name;
    int         idx;
} weapon_names[] = {
    { "FORWARD",    S_FORWARD_GUNS  },
    { "PLASMA",     S_PLASMA_GUNS   },
    { "MICRO",      S_MICRO_MISSLE  },
    { "DUMBFIRE",   S_DUMB_MISSLE   },
    { "MINIGUN",    S_MINI_GUN      },
    { "TURRET",     S_TURRET        },
    { "PODS",       S_MISSLE_PODS   },
    { "AIRMISSLE",  S_AIR_MISSLE    },
    { "GRDMISSLE",  S_GRD_MISSLE    },
    { "BOMB",       S_BOMB          },
    { "ENERGYGRAB", S_ENERGY_GRAB   },
    { "MEGABOMB",   S_MEGA_BOMB     },
    { "PULSE",      S_PULSE_CANNON  },
    { "LASER",      S_FORWARD_LASER },
    { "DEATHRAY",   S_DEATH_RAY     },
};

#define WFIELD(f) offsetof(SHOT_LIB, f)

static const struct
{
    const char *name;
    size_t      off;
    int         art;              // TRUE = re-lock the sprites afterwards
} weapon_fields[] = {
    { "DAMAGE",     WFIELD(hits),       0 },
    { "RATE",       WFIELD(shoot_rate), 0 },
    { "SPEED",      WFIELD(speed),      0 },
    { "MAXSPEED",   WFIELD(maxspeed),   0 },
    { "SHADOW",     WFIELD(shadow),     0 },
    { "SMOKE",      WFIELD(smoke),      0 },
    { "DELAY",      WFIELD(delayflag),  0 },
    { "PLOT",       WFIELD(use_plot),   0 },
    { "MOVE",       WFIELD(move_flag),  0 },
    { "HITTYPE",    WFIELD(ht),         0 },
    { "FOLLOWX",    WFIELD(fplrx),      0 },
    { "FOLLOWY",    WFIELD(fplry),      0 },
    { "TRACK",      WFIELD(meffect),    0 },
    { "BEAM",       WFIELD(beam),       0 },
    { "LUMP",       WFIELD(lumpnum),    1 },
    { "FRAMES",     WFIELD(numframes),  1 },
    { "STARTFRAME", WFIELD(startframe), 1 },
};

#define NUM_WEAPON_NAMES  ((int)(sizeof(weapon_names) / sizeof(weapon_names[0])))
#define NUM_WEAPON_FIELDS ((int)(sizeof(weapon_fields) / sizeof(weapon_fields[0])))
#define MAX_SHOT_FRAMES   10      // SHOT_LIB::pic[10]

/***************************************************************************
NameEq () - ASCII case-insensitive compare. MSVC spells strcasecmp
_stricmp, so mod configs get their own rather than an ifdef per compiler.
 ***************************************************************************/
static int
NameEq(
    const char *a,
    const char *b
)
{
    for (; *a && *b; a++, b++)
    {
        int ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;

        if (ca != cb)
            return 0;
    }

    return *a == *b;
}

/***************************************************************************
RelinkShotArt () - Re-locks a weapon's frames after LUMP/FRAMES changed.

Returns FALSE and touches nothing if any frame is out of range: the caller
restores that weapon from stock rather than shipping dangling pointers.
 ***************************************************************************/
static int
RelinkShotArt(
    SHOT_LIB *slib
)
{
    char *pic[MAX_SHOT_FRAMES];
    int i;

    if (slib->numframes < 1)
        slib->numframes = 1;

    if (slib->numframes > MAX_SHOT_FRAMES)
        slib->numframes = MAX_SHOT_FRAMES;

    for (i = 0; i < slib->numframes; i++)
    {
        if (!GLB_ValidItem(slib->lumpnum + i))
            return 0;

        pic[i] = (char*)GLB_LockItem(slib->lumpnum + i);

        if (!pic[i])
            return 0;
    }

    for (i = 0; i < slib->numframes; i++)
        slib->pic[i] = pic[i];

    if (slib->startframe < 0 || slib->startframe >= slib->numframes)
        slib->startframe = 0;

    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;

    return 1;
}

/***************************************************************************
LoadWeaponConfig () - Applies WEAPONS_TXT over the stock shot table.
 ***************************************************************************/
static void
LoadWeaponConfig(
    void
)
{
    char line[64], wname[24], fname[24];
    char *data;
    int item, size, pos, len, value, w, f, i;
    int touched[LAST_WEAPON + 1];
    int applied = 0;

    item = GLB_GetItemID("WEAPONS_TXT");

    if (item == -1)
        return;

    size = GLB_ItemSize(item);
    data = GLB_GetItem(item);

    if (!data || size <= 0)
        return;

    memset(touched, 0, sizeof(touched));

    for (pos = 0; pos < size; )
    {
        for (len = 0; pos + len < size && data[pos + len] != NEWLINE_CH && len < (int)sizeof(line) - 1; len++)
            line[len] = data[pos + len];

        line[len] = 0;

        while (pos + len < size && data[pos + len] != NEWLINE_CH)
            len++;

        pos += len + 1;

        if (sscanf(line, "%23s %23s %d", wname, fname, &value) != 3)
            continue;

        for (w = 0; w < NUM_WEAPON_NAMES; w++)
            if (NameEq(wname, weapon_names[w].name))
                break;

        if (w == NUM_WEAPON_NAMES)
        {
            LOG_Printf("weapons config: unknown weapon '%s'", wname);
            continue;
        }

        for (f = 0; f < NUM_WEAPON_FIELDS; f++)
            if (NameEq(fname, weapon_fields[f].name))
                break;

        if (f == NUM_WEAPON_FIELDS)
        {
            LOG_Printf("weapons config: unknown field '%s'", fname);
            continue;
        }

        *(int*)((char*)&shot_lib[weapon_names[w].idx] + weapon_fields[f].off) = value;

        if (weapon_fields[f].art)
            touched[weapon_names[w].idx] = 1;

        applied++;
    }

    GLB_FreeItem(item);

    // re-lock sprites for any weapon whose art fields moved; a bad lump
    // number puts that one weapon back to stock and the rest stand
    for (i = 0; i <= LAST_WEAPON; i++)
    {
        if (!touched[i])
            continue;

        if (!RelinkShotArt(&shot_lib[i]))
        {
            LOG_Printf("weapons config: weapon %d has no item %d - reverted",
                i, shot_lib[i].lumpnum);
            shot_lib[i] = shot_lib_stock[i];
        }
    }

    if (applied)
        LOG_Printf("weapons config: %d override(s) applied", applied);
}

/***************************************************************************
SHOTS_LoadGunConfig () - Re-reads every moddable weapon setting.

Called at startup and again on every mod hot-apply, so it starts from the
stock table each time and lets whatever is mounted now write over it.
 ***************************************************************************/
void
SHOTS_LoadGunConfig(
    void
)
{
    if (shot_lib_saved)
        memcpy(shot_lib, shot_lib_stock, sizeof(shot_lib));

    LoadPlayerGun();
    LoadWeaponConfig();
}

/***************************************************************************
SHOTS_Init () - Inits SHOTS system and clears link list
 ***************************************************************************/
void 
SHOTS_Init(
    void
)
{
    int i, item;
    SHOT_LIB *slib;
    
    SHOTS_Clear();
    
    for (i = 0; i < 4; i++)
    {
        detpow[i] = (char*)GLB_LockItem(FILE139_DETHPOW_BLK + i);
    }
    
    for (i = 0; i < 4; i++)
    {
        laspow[i] = (char*)GLB_LockItem(FILE13d_LASERPOW_BLK + i);
    }
    
    for (i = 0; i < 4; i++)
    {
        lashit[i] = (char*)GLB_LockItem(FILE1f1_DRAYHIT_BLK + i);
    }
    
    memset(shot_lib, 0, sizeof(shot_lib));
    
    // == FORWARD_GUNS =====================================
    slib = &shot_lib[S_FORWARD_GUNS];
    slib->lumpnum = FILE1c3_NMSHOT_BLK;
    slib->shadow = 0;
    slib->type = S_FORWARD_GUNS;
    slib->hits = 1;
    slib->speed = 8;
    slib->maxspeed = 16;
    slib->startframe = 0;
    slib->numframes = 4;
    slib->delayflag = 0;
    slib->shoot_rate = 2;
    slib->cur_shoot = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_ALL;

    // == PLASMA_GUNS =====================================
    slib = &shot_lib[S_PLASMA_GUNS];
    slib->lumpnum = FILE1cf_PLASMA_BLK;
    slib->shadow = 0;
    slib->type = S_PLASMA_GUNS;
    slib->hits = 2;
    slib->speed = 4;
    slib->maxspeed = 8;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 10;
    slib->cur_shoot = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_AIR;

    // == MICRO_MISSLE =====================================
    slib = &shot_lib[S_MICRO_MISSLE];
    slib->lumpnum = FILE1c1_MICROM_BLK;
    slib->shadow = 0;
    slib->type = S_MICRO_MISSLE;
    slib->hits = 2;
    slib->speed = 2;
    slib->maxspeed = 8;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 4;
    slib->cur_shoot = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_GRALL;

    // == DUMB_MISSLE =====================================
    slib = &shot_lib[S_DUMB_MISSLE];
    slib->lumpnum = FILE1ba_MISDUM_BLK;
    slib->shadow = 1;
    slib->type = S_DUMB_MISSLE;
    slib->hits = 4;
    slib->speed = 2;
    slib->maxspeed = 12;
    slib->startframe = 1;
    slib->numframes = 3;
    slib->delayflag = 1;
    slib->shoot_rate = 10;
    slib->cur_shoot = 0;
    slib->use_plot = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_ALL;

    // == MINI_GUN =====================================
    slib = &shot_lib[S_MINI_GUN];
    slib->lumpnum = FILE1c3_NMSHOT_BLK;
    slib->shadow = 1;
    slib->type = S_MINI_GUN;
    slib->hits = 1;
    slib->speed = 8;
    slib->maxspeed = 10;
    slib->startframe = 1;
    slib->numframes = 4;
    slib->delayflag = 0;
    slib->shoot_rate = 1;
    slib->cur_shoot = 0;
    slib->use_plot = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_GRALL;

    // == LASER TURRET =====================================
    slib = &shot_lib[S_TURRET];
    slib->lumpnum = -1;
    slib->shadow = 0;
    slib->type = S_TURRET;
    slib->hits = 5;
    slib->speed = 0;
    slib->maxspeed = 0;
    slib->startframe = 0;
    slib->numframes = 0;
    slib->delayflag = 0;
    slib->shoot_rate = 6;
    slib->cur_shoot = 0;
    slib->use_plot = 0;
    slib->move_flag = 0;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_LINE;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    // slib->f_78 = slib->f_74->f_c >> 1;
    // slib->f_7c = slib->f_74->f_10 >> 1;
    slib->ht = S_ALL;

    // == MISSLE_PODS =====================================
    slib = &shot_lib[S_MISSLE_PODS];
    slib->lumpnum = FILE1bd_MISRAT_BLK;
    slib->shadow = 0;
    slib->type = S_MISSLE_PODS;
    slib->hits = 4;
    slib->speed = 1;
    slib->maxspeed = 16;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 5;
    slib->cur_shoot = 0;
    slib->smoke = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_AIR;

    // == AIR TO AIR =====================================
    slib = &shot_lib[S_AIR_MISSLE];
    slib->lumpnum = FILE1bd_MISRAT_BLK;
    slib->shadow = 0;
    slib->type = S_MISSLE_PODS;
    slib->hits = 4;
    slib->speed = 1;
    slib->maxspeed = 12;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 10;
    slib->cur_shoot = 0;
    slib->smoke = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_AIR;

    // == AIR TO GROUND =====================================
    slib = &shot_lib[S_GRD_MISSLE];
    slib->lumpnum = FILE1bf_MISGRD_BLK;
    slib->shadow = 0;
    slib->type = S_GRD_MISSLE;
    slib->hits = 20;
    slib->speed = 1;
    slib->maxspeed = 6;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 20;
    slib->cur_shoot = 0;
    slib->smoke = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_GROUND;

    // == GROUND BOMB =====================================
    slib = &shot_lib[S_BOMB];
    slib->lumpnum = FILE1d3_BLDGBOMB_PIC;
    slib->shadow = 0;
    slib->type = S_BOMB;
    slib->hits = 50;
    slib->speed = 1;
    slib->maxspeed = 4;
    slib->startframe = 0;
    slib->numframes = 1;
    slib->delayflag = 0;
    slib->shoot_rate = 30;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_GTILE;

    // == ENERGY GRAB =====================================
    slib = &shot_lib[S_ENERGY_GRAB];
    slib->lumpnum = FILE11d_POWDIS_BLK;
    slib->shadow = 0;
    slib->type = S_ENERGY_GRAB;
    slib->hits = 3;
    slib->speed = 4;
    slib->maxspeed = 8;
    slib->startframe = 0;
    slib->numframes = 6;
    slib->delayflag = 0;
    slib->shoot_rate = 2;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_SUCK;

    // == MEGA BOMB =====================================
    slib = &shot_lib[S_MEGA_BOMB];
    slib->lumpnum = FILE1cb_MEGABM_BLK;
    slib->shadow = 1;
    slib->type = S_MEGA_BOMB;
    slib->hits = 50;
    slib->speed = 2;
    slib->maxspeed = 2;
    slib->startframe = 0;
    slib->numframes = 4;
    slib->delayflag = 0;
    slib->shoot_rate = 60;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->use_plot = 1;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 1;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_ALL;

    // == PULSE CANNON =====================================
    slib = &shot_lib[S_PULSE_CANNON];
    slib->lumpnum = FILE123_SHOKWV_BLK;
    slib->shadow = 0;
    slib->type = S_PULSE_CANNON;
    slib->hits = 5;
    slib->speed = 8;
    slib->maxspeed = 8;
    slib->startframe = 0;
    slib->numframes = 2;
    slib->delayflag = 0;
    slib->shoot_rate = 3;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->use_plot = 0;
    slib->move_flag = 1;
    slib->fplrx = 0;
    slib->fplry = 0;
    slib->meffect = 0;
    slib->beam = S_SHOOT;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_ALL;

    // == FORWARD LASER =====================================
    slib = &shot_lib[S_FORWARD_LASER];
    slib->lumpnum = FILE135_FRNTLAS_BLK;
    slib->shadow = 0;
    slib->type = S_FORWARD_LASER;
    slib->hits = 10;
    slib->speed = 0;
    slib->maxspeed = 0;
    slib->startframe = 0;
    slib->numframes = 4;
    slib->delayflag = 0;
    slib->shoot_rate = 7;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->use_plot = 0;
    slib->move_flag = 0;
    slib->fplrx = 1;
    slib->fplry = 1;
    slib->meffect = 1;
    slib->beam = S_BEAM;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_AIR;

    // == DEATH RAY =====================================
    slib = &shot_lib[S_DEATH_RAY];
    slib->lumpnum = FILE131_DETHRY_BLK;
    slib->shadow = 0;
    slib->type = S_DEATH_RAY;
    slib->hits = 6;
    slib->speed = 0;
    slib->maxspeed = 0;
    slib->startframe = 0;
    slib->numframes = 4;
    slib->delayflag = 0;
    slib->shoot_rate = 7;
    slib->cur_shoot = 0;
    slib->smoke = 0;
    slib->use_plot = 0;
    slib->move_flag = 0;
    slib->fplrx = 1;
    slib->fplry = 1;
    slib->meffect = 1;
    slib->beam = S_BEAM;
    for (i = 0; i < slib->numframes; i++)
    {
        item = slib->lumpnum + i;
        slib->pic[i] = (char*)GLB_LockItem(item);
    }
    slib->h = (GFX_PIC*)slib->pic[0];
    slib->hlx = LE_LONG(slib->h->width) >> 1;
    slib->hly = LE_LONG(slib->h->height) >> 1;
    slib->ht = S_GRALL;

    memcpy(shot_lib_stock, shot_lib, sizeof(shot_lib));
    shot_lib_saved = 1;
}

/***************************************************************************
SHOTS_PlayerShoot() - Shoots the specified weapon
 ***************************************************************************/
int 
SHOTS_PlayerShoot(
    int type               // INPUT : OBJECT TYPE
)
{
    SHOT_LIB *lib;
    SHOTS *cur;
    SPRITE_SHIP *enemy;

    lib = &shot_lib[type];
    
    if (type == -1)
        EXIT_Error("SHOTS_PlayerShoot() type = EMPTY  ");
    
    if (lib->cur_shoot)
        return 0;
    
    lib->cur_shoot = lib->shoot_rate;
    
    cur = SHOTS_Get();
    
    if (!cur)
        return 0;
    
    switch (type)
    {
    default:
        EXIT_Error("SHOTS_PlayerShoot() - Invalid Shot type");
        break;
    
    case S_FORWARD_GUNS:
        if (!fx_gus)
            SND_Patch(FX_GUN, 127);
        g_flash = 7;

        if (playrgun.active)
        {
            int mz;

            lib->cur_shoot = playrgun.rate;

            for (mz = 0; mz < playrgun.count; mz++)
            {
                if (mz)
                {
                    cur = SHOTS_Get();

                    if (!cur)
                        return 0;
                }

                cur->curframe = (wrand() % lib->numframes);
                cur->lib = &shot_lib[type];
                cur->delayflag = lib->delayflag;
                cur->speed = lib->speed;
                cur->x = player_cx + playrgun.mx[mz];
                cur->y = player_cy + playrgun.my[mz];
                cur->move.x = cur->x;
                cur->move.y = cur->y;
                cur->move.x2 = cur->x;
                cur->move.y2 = 0;
                cur->startx = player_cx;
                cur->starty = player_cy;

                // one flash at the first muzzle - stacked barrels share it
                if (mz == 0)
                    ANIMS_StartAnim(A_PLAYER_SHOOT, playrgun.mx[0], playrgun.my[0]);
            }
            break;
        }

        cur->curframe = (wrand() % lib->numframes);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun1[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        ANIMS_StartAnim(A_PLAYER_SHOOT, o_gun1[playerpic], 0);
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->curframe = (wrand() % lib->numframes);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun1[playerpic] - 1;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        ANIMS_StartAnim(A_PLAYER_SHOOT, -o_gun1[playerpic] - 1, 0);
        break;
    
    case S_PLASMA_GUNS:
        if (!fx_gus)
            SND_Patch(FX_GUN, 127);
        cur->lib = &shot_lib[type];
        cur->curframe = wrand() % lib->numframes;
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_MICRO_MISSLE:
        if (!fx_gus)
            SND_Patch(FX_GUN, 127);
        g_flash = 7;
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun3[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun3[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_DUMB_MISSLE:
        if (!fx_gus)
            SND_Patch(FX_MISSLE, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x + (wrand() % 16) + 10;
        cur->move.y2 = cur->y + 5;
        cur->startx = player_cx;
        cur->starty = player_cy;
        InitMobj(&cur->move);
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x - (wrand() % 16) - 10;
        cur->move.y2 = cur->y + 5;
        cur->startx = player_cx;
        cur->starty = player_cy;
        InitMobj(&cur->move);
        break;
    
    case S_MINI_GUN:
        enemy = ENEMY_GetRandom();
        if (!enemy)
            SHOTS_Remove(cur);
        else
        {
            if (!fx_gus)
                SND_Patch(FX_GUN, 127);
            cur->curframe = wrand() % lib->numframes;
            cur->lib = &shot_lib[type];
            cur->delayflag = lib->delayflag;
            cur->speed = lib->speed;
            cur->x = player_cx;
            cur->y = player_cy;
            cur->move.x = cur->x;
            cur->move.y = cur->y;
            cur->move.x2 = (wrand() % enemy->width) + enemy->x - 1;
            cur->move.y2 = (wrand() % enemy->height) + enemy->hly + enemy->y - 1;
            cur->startx = player_cx;
            cur->starty = player_cy;
            InitMobj(&cur->move);
        }
        break;
    
    case S_TURRET:
        enemy = ENEMY_GetRandomAir();
        if (!enemy)
        {
            SHOTS_Remove(cur);
            SND_Patch(FX_NOSHOOT, 127);
        }
        else
        {
            SND_Patch(FX_TURRET, 127);
            cur->lib = &shot_lib[type];
            enemy->hits -= lib->hits;
            cur->curframe = 0;
            cur->delayflag = lib->delayflag;
            cur->speed = lib->speed;
            cur->x = player_cx;
            cur->y = player_cy;
            cur->move.x = (wrand() % enemy->width) + enemy->move.x - 1;
            cur->move.y = (wrand() % enemy->height) + enemy->move.y - 1;
            cur->move.x2 = player_cx;
            cur->move.y2 = player_cy;
            cur->startx = player_cx;
            cur->starty = player_cy;
            InitMobj(&cur->move);
            ANIMS_StartAnim(A_LASER_BLAST, cur->move.x, cur->move.y);
        }
        break;
    
    case S_MISSLE_PODS:
        SND_Patch(FX_GUN, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        ANIMS_StartAnim(A_PLAYER_SHOOT, o_gun2[playerpic], 1);
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        ANIMS_StartAnim(A_PLAYER_SHOOT, -o_gun2[playerpic] - 1, 1);
        break;
    
    case S_AIR_MISSLE:
        SND_Patch(FX_MISSLE, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_GRD_MISSLE:
        SND_Patch(FX_MISSLE, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun2[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_BOMB:
        SND_Patch(FX_MISSLE, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_ENERGY_GRAB:
        SND_Patch(FX_GUN, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - 4;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_MEGA_BOMB:
        SND_Patch(FX_GUN, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = 160;
        cur->move.y2 = 75;
        cur->startx = player_cx;
        cur->starty = player_cy;
        InitMobj(&cur->move);
        break;
    
    case S_PULSE_CANNON:
        SND_Patch(FX_PULSE, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = 0;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_FORWARD_LASER:
        SND_Patch(FX_LASER, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx + o_gun3[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = -24;
        cur->startx = player_cx;
        cur->starty = player_cy;
        
        cur = SHOTS_Get();
        if (!cur)
            return 0;
        
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx - o_gun3[playerpic];
        cur->y = player_cy;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->move.x2 = cur->x;
        cur->move.y2 = -24;
        cur->startx = player_cx;
        cur->starty = player_cy;
        break;
    
    case S_DEATH_RAY:
        SND_Patch(FX_LASER, 127);
        cur->lib = &shot_lib[type];
        cur->delayflag = lib->delayflag;
        cur->speed = lib->speed;
        cur->x = player_cx;
        cur->y = player_cy - 24;
        cur->move.x = cur->x;
        cur->move.y = cur->y;
        cur->startx = player_cx;
        cur->starty = player_cy;
        cur->move.x2 = cur->x;
        cur->move.y2 = -24;
        break;
    }
    
    return 1;
}

/***************************************************************************
SHOTS_Think () - Does All Thinking for shot system
 ***************************************************************************/
void 
SHOTS_Think(
    void
)
{
    SHOT_LIB *lib;
    SHOTS *shot;
    SPRITE_SHIP *enemy;
    int i;

    lib = shot_lib;
    for (i = 0; i <= LAST_WEAPON; i++, lib++)
    {
        if (lib->cur_shoot > 0)
            lib->cur_shoot--;
    }

    for (shot = first_shots.next; &last_shots != shot; shot = shot->next)
    {
        shot->ox = shot->x;          // snapshot for interpolation
        shot->oy = shot->y;

        lib = shot->lib;

        switch (lib->beam)
        {
        default:
            EXIT_Error("SHOTS_Think()");
            break;

        case S_SHOOT:
            shot->pic = lib->pic[shot->curframe];
            shot->x = shot->move.x - lib->hlx;
            if (lib->move_flag)
                shot->y = shot->move.y - lib->hly;
            else
                shot->y = shot->move.y;
            if (lib->smoke)
                ANIMS_StartAnim(A_SMALL_SMOKE_DOWN, shot->x + lib->hlx, shot->y + lib->hly * 2);
            break;
        
        case S_LINE:
            shot->x = shot->move.x;
            shot->y = shot->move.y;
            break;
        
        case S_BEAM:
            shot->pic = lib->pic[shot->curframe];
            shot->x = shot->move.x - lib->hlx;
            shot->y = shot->move.y;
            
            for (enemy = first_enemy.next; &last_enemy != enemy; enemy = enemy->next)
            {
                if (shot->x > enemy->x && shot->x < enemy->x2 && enemy->y < player_cy && enemy->y > -30)
                {
                    enemy->hits -= lib->hits;
                    
                    if (enemy->hits != -1)
                    {
                        shot->move.y2 = enemy->y + enemy->hly;
                        break;
                    }
                }
            }
            break;
        }
        
        if (lib->fplrx)
            shot->x += player_cx - shot->startx;
        
        if (lib->fplry)
            shot->y += player_cy - shot->starty;
        
        if ((shot->y + 16 < 0) || (shot->x < 0) || (shot->x > 320) || (shot->y > 200))
        {
            if (lib->move_flag)
            {
                shot->move.done = 1;
                goto shot_done;
            }
        }
        
        if (shot->delayflag == 0)
        {
            if (shot->speed < lib->maxspeed)
                shot->speed++;
            
            shot->curframe++;
            
            if (shot->curframe >= lib->numframes)
            {
                if (lib->move_flag)
                    shot->curframe = lib->startframe;
                else
                {
                    shot->move.done = 1;
                    goto shot_done;
                }
            }
        }
        
        if (shot->doneflag)
        {
            shot->move.done = 1;
            goto shot_done;
        }
        
        if (lib->meffect)
            goto shot_done;
        
        switch (lib->ht)
        {
        default:
            enemy = ENEMY_DamageEnergy(shot->x, shot->y, lib->hits);
            if (enemy)
            {
                shot->doneflag = 1;
                
                ANIMS_StartAnim(A_BLUE_SPARK, shot->x, shot->y);
                ANIMS_StartEAnim(enemy, A_ENERGY_GRAB, enemy->hlx, enemy->hly);
            }
            break;
        
        case S_GRALL:
            if (ENEMY_DamageAll(shot->x, shot->y, lib->hits))
            {
                shot->doneflag = 1;
                
                if ((wrand() % 2) != 0)
                    ANIMS_StartAnim(A_BLUE_SPARK, shot->x, shot->y);
                else
                    ANIMS_StartAnim(A_ORANGE_SPARK, shot->x, shot->y);
            }
            break;
        
        case S_ALL:
            if (ENEMY_DamageAll(shot->x, shot->y, lib->hits))
            {
                shot->doneflag = 1;
                
                if ((wrand() % 2) != 0)
                    ANIMS_StartAnim(A_BLUE_SPARK, shot->x, shot->y);
                else
                    ANIMS_StartAnim(A_ORANGE_SPARK, shot->x, shot->y);
            }
            else
            {
                if (TILE_IsHit(lib->hits, shot->x, shot->y))
                {
                    shot->move.done = 1;
                }
            }
            break;
        
        case S_AIR:
            if (ENEMY_DamageAir(shot->x, shot->y, lib->hits))
            {
                shot->doneflag = 1;
                
                if ((wrand() % 2) != 0)
                    ANIMS_StartAnim(A_BLUE_SPARK, shot->x, shot->y);
                else
                    ANIMS_StartAnim(A_ORANGE_SPARK, shot->x, shot->y);
            }
            break;
        
        case S_GROUND:
            if (ENEMY_DamageGround(shot->x, shot->y, lib->hits))
            {
                shot->doneflag = 1;
                
                ANIMS_StartAnim(A_ORANGE_SPARK, shot->x, shot->y);
            }
            else
            {
                if (TILE_IsHit(lib->hits, shot->x, shot->y))
                {
                    shot->move.done = 1;
                }
            }
            break;
        
        case S_GTILE:
            if (TILE_Bomb(lib->hits, shot->x, shot->y))
            {
                shot->move.done = 1;
            }
            if (ENEMY_DamageGround(shot->x, shot->y, 5))
            {
                shot->doneflag = 1;
                
                ANIMS_StartAnim(A_SMALL_GROUND_EXPLO, shot->x, shot->y);
            }
            break;
        }
    
    shot_done:
        
        if (shot->move.done)
        {
            if (shot->delayflag)
            {
                shot->delayflag = 0;
                shot->move.x2 = shot->move.x + ((wrand() % 32) - 16);
                shot->move.y2 = 0;
                ANIMS_StartAnim(A_SMALL_SMOKE_DOWN, shot->move.x, shot->move.y);
                InitMobj(&shot->move);
            }
            else
            {
                switch (lib->type)
                {
                case S_MEGA_BOMB:
                    ESHOT_Clear();
                    TILE_DamageAll();
                    for (enemy = first_enemy.next; &last_enemy != enemy; enemy = enemy->next)
                    {
                        enemy->hits -= lib->hits;
                    }
                    startfadeflag = 1;
                    ANIMS_StartAnim(A_SUPER_SHIELD, 0, 0);
                    shot = SHOTS_Remove(shot);
                    continue;
                
                case S_TURRET:
                    break;
                
                default:
                    shot = SHOTS_Remove(shot);
                    continue;
                }
            }
        }
        
        if (lib->move_flag)
        {
            if (lib->use_plot)
            {
                MoveSobj(&shot->move, shot->speed);
            }
            else
            {
                shot->move.y -= shot->speed;
                if (shot->move.y < 0)
                {
                    shot->move.done = 1;
                    shot->doneflag = 1;
                }
            }
        }
    }
}

/***************************************************************************
SHOTS_Display () - Displays All active Shots
 ***************************************************************************/
void 
SHOTS_Display(
    void
)
{
    int loop, x, y;
    SHOTS *shot;
    GFX_PIC *h;
    
    for (shot = first_shots.next; shot != &last_shots; shot = shot->next)
    {
        switch (shot->lib->beam)
        {
        default:
            EXIT_Error("SHOTS_Display()");
            break;
        
        case S_SHOOT:
            GFX_PutSprite(shot->pic, GFX_Lerp(shot->x, shot->ox), GFX_Lerp(shot->y, shot->oy));
            break;
        
        case S_LINE:
            GFX_Line(player_cx + 1, player_cy, shot->move.x, shot->move.y, 69);
            GFX_Line(player_cx - 1, player_cy, shot->move.x, shot->move.y, 69);
            GFX_Line(player_cx, player_cy, shot->move.x, shot->move.y, 64);
            if (g_commit)                       // remove spent line-shot once per logic frame
                shot = SHOTS_Remove(shot);
            break;
        
        case S_BEAM:
            for (loop = shot->move.y2; loop < shot->y; loop += 3)
            {
                GFX_PutSprite(shot->pic, shot->x, loop);
            }
            
            if (shot->lib->type == S_DEATH_RAY)
                GFX_PutSprite(detpow[shot->cnt], shot->x - 4, shot->y);
            else
                GFX_PutSprite(laspow[shot->cnt], shot->x, shot->y);
            
            h = (GFX_PIC*)lashit[shot->cnt];
            
            x = shot->x - (LE_LONG(h->width) >> 2);
            y = shot->move.y2 - 8;
            
            if (y > 0)
                GFX_PutSprite((char*)h, x, y);

            if (g_commit)                       // beam anim counter: once per logic frame
            {
                shot->cnt++;
                shot->cnt = shot->cnt % 4;
            }
            break;
        }
    }
}

/***************************************************************************
SHOTS_GetExhaust () - mod-configured engine flame offsets, 0 = use stock
 ***************************************************************************/
int
SHOTS_GetExhaust(
    int *dx,
    int *dy
)
{
    if (!playrgun.active || !playrgun.exhaust_set)
        return 0;

    *dx = playrgun.exhaust_dx;
    *dy = playrgun.exhaust_dy;

    return 1;
}

/***************************************************************************
SHOTS_GetSpeedPct () - mod-configured player speed, percent of stock
 ***************************************************************************/
int
SHOTS_GetSpeedPct(
    void
)
{
    return playrgun.active ? playrgun.speed_pct : 100;
}

/***************************************************************************
SHOTS_ScaleDamage () - applies mod armor: damage scaled by 100/ARMOR,
with a running remainder so chip damage still accumulates
 ***************************************************************************/
int
SHOTS_ScaleDamage(
    int amt
)
{
    int take;

    if (!playrgun.active || playrgun.armor_pct == 100 || amt <= 0)
        return amt;

    playrgun.armor_acc += amt * 100;
    take = playrgun.armor_acc / playrgun.armor_pct;
    playrgun.armor_acc -= take * playrgun.armor_pct;

    return take;
}
