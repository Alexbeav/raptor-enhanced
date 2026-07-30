#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

void LOG_Printf(const char *fmt, ...);

#ifdef __ARM__
#include "arm.h"
#elif XBOX
#include "xbox.h"
#elif __PSP__
#include "mips.h"
#endif

static inline void EXIT_Error(const char *a1, ...)
{
	#if defined(__ARM__) || defined(XBOX)
	exit(0);
	#else
    char msg[1024];
    va_list args;

    va_start(args, a1);
    vsnprintf(msg, sizeof(msg), a1, args);
    va_end(args);

    LOG_Printf("FATAL: %s", msg);
    fprintf(stderr, "FATAL: %s\n", msg);
    exit(1);
    #endif
}

static inline void EXIT_Clean(void)
{
    exit(0);
}

static inline void EXIT_Install(void (*a1)(int a1))
{

}
