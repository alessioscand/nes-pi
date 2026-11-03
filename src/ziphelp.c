/*
 * neo-pi - extract one file from a zip held in memory (miniz)
 */
#include <stddef.h>
#include <string.h>
#include "miniz.h"

void *neopi_zip_extract (const void *pZip, size_t nZipSize, const char *pName, size_t *pSize)
{
    mz_zip_archive zip;
    memset (&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem (&zip, pZip, nZipSize, 0))
        return NULL;
    void *p = mz_zip_reader_extract_file_to_heap (&zip, pName, pSize, 0);
    mz_zip_reader_end (&zip);
    return p;
}

/* PNG of an RGB image (3 bytes per pixel); free the result with neopi_png_free */
void *neopi_png_encode (const void *pRGB, int nWidth, int nHeight, size_t *pSize)
{
    return tdefl_write_image_to_png_file_in_memory_ex (pRGB, nWidth, nHeight, 3, pSize,
                                                        6, MZ_FALSE);
}

void neopi_png_free (void *p)
{
    mz_free (p);
}
