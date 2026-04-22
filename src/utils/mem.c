#include "mem.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Layout returned to the caller:
 *
 *   [raw from malloc]  ...padding...  [void* stash][user memory]
 *                                                  ^
 *                                                  returned pointer
 *
 * The void* stash immediately before the returned pointer holds the
 * original malloc return, so mem_free_aligned() can recover it.
 */
void *mem_alloc_offset(unsigned int size, unsigned int offset)
{
  void *raw;
  char *base;
  char *aligned;
  unsigned int cur;

  offset &= MEM_ALIGN - 1;

  raw = malloc(size + MEM_ALIGN + sizeof(void *));
  if (!raw)
    return 0;

  base = (char *)raw + sizeof(void *);
  cur = (unsigned int)((unsigned long)base & (MEM_ALIGN - 1));
  /* Advance from base to the next address where addr % MEM_ALIGN == offset */
  if (cur <= offset)
    aligned = base + (offset - cur);
  else
    aligned = base + (MEM_ALIGN - cur) + offset;

  ((void **)aligned)[-1] = raw;
  return aligned;
}

void mem_free_aligned(void *ptr)
{
  if (ptr)
    free(((void **)ptr)[-1]);
}

void mem_debug(const void *ptr, const char *name)
{
  unsigned long addr = (unsigned long)ptr;
  /* L1 set index assumes 8KB L1 with 32-byte lines (Pentium-class).
   * Print two buffers and compare L1_set values to verify they won't
   * conflict-miss. */
  printf("%s: 0x%08lX  slot=%lu  L1_set=%lu\n",
         name ? name : "buf",
         addr,
         addr & (MEM_ALIGN - 1),
         (addr >> 5) & 0x7F);
}
