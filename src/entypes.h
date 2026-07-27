#pragma once

#if defined (__3DS__) || defined (__SWITCH__)
#include "SDL2/SDL_endian.h"
#else
#include "SDL_endian.h"
#endif

#define LE_USHORT(x) SDL_SwapLE16(x)
#define LE_SHORT(x) (signed short) SDL_SwapLE16(x)
#define LE_ULONG(x) SDL_SwapLE32(x)
#define LE_LONG(x) (signed int) SDL_SwapLE32(x)