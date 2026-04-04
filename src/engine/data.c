/*
 * data.c - Read assets from the packed demo.dat file
 */

#include "data.h"

#include <stdio.h>
#include <stdlib.h>

void *data_read(Asset asset)
{
  FILE *f;
  void *buf;

  f = fopen(ASSET_DAT_FILE, "rb");
  if (!f)
    return 0;

  buf = malloc((unsigned)asset.length);
  if (!buf)
  {
    fclose(f);
    return 0;
  }

  fseek(f, (long)asset.offset, SEEK_SET);
  if (fread(buf, 1, (unsigned)asset.length, f) != asset.length)
  {
    free(buf);
    fclose(f);
    return 0;
  }

  fclose(f);
  return buf;
}
