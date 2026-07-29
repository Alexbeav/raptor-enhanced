//
// Selectable OPL FM core for the music/Adlib-SFX synth.
//
// Two emulators are compiled in by default:
//   - Nuked OPL3 (opl3.cpp)  - cycle-accurate, the default
//   - DOSBox dbopl (dbopl.cpp) - much cheaper on low-end CPUs
//
// Runtime selection: SETUP.INI [Music] OplEmu=0 (Nuked) / 1 (DBOPL).
// Ports can shed a core entirely at compile time by defining
// OPL_NO_NUKED or OPL_NO_DBOPL and dropping the matching source file.
//

#ifndef OPL_CORE_H
#define OPL_CORE_H

#include <stdint.h>

#if defined(OPL_NO_NUKED) && defined(OPL_NO_DBOPL)
#error "At least one OPL core must be compiled in"
#endif

void OPLCore_Init(uint32_t samplerate);
void OPLCore_WriteReg(uint16_t reg, uint8_t val);
void OPLCore_Generate(int16_t *stream, int numframes);

#endif // OPL_CORE_H
