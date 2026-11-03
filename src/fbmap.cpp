// fbmap.cpp - map the framebuffer as write-combining memory (see fbmap.h)

#include "fbmap.h"
#include <circle/bcm2835.h>

#define ATTRINDX_WC	3
#define MAIR_WC		0x44ULL		// Normal, inner/outer Non-cacheable

void FbSetMair (void)
{
	u64 nMair;
	__asm__ volatile ("mrs %0, mair_el1" : "=r" (nMair));
	nMair &= ~(0xFFULL << (ATTRINDX_WC * 8));
	nMair |= MAIR_WC << (ATTRINDX_WC * 8);
	__asm__ volatile ("msr mair_el1, %0; isb" : : "r" (nMair) : "memory");
}

unsigned FbWriteCombine (uintptr nBase, size_t nSize)
{
	if (nBase == 0 || nSize == 0)
	{
		return 0;
	}

	// only GPU memory below the peripherals (never a device register)
	if (nBase + nSize > ARM_IO_BASE || nBase + nSize > 0x40000000UL)
	{
		return 0;
	}

	FbSetMair ();

	// Circle: one level 2 table (512 MB entries) -> level 3 tables (64 KB pages)
	u64 nTTBR;
	__asm__ volatile ("mrs %0, ttbr0_el1" : "=r" (nTTBR));
	u64 *pLevel2 = (u64 *) (uintptr) (nTTBR & 0x0000FFFFFFFFFFC0ULL);

	unsigned nPages = 0;
	for (uintptr nAddr = nBase & ~(uintptr) 0xFFFF; nAddr < nBase + nSize; nAddr += 0x10000)
	{
		u64 nDesc2 = pLevel2[nAddr >> 29];
		if ((nDesc2 & 3) != 3)			// not a table descriptor
		{
			break;
		}
		u64 *pLevel3 = (u64 *) (uintptr) (nDesc2 & 0x0000FFFFFFFF0000ULL);
		u64 *pDesc = &pLevel3[(nAddr >> 16) & 0x1FFF];
		if ((*pDesc & 3) != 3)			// not a page descriptor
		{
			break;
		}

		u64 nDesc = *pDesc;
		nDesc &= ~(7ULL << 2);			// AttrIndx [4:2]
		nDesc |= (u64) ATTRINDX_WC << 2;
		*pDesc = nDesc;
		__asm__ volatile ("dc civac, %0" : : "r" (pDesc) : "memory");
		nPages++;
	}

	__asm__ volatile (
		"dsb ish\n"
		"tlbi vmalle1is\n"
		"dsb ish\n"
		"isb\n" ::: "memory");

	return nPages;
}
