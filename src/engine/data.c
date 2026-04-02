/*
 * data.c - Read assets from the packed demo.dat file
 */

#include <stdio.h>
#include <stdlib.h>
#include "data.h"

#define DAT_FILE "demo.dat"

void *data_read(unsigned long offset, unsigned long length)
{
    FILE *f;
    void *buf;

    f = fopen(DAT_FILE, "rb");
    if (!f) return 0;

    buf = malloc((unsigned)length);
    if (!buf) { fclose(f); return 0; }

    fseek(f, (long)offset, SEEK_SET);
    if (fread(buf, 1, (unsigned)length, f) != length) {
        free(buf);
        fclose(f);
        return 0;
    }

    fclose(f);
    return buf;
}
