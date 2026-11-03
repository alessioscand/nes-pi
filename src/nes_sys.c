/*
 * nes-pi - what newlib and libstdc++ need from the "operating system"
 *
 * Nestopia uses the C++ standard library (string streams, containers,
 * exceptions), which sits on newlib. There is no operating system: memory
 * comes from Circle's allocator, there are no files (games and saves go
 * through FatFs in the frontend) and nothing is printed.
 */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

struct _reent;

void *malloc (size_t nSize);
void *memalign (size_t nAlign, size_t nSize);
void free (void *pBlock);
void *calloc (size_t nBlocks, size_t nSize);
void *realloc (void *pBlock, size_t nSize);

// newlib's reentrant allocator entry points: Circle's heap, one allocator only
void *_malloc_r (struct _reent *r, size_t n)			{ (void) r; return malloc (n); }
void _free_r (struct _reent *r, void *p)			{ (void) r; free (p); }
void *_calloc_r (struct _reent *r, size_t a, size_t b)		{ (void) r; return calloc (a, b); }
void *_realloc_r (struct _reent *r, void *p, size_t n)		{ (void) r; return realloc (p, n); }
void *_memalign_r (struct _reent *r, size_t a, size_t n)	{ (void) r; return memalign (a, n); }

// no break area: everything goes through the functions above
void *_sbrk (ptrdiff_t nIncrement)
{
	(void) nIncrement;
	errno = ENOMEM;
	return (void *) -1;
}

// no files, no console
int _write (int nFile, const void *pBuffer, int nCount)	{ (void) nFile; (void) pBuffer; return nCount; }
int _read (int nFile, void *pBuffer, int nCount)	{ (void) nFile; (void) pBuffer; (void) nCount; return 0; }
int _close (int nFile)					{ (void) nFile; return -1; }
int _lseek (int nFile, int nOffset, int nWhence)	{ (void) nFile; (void) nOffset; (void) nWhence; return 0; }
int _isatty (int nFile)					{ (void) nFile; return 1; }
int _fstat (int nFile, struct stat *pStat)
{
	(void) nFile;
	pStat->st_mode = S_IFCHR;
	return 0;
}
int _getpid (void)					{ return 1; }
int _kill (int nPid, int nSignal)			{ (void) nPid; (void) nSignal; errno = EINVAL; return -1; }
void _exit (int nStatus)
{
	(void) nStatus;
	for (;;)
	{
		__asm__ volatile ("wfi");
	}
}

// the C++ runtime's constructors and destructors run through Circle's start-up
void _init (void) {}
void _fini (void) {}

// no clock for newlib's time(): nothing in nes-pi asks it for the date
struct timeval;
int _gettimeofday (struct timeval *pTv, void *pTz)	{ (void) pTv; (void) pTz; errno = ENOSYS; return -1; }

// libstdc++ asks newlib for its reentrant number parsers; the plain ones are
// Circle's: answering here keeps newlib's copies (which clash) out of the link
unsigned long strtoul (const char *pString, char **ppEnd, int nBase);
unsigned long long strtoull (const char *pString, char **ppEnd, int nBase);
unsigned long _strtoul_r (struct _reent *r, const char *s, char **e, int b)		{ (void) r; return strtoul (s, e, b); }
unsigned long long _strtoull_r (struct _reent *r, const char *s, char **e, int b)	{ (void) r; return strtoull (s, e, b); }
unsigned long strtoul_l (const char *s, char **e, int b, void *l)			{ (void) l; return strtoul (s, e, b); }
unsigned long long strtoull_l (const char *s, char **e, int b, void *l)			{ (void) l; return strtoull (s, e, b); }

// no files to open, no entropy source asked for
int _open (const char *pName, int nFlags, int nMode)	{ (void) pName; (void) nFlags; (void) nMode; errno = ENOENT; return -1; }
int _getentropy (void *pBuffer, size_t nLength)		{ (void) pBuffer; (void) nLength; errno = ENOSYS; return -1; }
