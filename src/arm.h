#ifdef __ARM__
#ifdef __3DS__
#include <SDL2/SDL.h>
#include <3ds.h>
#elif __SWITCH__
#include <SDL2/SDL.h>
#include <switch.h>
#endif

//Generic file copy function.
int cp(const char *to, const char *from);

//Init the target system
void sys_init();

#define access checkFile

#ifdef __3DS__
#define ROMFS "romfs:/"
#define SDMC "sdmc:/"
#define RAP_SD_DIR SDMC "3ds/Raptor/"
#elif __SWITCH__
#define ROMFS "romfs:/"
#define SDMC "sdmc:/"
#define RAP_SD_DIR SDMC "switch/Raptor/"
#endif
#endif