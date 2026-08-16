---
date: 2026-08-16T21:21:46.597339+00:00
outcome: REVIEW_FAILED
turns: 0
tags: [handshake]
---

# Review this patch adding a mod system to raptor-enhanced (GPL-2 C++17 port of Raptor: Call of the Shadows, 1994 codebase

**Outcome:** REVIEW_FAILED (exit 1)

## Task

Review this patch adding a mod system to raptor-enhanced (GPL-2 C++17 port of Raptor: Call of the Shadows, 1994 codebase style). The patch is a git-show of one commit limited to source files; the full repo is not included, so judge the changed/new code on its own terms. Architecture facts you should trust: (1) GLB item handles are (filenum<<16)|itemnum; base archives mount at filenums 0-5 and stay stable; GLB_MountPath appends; MAX_GLB_FILES is 15. (2) All item fetches funnel through the hooked entry points shown (GLB_FetchItem covers GetItem/LockItem/CacheItem). (3) MOD_ApplyPending is only called at documented safe points where no window holds item locks (startup, and the main-menu teardown/rebuild shown in windows.cpp). (4) SWD window data format: SWIN header 120 bytes + SFIELD32 records 148 bytes + text area; per-field txtoff is relative to the field record's own start; the MAIN_SWD runtime patch mirrors a battle-tested installer transform. (5) The engine is single-threaded. (6) INI_* functions and LOG_Printf exist and work. Focus on: correctness bugs in the new modapi.cpp/glbapi additions, the MAIN_SWD byte patch, lifetime/lock-safety of hot-apply, the manifest parser, and the python asset builder. Do not fail for style matching the 1994 codebase, for the intentional design of keeping disabled mods mounted, or for findings requiring the rest of the repo to verify.

## Turn 1
### Review

**FAIL** (confidence 0.97)

**Blocking:**
- modapi.cpp: MOD_Resolve() silently stops after four redirects. With MOD_MAX=8, valid alias chains can exceed four steps, so the returned handle can be an intermediate item and later overrides will not apply. Traverse until no mapping remains, with cycle detection or a bound derived from overrides.size().
- modapi.cpp: MOD_FilterWindowData() caches the first patched MAIN_SWD forever. After hot-applying a mod that enables, disables, or replaces MAIN_SWD, rebuilding the menu still returns the stale cached window. Cache by the resolved handle/override generation and rebuild the cached buffer when that value changes.

**Non-blocking:**
- Conflicting addition-only item names are resolved by archive mount order, while normal overrides use sorted mod order with later mods winning. Because disabled mods mount lazily, the winner can depend on enable history.
- Disabled, never-mounted mods display their filename stem rather than the MODINFO_TXT display name until first enabled.
- The std::transform call should include <cctype> and pass unsigned char values to std::tolower to avoid undefined behavior for non-ASCII path bytes.

## Issue ledger

- `OPEN` (turns 1–1) modapi.cpp: MOD_Resolve() silently stops after four redirects. With MOD_MAX=8, valid alias chains can exceed four steps, so the returned handle can be an intermediate item and later overrides will not apply. Traverse until no mapping remains, with cycle detection or a bound derived from overrides.size().
- `OPEN` (turns 1–1) modapi.cpp: MOD_FilterWindowData() caches the first patched MAIN_SWD forever. After hot-applying a mod that enables, disables, or replaces MAIN_SWD, rebuilding the menu still returns the stale cached window. Cache by the resolved handle/override generation and rebuild the cached buffer when that value changes.
