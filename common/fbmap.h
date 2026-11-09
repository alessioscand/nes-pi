// fbmap.h - map the framebuffer as write-combining memory
//
// Circle maps all GPU memory (framebuffer included) as Device-nGnRE: every
// store goes out to the bus on its own. Linux maps framebuffers as
// Normal Non-cacheable instead, so the core merges the stores into bursts.
// This changes only the pages of our framebuffer, at run time, without
// touching Circle (shared with NES-Pi): MAIR_EL1 index 3 (unused by Circle)
// becomes Normal Non-cacheable and the framebuffer pages point to it.

#ifndef _fbmap_h
#define _fbmap_h

#include <circle/types.h>


// MAIR_EL1 index 3 = Normal Non-cacheable; every core that draws calls it once
void FbSetMair (void);

// the pages of [nBase, nBase + nSize) use index 3; returns the pages changed
unsigned FbWriteCombine (uintptr nBase, size_t nSize);

// stores to the framebuffer reach memory (before the HVS reads them)
static inline void FbFlush (void)
{
	__asm__ volatile ("dsb st" ::: "memory");
}

#endif
