#ifdef __PSP__
#include <pspuser.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <SDL2/SDL.h>

// Generic file copy function (kept for parity with arm.h / other console
// ports, even though the PSP branch below doesn't currently need it since
// there's no separate romfs archive to pull a default INI from).
int cp(const char *to, const char *from);

// Init the target system. On PSP, SDL2's own PSP main() already brings up
// the GU/GPU and starts the exit-callback thread before your main() runs,
// so this hook is here mainly for parity with arm.h and any PSP-specific
// setup you want to add later (e.g. sceKernelDevkitVersion-based memory
// tricks, sceUtilityMsgDialog, etc).
void sys_init();

//#define access checkFile

// Raptor ships as a flat folder next to EBOOT.PBP on the memory stick
// (unlike 3DS/Switch, which mount a separate romfs archive read-only
// alongside a writable SD path). So there's just one directory, addressed
// with a plain relative path, and ROMFS/RAP_SD_DIR collapse to the same
// thing here.
#define ROMFS ""
#define RAP_SD_DIR ""

// NOTE: unlike arm.h, this does NOT #define access as something else.
// PSPSDK's newlib provides a working access(), so the stock POSIX call
// should just work -- but verify this on real hardware/PPSSPP before
// relying on it, since some PSPSDK versions have had gaps in libc coverage.

#endif // __PSP__
