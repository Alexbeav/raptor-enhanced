# Raptor Enhanced on the PSP — port notes

Field notes from bringing the PSP port up on real hardware (a PSP-1004,
firmware 6.60), July 2026. The port ran fine in PPSSPP throughout; every
problem below only existed on the real machine. Written down so the next
console port doesn't have to rediscover any of it.

## Why it worked in PPSSPP and died on hardware

PPSSPP is not a strict Allegrex. Three differences bit us, in order of pain:

**1. Unaligned memory access.** The real Allegrex raises an address-error
exception on any misaligned 16/32-bit load or store; PPSSPP just performs
the access, because the host CPU doesn't care. Raptor's data formats
guarantee misalignment: an ANIM frame starts with a 1-byte fill color, so
every `ANIMLINE` record after it sits at an odd address, and sprite data
walks `GFX_PIC`/`GFX_SPRITE` headers forward by arbitrary byte-length
pixel runs. Result on hardware: hard freeze on the first intro animation
frame (and it would have crashed again on the first in-game sprite).

Fix: `__attribute__((packed))` on those three structs under `__PSP__`
(movie.h, gfxapi.h) — the same trick the ARM port already used for
`GFX_SPRITE`. GCC then emits `lbu` pairs and `lwl`/`lwr`, which the
Allegrex supports. Verified with `psp-objdump -d`. If you add a new
struct that overlays byte-stream data, it needs the same treatment.

**2. Memory size.** PPSSPP defaults to emulating a slim (64 MB); a
PSP-1000 has 32 MB with ~24 MB usable. Didn't bite this game (per-item
GLB allocation, freed after use), but set PPSSPP's PSP model to PSP-1000
if you want the emulator to be honest about it.

**3. Speed.** PPSSPP on a PC runs guest code far faster than 333 MHz.
Two consequences: an unoptimized build feels fine in the emulator and
slows down on hardware, and the smooth-motion mode (below) can't be
performance-tested in PPSSPP at all.

## The performance cliff was the compiler

`Makefile.psp` originally had no `-O` flag — the whole game, including
the OPL FM synthesizer running continuously on the audio thread, was
compiled at `-O0`. That alone was the difference between "slows down when
enemies fill the screen" and "flies". Now:

```
CFLAGS = -O2 -G0 -fno-strict-aliasing ...
```

`-O2 -G0` is the PSPSDK convention. `-fno-strict-aliasing` matters
because this codebase overlays structs on char buffers everywhere; don't
let the optimizer assume those never alias. The CPU is also clocked to
333 MHz at startup (`scePowerSetClockFrequency(333, 333, 166)`, needs
`-lpsppower`) — the default 222 MHz leaves nothing in reserve for the
audio thread plus smooth motion.

Related: `-O2` exposed real undefined behavior in `SaveRead32()`, which
assembled bytes through a signed `int` (`SaveRead8() << 24` shifts into
the sign bit). Assemble through `uint32_t`.

## Sound

Symptoms: logos played silently. RAPTOR.LOG showed the SDL audio device
opened fine at 22050 Hz — the game had simply read `CardType 0` (no
sound hardware) from a stale SETUP.INI on the memory stick. The INI layer
materializes defaults back into the file, so once a bad value lands
there, it sticks. The 3DS/Switch/Xbox ports hardcode the SoundBlaster
card and ignore the INI; the PSP now does the same. Rule of thumb for
console ports of this codebase: never trust SETUP.INI for anything a
player can't reach from inside the game.

SDL2's PSP audio backend accepts 44100 Hz on a plain hardware channel and
other rates via the sceAudioSRC channel; 22050/512-sample stereo works on
real hardware. If an exotic rate ever fails, the PSP build retries at
44100 before giving up.

The soundtrack is the Adlib/OPL arrangement (DBOPL core — integer
runtime, fine at 333 MHz; Nuked was deliberately not used). General MIDI
via TinySoundFont would need a second audio path and real RAM/CPU;
untested on PSP.

## Smooth motion on a 33 MHz-class budget

The enhanced port's smooth-motion mode redraws and presents interpolated
sub-frames between 35 Hz logic ticks. Hardware verdicts from the 1004:

| Config | Result |
|---|---|
| 3 sub-frames, -O0 | constant slow motion |
| 2 sub-frames, -O0 | slows down under load |
| 1 (classic), -O2 | flies |
| 2 sub-frames, -O2 | good; minor drops on High detail, smooth on Low |

So: `SMOOTH=1` (default) builds 2 sub-frames on PSP, `SMOOTH=0` builds
classic single-present, and both prebuilt EBOOTs live in `pkg/psp/`.
The renderer doesn't vsync, so sub-frames cost GPU/CPU time only.

Enabling smooth mode also exposed an engine bug relevant to every
platform: `RAP_DisplayStats()` ran once per *sub-frame* but mutates real
state (death explosions and sounds, the low-shield blink/damage drain,
end-of-wave fly-off movement, `g_oldshield`). Those now sit behind
`g_commit`, the once-per-logic-frame flag the enemy/shots/tile code
already used. If you see doubled effects in smooth mode on another
platform, look for the same pattern.

## Input: pointer + menu keys at the same time

Raptor's UI is mouse-first, and the PSP has both a pointing device (the
nub) and a D-pad — so the port runs both input styles simultaneously
instead of picking one (`joy_menu_keys=1` with `joy_ipt_MenuNew=0`; on
every other platform `joy_menu_keys` simply mirrors `joy_ipt_MenuNew`,
so nothing changes there):

- Nub moves the drawn cursor; Cross clicks what it's over.
- D-pad navigates menu fields and cycles letters in the pilot name entry
  (Up/Down letter, Cross next char, Square backspace, Triangle space,
  Start confirm). The stick is gated *out* of all arrow synthesis in
  this mode — otherwise the nub double-acts as arrows.
- D-pad navigation snaps the cursor onto the selected field
  (`SWD_SetWindowPtr` / `SWD_SetFieldPtr`, which needed the window
  origin added — field coords are window-relative), so hover and
  highlight can never disagree and Cross is never ambiguous.
- Cross is *not* a click while a text-input field is active (it means
  "next character" there), and over empty space it selects the
  highlighted field.
- `JOY_Wait`/`JOY_IsKey` ignore the stick in this mode; they otherwise
  spin until *all* inputs are neutral, which froze menus while the nub
  was held.

`-DNOCURSOR` in the Makefiles is vestigial — nothing in the tree
references it. The cursor on/off logic lives in `ptrapi.cpp` platform
guards and the flags above.

## Diagnostics on hardware

There's no stdout on a PSP without psplink. The port writes `RAPTOR.LOG`
next to `EBOOT.PBP` (`LOG_Init` previously did nothing on PSP because
`cdflag` stays 0) with the sound card selection, SDL audio open results,
and a breadcrumb per cutscene. When something misbehaves on hardware,
that file is the first thing to read — it's how the silent-audio cause
was found in one round trip.

Also fixed while in there: the PSP render path called
`SDL_SetRenderTarget(renderer, texture)` on a `STREAMING` texture, which
always fails (only `TARGET` textures can be render targets) and only
worked because the failure left the backbuffer targeted. It now targets
`NULL` explicitly.

## Building

No local toolchain needed — the official Docker image has PSPSDK and
SDL2 preinstalled:

```
docker run --rm -v "$(pwd):/build" pspdev/pspdev:latest \
  bash -c "cd /build && make -f Makefile.psp SMOOTH=1"
```

`build.mak` has no header dependency tracking: run
`make -f Makefile.psp clean` after touching any header, or stale objects
will silently ship without your changes.

XMB art is packed automatically (`PSP_EBOOT_ICON`/`PSP_EBOOT_PIC1` in
Makefile.psp): ICON0 is the 160x80 banner letterboxed to the required
144x80, PIC1 (480x272) was composed from the game's own RAPLOG sprite
over the BACKGRND menu art, extracted with `tools/glb_extract.py`.
