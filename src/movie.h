#pragma once

typedef enum
{
    M_ANIM,
    M_PIC,
    M_SEE_THRU
}FRAMETYPE;

typedef enum
{
    M_NORM,
    M_FADEIN,
    M_FADEOUT,
    M_PALETTE,
    M_ERASE
}FRAMEOPT;

typedef enum
{
    S_PLAY,
    S_FADEIN,
    S_FADEOUT,
    S_STOP
}SONGOPTS;

// ANIM data streams place ANIMLINE records at odd offsets (the frame's fill
// byte shifts everything by 1, and each record is followed by an arbitrary
// byte-length run). packed makes GCC emit unaligned-safe loads; the PSP's
// Allegrex CPU raises an address-error exception on misaligned 16/32-bit
// access, unlike x86/ARM/PPSSPP which tolerate it.
#if defined(__PSP__)
typedef struct __attribute__((packed))
#else
typedef struct
#endif
{
    unsigned short opt;
    unsigned short fill;
    unsigned short offset;
    unsigned short length;
}ANIMLINE;

typedef struct
{
    int opt;                   // TYPE OF DRAWING REQUIRED
    int framerate;             // FRAME RATE OF UPDATE
    int numframes;             // NUMBER OF FRAMES LEFT
    int item;                  // ITEM # OF PICTURE
    int startf;                // START FRAME OPTS
    int startsteps;            // # OF STEPS IF FADEIN
    int endf;                  // END FRAME OPTS
    int endsteps;              // # OF STEPS IN FADEOUT
    int red;                   // RED VAL FOR FADEOUT
    int green;                 // GREEN VAL FOR FADEOUT
    int blue;                  // BLUE VAL FOR FADEOUT
    int holdframe;             // NUMBER OF TICS TO HOLD FRAME
    int songid;                // SONG ID TO PLAY
    int songopt;               // SONG OPTS
    int songstep;              // SONG STEPS FOR FADES
    int soundfx;               // SOUND FX START
    int fx_vol;                // SOUND FX VOLUME
    int fx_xpos;               // SOUND FX XPOS
}FRAME;

void ANIM_Render(ANIMLINE *inmem);
void MOVIE_BPatch(int soundfx);
int MOVIE_Play(FRAME *frame, int numplay, char *palette);
