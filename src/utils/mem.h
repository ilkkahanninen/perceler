#ifndef MEM_H
#define MEM_H

/*
 * Shared cache-stagger slots.
 *
 * All staggered buffers use MEM_ALIGN as their alignment boundary and one
 * of the slot offsets below. Slots are one cache-line (32B) apart, so
 * buffers allocated with different slots land in different L1 cache sets
 * and don't conflict-miss when read in lockstep.
 *
 * The first few slots are reserved for demo-system buffers (backbuffer
 * etc.); the rest are available for scenes.
 */
#define MEM_ALIGN 128

#define MEM_OFFSET_BACKBUFFER 0

#define MEM_OFFSET_SCENE_0 32
#define MEM_OFFSET_SCENE_1 64
#define MEM_OFFSET_SCENE_2 96

/*
 * Allocate `size` bytes so the returned pointer satisfies
 *   (addr & (MEM_ALIGN - 1)) == offset
 *
 * `offset` must be less than MEM_ALIGN — use one of the MEM_OFFSET_*
 * constants above. Free with mem_free_aligned().
 */
void *mem_alloc_offset(unsigned int size, unsigned int offset);

void mem_free_aligned(void *ptr);

/*
 * Print the address, slot offset (within MEM_ALIGN), and L1 cache set
 * index for a buffer on a single line. Call on multiple buffers to
 * verify their L1_set values differ (i.e. no conflict-miss on lockstep
 * access). Uses printf, so visible output requires either calling this
 * before vga_init() or after vga_exit().
 */
void mem_debug(const void *ptr, const char *name);

#endif
