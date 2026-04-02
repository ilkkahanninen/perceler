#ifndef DATA_H
#define DATA_H

/*
 * Read a chunk from the packed data file (demo.dat).
 * Returns a malloc'd buffer that the caller must free.
 * Returns 0 on error.
 */
void *data_read(unsigned long offset, unsigned long length);

#endif
