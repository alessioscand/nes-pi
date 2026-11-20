/*
 * Between cores: shared variables, events, the ARM timer. Aligned accesses
 * up to 64 bits are atomic; SharedLoad / SharedStore stop the compiler from
 * caching them, SharedAcquire / SharedRelease order what they publish.
 */
#ifndef SMP_H
#define SMP_H

#include <circle/types.h>

template <typename T> static inline T SharedLoad (const T &rVar)
{
	return __atomic_load_n (&rVar, __ATOMIC_RELAXED);
}

template <typename T> static inline void SharedStore (T &rVar, T Value)
{
	__atomic_store_n (&rVar, Value, __ATOMIC_RELAXED);
}

template <typename T> static inline T SharedAcquire (const T &rVar)
{
	return __atomic_load_n (&rVar, __ATOMIC_ACQUIRE);
}

template <typename T> static inline void SharedRelease (T &rVar, T Value)
{
	__atomic_store_n (&rVar, Value, __ATOMIC_RELEASE);
}

// stores visible to the other cores, then wake those waiting in WaitEvent()
static inline void SignalEvent (void)
{
	__asm__ volatile ("dsb ish; sev" ::: "memory");
}

static inline void WaitEvent (void)
{
	__asm__ volatile ("wfe");
}

// ARM generic timer
static inline u64 Ticks (void)
{
	u64 v;
	__asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r" (v));
	return v;
}

static inline u64 TicksFreq (void)
{
	u64 v;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
	return v;
}

#endif
