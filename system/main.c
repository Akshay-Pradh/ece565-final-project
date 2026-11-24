/* main.c - Phase 4 test: vmalloc / vfree (no heap access) */

#include <xinu.h>

extern uint32 pt_free;			/* defined in paging.c */

static int passed = 0, failed = 0;
static volatile int tester_done = 0;

#define CHECK(cond, msg)                                              \
	do {                                                          \
		if (cond) { passed++; kprintf("  PASS  %s\n", msg); } \
		else      { failed++; kprintf("  FAIL  %s\n", msg); } \
	} while (0)

#define PG(n)		(VHEAP_START + (n) * PAGE_SIZE)	/* heap page n's address */
#define HEAP_PAGES	(TOTAL_VIRTUAL_PAGES - VHEAP_START / PAGE_SIZE)

/* ---- Inspection helpers (read tables only, never touch heap memory) ---- */

/* 0 = not allocated, 1 = allocated but no frame (lazy), 2 = has a frame */
static int page_state(uint32 addr)
{
	uint32	old;
	intmask	m;
	pd_t	*pd;
	pt_t	*pt;
	int	st = 0;

	m = sys_pd_enter(&old);
	pd = (pd_t *)proctab[currpid].prpdbr;
	if (pd[addr >> 22].pd_pres) {
		pt = (pt_t *)(pd[addr >> 22].pd_base << 12);
		if (pt[(addr >> 12) & 0x3FF].pt_pres)
			st = 2;
		else if (pt[(addr >> 12) & 0x3FF].pt_avail & VALID_BIT)
			st = 1;
	}
	sys_pd_exit(m, old);
	return st;
}

static int list_count(void)
{
	vblock_t *b;
	int n = 0;
	for (b = proctab[currpid].prvfree; b != NULL; b = b->next) n++;
	return n;
}

static vblock_t *list_node(int i)		/* i-th node, or NULL */
{
	vblock_t *b = proctab[currpid].prvfree;
	while (b != NULL && i-- > 0) b = b->next;
	return b;
}

static void print_list(void)
{
	vblock_t *b;
	kprintf("    free list:");
	for (b = proctab[currpid].prvfree; b != NULL; b = b->next)
		kprintf(" [page %d, %d pages]",
			(b->start - VHEAP_START) / PAGE_SIZE, b->npages);
	kprintf("\n");
}

/* ---- The tester (a user process) ---- */

process tester(void)
{
	pid32	me = getpid();
	char	*A, *B, *C, *D, *E, *F, *x;
	uint32	pt_before, ffs_before;
	vblock_t *n0, *n1;
	int	i, ok;

	ffs_before = free_ffs_pages();

	/* ---- 1. Basic allocation ---- */
	kprintf("\n[basic vmalloc]\n");
	CHECK(vmalloc(0) == (char *)SYSERR, "vmalloc(0) returns SYSERR");

	pt_before = pt_free;
	A = vmalloc(2 * PAGE_SIZE);
	CHECK(A == (char *)PG(0), "A (2 pages) starts at VHEAP_START");
	CHECK(pt_free == pt_before - 1, "first allocation created one page table");
	CHECK(page_state(PG(0)) == 1 && page_state(PG(1)) == 1,
	      "A's pages are valid but have no frame (lazy)");
	CHECK(page_state(PG(2)) == 0, "page after A is not allocated");

	pt_before = pt_free;
	B = vmalloc(2 * PAGE_SIZE + 1);			/* rounds up to 3 pages */
	CHECK(B == (char *)PG(2), "B (2 pages + 1 byte) comes right after A");
	CHECK(page_state(PG(4)) == 1, "B was rounded up to 3 pages");
	CHECK(pt_free == pt_before, "same 4 MB slice: no new page table");

	C = vmalloc(1);					/* rounds up to 1 page */
	CHECK(C == (char *)PG(5), "C (1 byte) gets one page at page 5");

	CHECK(allocated_virtual_pages(me) == 6, "allocated_virtual_pages = 6");
	CHECK(used_ffs_frames(me) == 0, "used_ffs_frames = 0 (nothing touched)");
	CHECK(free_ffs_pages() == ffs_before, "no FFS frames taken");
	n0 = list_node(0);
	CHECK(list_count() == 1 && n0->start == PG(6) && n0->npages == HEAP_PAGES - 6,
	      "free list: one block starting at page 6");
	print_list();

	/* ---- 2. Free, first-fit reuse, and all merge cases ---- */
	kprintf("\n[vfree, first-fit and merging]\n");
	CHECK(vfree(B, 3 * PAGE_SIZE) == OK, "vfree(B) returns OK");
	CHECK(page_state(PG(2)) == 0 && page_state(PG(4)) == 0, "B's pages cleared");
	CHECK(allocated_virtual_pages(me) == 3, "allocated_virtual_pages = 3");
	n0 = list_node(0); n1 = list_node(1);
	CHECK(list_count() == 2 && n0->start == PG(2) && n0->npages == 3
	      && n1->start == PG(6), "no-merge case: hole [2-4] + tail [6..]");
	print_list();

	D = vmalloc(PAGE_SIZE);
	CHECK(D == (char *)PG(2), "D (1 page) fills the hole first (first-fit)");
	E = vmalloc(4 * PAGE_SIZE);
	CHECK(E == (char *)PG(6), "E (4 pages) doesn't fit the 2-page hole, goes to page 6");
	print_list();

	CHECK(vfree(D, PAGE_SIZE) == OK, "vfree(D)");
	n0 = list_node(0);
	CHECK(list_count() == 2 && n0->start == PG(2) && n0->npages == 3,
	      "merge-with-next case: [2] joined [3-4] -> [2-4]");

	CHECK(vfree(C, PAGE_SIZE) == OK, "vfree(C)");
	n0 = list_node(0);
	CHECK(list_count() == 2 && n0->start == PG(2) && n0->npages == 4,
	      "merge-with-prev case: [2-4] grew to [2-5]");

	CHECK(vfree(E, 4 * PAGE_SIZE) == OK, "vfree(E)");
	n0 = list_node(0);
	CHECK(list_count() == 1 && n0->start == PG(2) && n0->npages == HEAP_PAGES - 2,
	      "merge-both case: [2-5] + [6-9] + tail -> one block from page 2");
	CHECK(allocated_virtual_pages(me) == 2, "only A left (2 pages)");
	print_list();

	/* ---- 3. Bad frees: SYSERR and nothing changes ---- */
	kprintf("\n[bad vfree calls]\n");
	CHECK(vfree(A, 0) == SYSERR, "nbytes = 0");
	CHECK(vfree(A + 1, PAGE_SIZE) == SYSERR, "pointer not page-aligned");
	CHECK(vfree((char *)0x00100000, PAGE_SIZE) == SYSERR, "pointer below the heap");
	CHECK(vfree((char *)PG(3), PAGE_SIZE) == SYSERR, "page that is free");
	CHECK(vfree((char *)0xFFFFF000, 2 * PAGE_SIZE) == SYSERR, "range runs past 4 GB");
	CHECK(vfree(A, 3 * PAGE_SIZE) == SYSERR,
	      "half-allocated range (A + one free page)");
	CHECK(page_state(PG(0)) == 1 && page_state(PG(1)) == 1,
	      "A still fully allocated after the failed free (ECE565 rule)");
	CHECK(allocated_virtual_pages(me) == 2 && list_count() == 1,
	      "counters and free list unchanged by bad calls");

	CHECK(vfree(A, 2 * PAGE_SIZE) == OK, "vfree(A) for real");
	CHECK(vfree(A, 2 * PAGE_SIZE) == SYSERR, "double free returns SYSERR");
	n0 = list_node(0);
	CHECK(list_count() == 1 && n0->start == PG(0) && n0->npages == HEAP_PAGES,
	      "heap fully free again: one block covering everything");

	/* ---- 4. Large allocation ---- */
	kprintf("\n[large allocation: 100 MB]\n");
	pt_before = pt_free;
	F = vmalloc(100 * 1024 * 1024);
	CHECK(F == (char *)PG(0), "100 MB block starts at VHEAP_START");
	kprintf("    page tables used: %d (expect 24)\n", pt_before - pt_free);
	CHECK(pt_before - pt_free == 24, "one page table per 4 MB (page 0's table already existed? see note)");
	ok = 1;
	for (i = 0; i < 25600; i += 1000) {
		if (page_state(PG(i)) != 1) { ok = 0; break; }
	}
	CHECK(ok && page_state(PG(25599)) == 1 && page_state(PG(25600)) == 0,
	      "every page valid, nothing past the end");
	CHECK(used_ffs_frames(me) == 0 && free_ffs_pages() == ffs_before,
	      "still no FFS frames used (lazy)");
	pt_before = pt_free;
	CHECK(vfree(F, 100 * 1024 * 1024) == OK, "vfree of the 100 MB block");
	CHECK(pt_free == pt_before, "page tables kept after vfree (freed only at kill)");
	CHECK(allocated_virtual_pages(me) == 0, "allocated_virtual_pages back to 0");

	/* ---- 5. Out of virtual space ---- */
	kprintf("\n[too large]\n");
	x = vmalloc(0xFFFFFFFF);			/* more than the whole heap */
	CHECK(x == (char *)SYSERR, "request bigger than the heap returns SYSERR");

	tester_done = 1;
	return OK;
}

/* ---- main (a system process) ---- */

process	main(void)
{
	uint32	pt_start;
	pid32	pid;

	kprintf("\n===== Phase 4: vmalloc / vfree tests =====\n");

	kprintf("\n[system process]\n");
	CHECK(vmalloc(PAGE_SIZE) == (char *)SYSERR, "vmalloc from a system process fails");
	CHECK(vfree((char *)VHEAP_START, PAGE_SIZE) == SYSERR,
	      "vfree from a system process fails");

	pt_start = pt_free;
	pid = vcreate(tester, 16384, 30, "tester", 0);
	resume(pid);
	while (!tester_done) {
		sleepms(10);
	}
	sleepms(50);					/* let it exit and be cleaned up */

	kprintf("\n[after the tester exits]\n");
	CHECK(pt_free == pt_start, "kill freed the PD and every heap page table");

	kprintf("\n===== %d passed, %d failed =====\n", passed, failed);
	return OK;
}