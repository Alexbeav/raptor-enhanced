//
// Selectable OPL FM core: Nuked OPL3 (accurate) or DOSBox dbopl (fast).
// See opl_core.h. All calls arrive on the SDL audio callback thread
// (MUS_Mix interleaves register writes and generation frame by frame),
// so neither core needs any locking here.
//

#include "opl_core.h"
#include "prefapi.h"

#ifndef OPL_NO_NUKED
#include "opl3.h"
static opl3_chip nuked_chip;
#endif

#ifndef OPL_NO_DBOPL
#include "dbopl.h"
static DBOPL::Handler dbopl_chip;
#endif

enum
{
    OPL_CORE_NUKED = 0,
    OPL_CORE_DBOPL = 1,
};

static int opl_core = OPL_CORE_NUKED;

void OPLCore_Init(uint32_t samplerate)
{
    opl_core = INI_GetPreferenceLong("Music", "OplEmu", 0) != 0
        ? OPL_CORE_DBOPL : OPL_CORE_NUKED;

#ifdef OPL_NO_DBOPL
    opl_core = OPL_CORE_NUKED;
#endif
#ifdef OPL_NO_NUKED
    opl_core = OPL_CORE_DBOPL;
#endif

#ifndef OPL_NO_NUKED
    if (opl_core == OPL_CORE_NUKED)
        OPL3_Reset(&nuked_chip, samplerate);
#endif
#ifndef OPL_NO_DBOPL
    if (opl_core == OPL_CORE_DBOPL)
        dbopl_chip.Init(samplerate);
#endif
}

void OPLCore_WriteReg(uint16_t reg, uint8_t val)
{
#ifndef OPL_NO_NUKED
    if (opl_core == OPL_CORE_NUKED)
    {
        OPL3_WriteRegBuffered(&nuked_chip, reg, val);
        return;
    }
#endif
#ifndef OPL_NO_DBOPL
    dbopl_chip.WriteReg(reg, val);
#endif
}

void OPLCore_Generate(int16_t *stream, int numframes)
{
#ifndef OPL_NO_NUKED
    if (opl_core == OPL_CORE_NUKED)
    {
        OPL3_GenerateStream(&nuked_chip, stream, numframes);
        return;
    }
#endif
#ifndef OPL_NO_DBOPL
    // dbopl emits 32-bit samples: stereo pairs in OPL3 mode, mono before
    // the NEW register is set during init. Clamp to 16-bit either way.
    for (int i = 0; i < numframes; i++)
    {
        Bit32s frame[2];

        dbopl_chip.Generate(frame, 1);
        if (!dbopl_chip.chip.opl3Active)
            frame[1] = frame[0];

        for (int ch = 0; ch < 2; ch++)
        {
            Bit32s s = frame[ch];
            if (s > 32767)
                s = 32767;
            else if (s < -32768)
                s = -32768;
            stream[i * 2 + ch] = (int16_t)s;
        }
    }
#endif
}
