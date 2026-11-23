/* main.c - Phase 3 test: vcreate, resched CR3 switch, kill cleanup */

#include <xinu.h>

extern uint32 pt_free;			/* defined in paging.c */

static int passed = 0, failed = 0;

#define CHECK(cond, msg)                                              \
	do {                                                          \
		if (cond) { passed++; kprintf("  PASS  %s\n", msg); } \
		else      { failed++; kprintf("  FAIL  %s\n", msg); } \
	} while (0)

/* Results reported back by the user processes */
static volatile int	u_ran = 0, u_cr3_ok = 0, u_global_ok = 0;
static volatile int	loop_bad = 0, loop_done = 0;
static volatile int	nested_ran = 0, nested_cr3_ok = 0;
static volatile pid32	nested_pid = SYSERR;
static uint32		shared_value = 0xA5A5A5A5;

/* ---- User process bodies ---- */

process simple_user(void)
{
	u_ran = 1;
	u_cr3_ok = (read_cr3() == proctab[getpid()].prpdbr) &&
		   (current_pd == proctab[getpid()].prpdbr);
	u_global_ok = (shared_value == 0xA5A5A5A5);	/* kernel data visible */
	kprintf("    [user %d] hello from a user process, CR3 = 0x%08x\n",
		getpid(), read_cr3());
	return OK;
}

process loop_user(int id)
{
	int i;
	for (i = 0; i < 5; i++) {
		if (read_cr3() != proctab[getpid()].prpdbr) {
			loop_bad++;
		}
		kprintf("    [user %d] iteration %d\n", id, i);
		sleepms(10);			/* force context switches */
	}
	loop_done++;
	return OK;
}

process sleeper_user(void)
{
	sleepms(100000);			/* main kills us long before this */
	return OK;
}

process nested_child(void)
{
	nested_ran = 1;
	nested_cr3_ok = (read_cr3() == proctab[getpid()].prpdbr);
	return OK;
}

process nested_parent(void)
{
	/* vcreate called from USER context: the CR3-switch helper must work */
	nested_pid = vcreate(nested_child, 8192, 30, "nchild", 0);
	if (nested_pid != SYSERR) {
		resume(nested_pid);
		sleepms(20);
	}
	return OK;
}

/* ---- Helpers ---- */

/* Check a user PD: 0..7 share the system PTs, everything else empty */
static int check_user_pd(uint32 pd_addr)
{
	uint32	old;
	intmask	mask;
	pd_t	*upd, *spd;
	int	i, ok = 1;
	int	nk = XINU_PAGES / 1024;

	mask = sys_pd_enter(&old);
	upd = (pd_t *)pd_addr;
	spd = (pd_t *)system_pd;
	for (i = 0; i < nk; i++) {
		if (!upd[i].pd_pres || upd[i].pd_base != spd[i].pd_base) {
			kprintf("    PDE %d does not match system PD\n", i);
			ok = 0;
		}
	}
	for (i = nk; i < 1024; i++) {
		if (upd[i].pd_pres) {
			kprintf("    PDE %d unexpectedly present\n", i);
			ok = 0;
		}
	}
	sys_pd_exit(mask, old);
	return ok;
}

process	main(void)
{
	pid32		pid, p1, p2;
	uint32		pt_before;
	struct procent	*prptr;
	int		i;

	kprintf("\n===== Phase 3: user process tests =====\n");

	/* ---- 1. vcreate sets up a correct process entry and PD ---- */
	kprintf("\n[vcreate]\n");
	pt_before = pt_free;
	pid = vcreate(simple_user, 8192, 30, "user1", 0);
	CHECK(pid != SYSERR, "vcreate returns a pid");
	prptr = &proctab[pid];
	CHECK(pt_free == pt_before - 1, "exactly one PD/PT frame used (the PD)");
	CHECK(prptr->pruser == TRUE, "pruser is TRUE");
	CHECK(prptr->prpdbr != system_pd && prptr->prpdbr >= PT_AREA_START
	      && prptr->prpdbr < FFS_START, "prpdbr is its own frame in the PD/PT area");
	CHECK(check_user_pd(prptr->prpdbr), "user PD: PDEs 0..7 shared, rest not present");
	CHECK(prptr->prvfree != NULL && prptr->prvfree->start == VHEAP_START,
	      "free list starts at VHEAP_START");
	CHECK(prptr->prvfree->npages == 1048576 - VHEAP_START / PAGE_SIZE,
	      "free list covers the whole heap (1,022,976 pages)");
	CHECK(prptr->prvfree->next == NULL, "free list has a single node");
	CHECK(allocated_virtual_pages(pid) == 0 && used_ffs_frames(pid) == 0,
	      "debug counters start at 0");
	dump_pgtables(prptr->prpdbr);

	/* ---- 2. It runs on its own PD and can still use kernel memory ---- */
	kprintf("\n[running a user process]\n");
	resume(pid);
	sleepms(50);
	CHECK(u_ran, "user process ran");
	CHECK(u_cr3_ok, "CR3 and current_pd were its own PD while it ran");
	CHECK(u_global_ok, "it could read kernel globals (shared mapping)");
	CHECK(read_cr3() == system_pd, "main is back on system_pd afterwards");

	/* ---- 3. Exiting normally frees the PD ---- */
	kprintf("\n[cleanup on normal exit]\n");
	CHECK(pt_free == pt_before, "PD frame returned after the process returned");
	CHECK(proctab[pid].prstate == PR_FREE, "process slot is free");

	/* ---- 4. Two user processes + main interleaving ---- */
	kprintf("\n[context switching between user processes]\n");
	pt_before = pt_free;
	p1 = vcreate(loop_user, 8192, 20, "loop1", 1, 1);
	p2 = vcreate(loop_user, 8192, 20, "loop2", 1, 2);
	CHECK(p1 != SYSERR && p2 != SYSERR, "two vcreates succeed");
	CHECK(proctab[p1].prpdbr != proctab[p2].prpdbr, "they get different PDs");
	resume(p1);
	resume(p2);
	for (i = 0; i < 5; i++) {
		if (read_cr3() != system_pd) loop_bad++;
		kprintf("    [main] iteration %d\n", i);
		sleepms(10);
	}
	sleepms(100);
	CHECK(loop_done == 2, "both loop processes finished");
	CHECK(loop_bad == 0, "every process always ran on its own PD");
	CHECK(pt_free == pt_before, "both PDs returned");

	/* ---- 5. Killing a sleeping user process from main ---- */
	kprintf("\n[kill another process]\n");
	pt_before = pt_free;
	pid = vcreate(sleeper_user, 8192, 20, "sleeper", 0);
	resume(pid);
	sleepms(20);				/* let it go to sleep */
	CHECK(kill(pid) == OK, "kill returns OK");
	CHECK(read_cr3() == system_pd && current_pd == system_pd,
	      "main still on its own PD after killing someone else");
	CHECK(pt_free == pt_before, "victim's PD returned");

	/* ---- 6. vcreate called from inside a user process ---- */
	kprintf("\n[vcreate from user context]\n");
	pt_before = pt_free;
	pid = vcreate(nested_parent, 8192, 20, "nparent", 0);
	resume(pid);
	sleepms(100);
	CHECK(nested_pid != SYSERR, "user process could vcreate another");
	CHECK(nested_ran && nested_cr3_ok, "nested child ran on its own PD");
	CHECK(pt_free == pt_before, "both PDs returned");

	/* ---- 7. Leak check: many create/kill cycles ---- */
	kprintf("\n[leak check: 50 vcreate + kill]\n");
	pt_before = pt_free;
	for (i = 0; i < 50; i++) {
		pid = vcreate(simple_user, 8192, 20, "cycle", 0);
		if (pid == SYSERR) { kprintf("    vcreate failed at %d\n", i); break; }
		kill(pid);			/* never resumed */
	}
	CHECK(i == 50, "all 50 vcreates succeeded");
	CHECK(pt_free == pt_before, "no PD/PT frames leaked");

	kprintf("\n===== %d passed, %d failed =====\n", passed, failed);
	return OK;
}