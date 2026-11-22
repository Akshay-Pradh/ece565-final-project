/* main.c - Phase 2 test: paging enabled, identity map, system processes */

#include <xinu.h>

extern uint32 pt_free;		/* defined in paging.c */

#define TRY_FAULT 1		/* set to 1 for the last test: deliberate fault */

static int passed = 0, failed = 0;
static volatile int child_ran = 0;
static uint32 some_global = 0x12345678;

#define CHECK(cond, msg)                                              \
	do {                                                          \
		if (cond) { passed++; kprintf("  PASS  %s\n", msg); } \
		else      { failed++; kprintf("  FAIL  %s\n", msg); } \
	} while (0)

/* Walk the current page tables in software: virtual -> physical.
 * Returns 0xFFFFFFFF if the address isn't mapped. */
static uint32 translate(uint32 va)
{
	uint32	old, pa = 0xFFFFFFFF;
	intmask	mask;
	pd_t	*pd;
	pt_t	*pt;
	uint32	pdi = va >> 22, pti = (va >> 12) & 0x3FF;

	mask = sys_pd_enter(&old);
	pd = (pd_t *)old;			/* the PD that was loaded */
	if (pd[pdi].pd_pres) {
		pt = (pt_t *)(pd[pdi].pd_base << 12);
		if (pt[pti].pt_pres) {
			pa = (pt[pti].pt_base << 12) | (va & 0xFFF);
		}
	}
	sys_pd_exit(mask, old);
	return pa;
}

static process child(void)
{
	child_ran = 1;
	return OK;
}

process	main(void)
{
	uint32	old, f, *p;
	intmask	mask;
	pd_t	*pd;
	pt_t	*pt;
	int	s, i, ok;
	uint32	npde = VHEAP_START >> 22;
	pid32	cpid;

	kprintf("\n===== Phase 2: paging tests =====\n");

	/* ---- Control registers ---- */
	kprintf("\n[control registers]\n");
	CHECK(read_cr0() & 0x80000000, "CR0.PG (bit 31) is set");
	CHECK(read_cr3() == system_pd, "CR3 holds system_pd");
	CHECK(current_pd == system_pd, "current_pd == system_pd");

	/* ---- Process table ---- */
	kprintf("\n[process table]\n");
	CHECK(proctab[NULLPROC].prpdbr == system_pd, "null process uses system_pd");
	CHECK(proctab[currpid].prpdbr == system_pd, "main (made by create) uses system_pd");

	/* ---- Frame accounting ---- */
	kprintf("\n[frame accounting]\n");
	kprintf("  pt_free = %d (expect %d)\n", pt_free, MAX_PT_SIZE - 1 - npde);
	CHECK(pt_free == MAX_PT_SIZE - 1 - npde, "PD + one PT per 4 MB slice used");
	CHECK(free_ffs_pages() == MAX_FFS_SIZE, "no FFS frames used yet");

	/* ---- Table contents: full identity map, nothing else ---- */
	kprintf("\n[table contents]\n");
	mask = sys_pd_enter(&old);
	pd = (pd_t *)system_pd;
	ok = 1;
	for (s = 0; s < (int)npde && ok; s++) {
		if (!pd[s].pd_pres) { ok = 0; kprintf("  PDE %d not present\n", s); break; }
		pt = (pt_t *)(pd[s].pd_base << 12);
		for (i = 0; i < 1024; i++) {
			if (!pt[i].pt_pres || pt[i].pt_base != (uint32)(s * 1024 + i)
			    || (pt[i].pt_avail & VALID_BIT)) {
				ok = 0;
				kprintf("  PDE %d PTE %d wrong (base 0x%x)\n", s, i, pt[i].pt_base);
				break;
			}
		}
	}
	sys_pd_exit(mask, old);
	CHECK(ok, "PDEs 0..npde-1: every PTE present, page N -> frame N, valid bit clear");

	mask = sys_pd_enter(&old);
	ok = 1;
	for (s = npde; s < 1024; s++) {
		if (pd[s].pd_pres) { ok = 0; kprintf("  PDE %d unexpectedly present\n", s); break; }
	}
	sys_pd_exit(mask, old);
	CHECK(ok, "PDEs from VHEAP_START up are not present");

	/* ---- Translation spot checks ---- */
	kprintf("\n[translation]\n");
	CHECK(translate((uint32)main) == (uint32)main, "code address maps to itself");
	CHECK(translate((uint32)&some_global) == (uint32)&some_global, "global variable maps to itself");
	CHECK(translate((uint32)&ok) == (uint32)&ok, "stack variable maps to itself");
	CHECK(translate(PT_AREA_START) == PT_AREA_START, "PD/PT area start maps to itself");
	CHECK(translate(FFS_START + MAX_FFS_SIZE * PAGE_SIZE - 4) ==
	      FFS_START + MAX_FFS_SIZE * PAGE_SIZE - 4, "last word of FFS maps to itself");
	CHECK(translate(VHEAP_START) == 0xFFFFFFFF, "VHEAP_START is not mapped");

	/* ---- Memory access through paging ---- */
	kprintf("\n[memory access]\n");
	CHECK(some_global == 0x12345678, "global variable readable");
	f = get_ffs_frame();
	p = (uint32 *)f;
	p[0] = 0xCAFEBABE;
	p[PAGE_SIZE / 4 - 1] = 0xFEEDFACE;
	CHECK(p[0] == 0xCAFEBABE && p[PAGE_SIZE / 4 - 1] == 0xFEEDFACE,
	      "write/read an FFS frame with paging on");
	zero_frame(f);
	CHECK(p[0] == 0, "zero_frame works with paging on");
	free_ffs_frame(f);

	/* ---- Scheduling and interrupts still work ---- */
	kprintf("\n[processes and clock]\n");
	cpid = create(child, 1024, 20, "child", 0);
	CHECK(cpid != SYSERR, "create() works");
	CHECK(proctab[cpid].prpdbr == system_pd, "child gets system_pd");
	resume(cpid);
	sleepms(50);			/* needs the clock interrupt */
	CHECK(child_ran, "child ran and main woke up from sleep (clock + resched OK)");

	/* ---- Full dump for eyeballing ---- */
	dump_pgtables(system_pd);

	kprintf("\n===== %d passed, %d failed =====\n", passed, failed);

#if TRY_FAULT
	kprintf("\nTouching VHEAP_START on purpose; expect the stub's PAGE FAULT message...\n");
	*(volatile uint32 *)VHEAP_START = 1;
	kprintf("FAIL: no fault happened\n");
#endif

	return OK;
}