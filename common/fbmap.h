// fbmap.h - the framebuffer as write-combining memory
//
// Circle maps it as Device-nGnRE (one bus write per store); as Linux, we map
// its pages Normal Non-cacheable (stores merged into bursts) through
// MAIR_EL1 index 3, unused by Circle, at run time.

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
