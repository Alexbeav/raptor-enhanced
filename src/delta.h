//
// Delta Sector synthesis: builds the community 4th campaign's nine maps
// in memory from the player's own base data, following the splice recipe
// a mod ships (DELTARCP_TXT). Behavioral contract: byte-identical output
// to pkg/release/DeltaSector-optional/install_delta_sector.py, so a
// synthesized campaign matches a file-installed one exactly.
//

#ifndef DELTA_H
#define DELTA_H

// Parses the recipe text, splices the nine MAPnG4_MAP items from base
// archives [0, base_files) and mounts them as one memory archive; the
// optional ending text (adopted, freed on failure) is mounted alongside
// as END4_TXT. reuse_filenum -1 mounts a new archive; otherwise the
// existing memory archive's contents are replaced (safe points only -
// used when a different recipe mod takes over on hot-apply). Returns
// the archive's filenum, or -1 on any failure (logged).
int DELTA_Synthesize(const char *recipe, int recipe_len, int base_files,
                     char *end_text, int end_len, int reuse_filenum);

#endif // DELTA_H
