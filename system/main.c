/* main.c - Phase 1 test: frame allocators and zero_frame (paging OFF) */

#include <xinu.h>

extern uint32 pt_free, ffs_free;

static int passed = 0, failed = 0;

#define CHECK(cond, msg)                                        \
	do {                                                    \
		if (cond) { passed++; kprintf("  PASS  %s\n", msg); } \
		else      { failed++; kprintf("  FAIL  %s\n", msg); } \
	} while (0)

process	main(void)
{
	uint32	a, b, c, d, f;
	uint32	got[64];
	uint32	*p;
	int	i, ok, count;

	kprintf("\n===== Phase 1: frame allocator tests =====\n");
	init_frame_pools();

	/* ---- Starting state ---- */
	kprintf("\n[start]\n");
	CHECK(pt_free == MAX_PT_SIZE, "pt_free starts at MAX_PT_SIZE");
	CHECK(ffs_free == MAX_FFS_SIZE, "ffs_free starts at MAX_FFS_SIZE");
	CHECK(free_ffs_pages() == MAX_FFS_SIZE, "free_ffs_pages() matches");

	/* ---- PT area: basic allocate / free / reuse ---- */
	kprintf("\n[PT area]\n");
	a = get_pt_frame();
	b = get_pt_frame();
	c = get_pt_frame();
	kprintf("  got 0x%08x 0x%08x 0x%08x\n", a, b, c);
	CHECK(a == PT_AREA_START, "first PT frame is PT_AREA_START");
	CHECK(b == PT_AREA_START + PAGE_SIZE, "second PT frame is +4 KB");
	CHECK(c == PT_AREA_START + 2 * PAGE_SIZE, "third PT frame is +8 KB");
	CHECK(pt_free == MAX_PT_SIZE - 3, "pt_free dropped by 3");

	CHECK(free_pt_frame(b) == OK, "free middle PT frame returns OK");
	CHECK(pt_free == MAX_PT_SIZE - 2, "pt_free went back up by 1");
	d = get_pt_frame();
	CHECK(d == b, "next allocation reuses the freed frame");

	CHECK(free_pt_frame(b) == OK, "free it again (legit)");
	CHECK(free_pt_frame(b) == (uint32)SYSERR, "double free returns SYSERR");
	CHECK(free_pt_frame(0x00001000) == (uint32)SYSERR, "free below PT area returns SYSERR");
	CHECK(free_pt_frame(FFS_START) == (uint32)SYSERR, "free of an FFS address returns SYSERR");
	free_pt_frame(a);
	free_pt_frame(c);
	CHECK(pt_free == MAX_PT_SIZE, "pt_free back to full");

	/* ---- PT area: crossing word boundaries (bits 31/32 etc.) ---- */
	kprintf("\n[PT area: 64 frames across bitmap words]\n");
	ok = 1;
	for (i = 0; i < 64; i++) {
		got[i] = get_pt_frame();
		if (got[i] != PT_AREA_START + i * PAGE_SIZE) {
			ok = 0;
			kprintf("  frame %d: expected 0x%08x got 0x%08x\n",
				i, PT_AREA_START + i * PAGE_SIZE, got[i]);
		}
	}
	CHECK(ok, "64 frames come out in order (checks bit 31 -> bit 0 of next word)");
	for (i = 0; i < 64; i++) {
		free_pt_frame(got[i]);
	}
	CHECK(pt_free == MAX_PT_SIZE, "pt_free back to full after freeing 64");

	/* ---- PT area: exhaust the pool ---- */
	kprintf("\n[PT area: exhaust]\n");
	count = 0;
	while (get_pt_frame() != (uint32)SYSERR) {
		count++;
	}
	CHECK(count == MAX_PT_SIZE, "exactly MAX_PT_SIZE frames handed out");
	CHECK(pt_free == 0, "pt_free is 0 when empty");
	CHECK(get_pt_frame() == (uint32)SYSERR, "allocation on empty pool returns SYSERR");
	for (i = 0; i < MAX_PT_SIZE; i++) {
		free_pt_frame(PT_AREA_START + i * PAGE_SIZE);
	}
	CHECK(pt_free == MAX_PT_SIZE, "pt_free back to full after freeing all");

	/* ---- FFS: same basics ---- */
	kprintf("\n[FFS]\n");
	a = get_ffs_frame();
	b = get_ffs_frame();
	kprintf("  got 0x%08x 0x%08x\n", a, b);
	CHECK(a == FFS_START, "first FFS frame is FFS_START");
	CHECK(b == FFS_START + PAGE_SIZE, "second FFS frame is +4 KB");
	CHECK(free_ffs_pages() == MAX_FFS_SIZE - 2, "free_ffs_pages dropped by 2");
	CHECK(free_ffs_frame(a) == OK, "free first FFS frame returns OK");
	CHECK(get_ffs_frame() == a, "next allocation reuses it");
	CHECK(free_ffs_frame(PT_AREA_START) == (uint32)SYSERR, "free of a PT address returns SYSERR");

	/* ---- zero_frame on real FFS memory (paging is off, physical access) ---- */
	kprintf("\n[zero_frame]\n");
	f = b;
	p = (uint32 *)f;
	for (i = 0; i < PAGE_SIZE / 4; i++) {
		p[i] = 0xDEADBEEF;
	}
	CHECK(p[0] == 0xDEADBEEF && p[PAGE_SIZE/4 - 1] == 0xDEADBEEF,
	      "can write to FFS memory (VM has enough RAM)");
	zero_frame(f);
	ok = 1;
	for (i = 0; i < PAGE_SIZE / 4; i++) {
		if (p[i] != 0) { ok = 0; break; }
	}
	CHECK(ok, "zero_frame cleared the whole 4 KB");

	free_ffs_frame(a);
	free_ffs_frame(b);
	CHECK(free_ffs_pages() == MAX_FFS_SIZE, "FFS back to full");

	kprintf("\n===== %d passed, %d failed =====\n", passed, failed);
	return OK;
}