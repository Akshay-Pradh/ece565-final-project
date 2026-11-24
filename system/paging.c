/* paging.c - page table management functions */
#include <xinu.h>

#define BIT_IS_SET(map, i)  ((map)[(i) / 32] & (1u << ((i) % 32)))
#define BIT_SET(map, i)     ((map)[(i) / 32] |= (1u << ((i) % 32)))
#define BIT_CLEAR(map, i)   ((map)[(i) / 32] &= ~(1u << ((i) % 32)))
#define DUMP_MAX_PAGES	16

uint32 pt_bitmap[MAX_PT_SIZE / 32];    /* bitmap to track used/free page tables */
uint32 ffs_bitmap[MAX_FFS_SIZE / 32];  /* bitmap to track used/free frames in FFS */
uint32 pt_free, ffs_free;      /* counters for free page tables and frames */
uint32 system_pd, current_pd;  /* physical addresses of system and current page directories */

/* ============== Helper functions ============== */

void init_frame_pools(void) {
    int i;
    for (i = 0; i < MAX_PT_SIZE / 32; i++) {
        pt_bitmap[i] = 0;  /* all page tables are free */
    }
    for (i = 0; i < MAX_FFS_SIZE / 32; i++) {
        ffs_bitmap[i] = 0;  /* all frames in FFS are free */
    }
    pt_free = MAX_PT_SIZE;
    ffs_free = MAX_FFS_SIZE;
    system_pd = 0;
    current_pd = 0;
}

uint32 get_pt_frame(void) {
    /* Function to allocate a page table frame from the bitmap */
    intmask mask = disable();
    int i;

    for (i = 0; i < MAX_PT_SIZE; i++) {
        if (!BIT_IS_SET(pt_bitmap, i)) {
            BIT_SET(pt_bitmap, i);  /* mark as used */
            pt_free--;
            restore(mask);
            return PT_AREA_START + (i * PAGE_SIZE);  /* return physical address of the allocated page table frame */
        }
    }
    restore(mask);
    return (uint32)SYSERR;  /* no free page table frames available */
}

uint32 free_pt_frame(uint32 frame_addr) {
    intmask mask = disable();

    uint32 index = (frame_addr - PT_AREA_START) / PAGE_SIZE;
    if (index < MAX_PT_SIZE && BIT_IS_SET(pt_bitmap, index)) {
        BIT_CLEAR(pt_bitmap, index);  /* mark as free */
        pt_free++;
        restore(mask);
        return OK;  /* success */
    }
    restore(mask);
    return (uint32)SYSERR;  /* error: invalid frame address or already free */
}

uint32 get_ffs_frame(void) {
    /* Function to allocate a frame from the FFS area */
    intmask mask = disable();
    int i;

    for (i = 0; i < MAX_FFS_SIZE; i++) {
        if (!BIT_IS_SET(ffs_bitmap, i)) {
            BIT_SET(ffs_bitmap, i);  /* mark as used */
            ffs_free--;
            restore(mask);
            return FFS_START + (i * PAGE_SIZE);  /* return physical address of the allocated frame */
        }
    }
    restore(mask);
    return (uint32)SYSERR;  /* no free frames available */
}

uint32 free_ffs_frame(uint32 frame_addr) {
    intmask mask = disable();

    uint32 index = (frame_addr - FFS_START) / PAGE_SIZE;
    if (index < MAX_FFS_SIZE && BIT_IS_SET(ffs_bitmap, index)) {
        BIT_CLEAR(ffs_bitmap, index);  /* mark as free */
        ffs_free++;
        restore(mask);
        return OK;  /* success */
    }
    restore(mask);
    return (uint32)SYSERR;  /* error: invalid frame address or already free */
}

intmask sys_pd_enter(uint32 *old) {
    intmask mask = disable();
    *old = read_cr3();     /* read current page directory base register */
    write_cr3(system_pd);  /* switch to system page directory */
    return mask;
}

void sys_pd_exit(intmask mask, uint32 old) {
    write_cr3(old);  /* restore previous page directory base register */
    restore(mask);
}

void zero_frame(uint32 frame_addr) {
    /* Function to zero out a frame */
    memset((void *)frame_addr, 0, PAGE_SIZE);
}

uint32 free_ffs_pages(void) {
    return ffs_free;  /* return the number of free frames in FFS */
}

uint32 allocated_virtual_pages(pid32 pid) {
	return proctab[pid].prvpages;
}

uint32 used_ffs_frames(pid32 pid) {
	return proctab[pid].prffs;
}

/* ============== Main functions ============== */

void init_paging(void)
{
	int	s, i;
	uint32	pt_addr;
	pt_t	*pt;
	pd_t	*pd;

	init_frame_pools();

	system_pd = get_pt_frame();		/* system page directory */
	zero_frame(system_pd);			/* paging is off: physical access is fine */
	pd = (pd_t *)system_pd;

	/* Identity-map 0 .. VHEAP_START (Xinu region, PD/PT area, FFS) */
	for (s = 0; s < (VHEAP_START >> 22); s++) {
		pt_addr = get_pt_frame();
		zero_frame(pt_addr);		/* clear every field, including pt_avail */
		pt = (pt_t *)pt_addr;

		for (i = 0; i < 1024; i++) {
			pt[i].pt_pres  = 1;
			pt[i].pt_write = 1;
			pt[i].pt_user  = 1;
			pt[i].pt_base  = s * 1024 + i;	/* frame number = address >> 12 */
		}

		pd[s].pd_pres  = 1;
		pd[s].pd_write = 1;
		pd[s].pd_user  = 1;
		pd[s].pd_base  = pt_addr >> 12;
	}

	current_pd = system_pd;
}

/*------------------------------------------------------------------------
 * dump_pgtables - print the present entries of a page directory
 *
 *  For every present PDE:   index, virtual range, PT address,
 *                           # present PTEs, # valid-but-not-present PTEs,
 *                           first and last mapped frame
 *  For heap PDEs (>= VHEAP_START >> 22): also each valid page, up to
 *                           DUMP_MAX_PAGES per page table
 *------------------------------------------------------------------------
 */
void dump_pgtables(uint32 pd_addr)
{
	uint32	old;
	intmask	mask;
	pd_t	*pd;
	pt_t	*pt;
	int	s, i, shown;
	uint32	npres, nlazy;
	int	first, last;
	uint32	heap_pdi = VHEAP_START >> 22;

	mask = sys_pd_enter(&old);	/* PD/PT area is only mapped in the system PD */
	pd = (pd_t *)pd_addr;

	kprintf("\n--- page directory at 0x%08x ---\n", pd_addr);

	for (s = 0; s < 1024; s++) {
		if (!pd[s].pd_pres) {
			continue;
		}
		pt = (pt_t *)(pd[s].pd_base << 12);

		/* Count present and lazily-allocated entries */
		npres = nlazy = 0;
		first = last = -1;
		for (i = 0; i < 1024; i++) {
			if (pt[i].pt_pres) {
				npres++;
				if (first < 0) first = i;
				last = i;
			} else if (pt[i].pt_avail & VALID_BIT) {
				nlazy++;
			}
		}

		kprintf("PDE %4d  VA 0x%08x-0x%08x  PT 0x%08x  present %4d  lazy %4d",
			s, s << 22, (s << 22) + 0x3FFFFF,
			pd[s].pd_base << 12, npres, nlazy);
		if (first >= 0) {
			kprintf("  frames 0x%08x..0x%08x",
				pt[first].pt_base << 12, pt[last].pt_base << 12);
		}
		kprintf("\n");

		/* Heap tables: list the individual pages */
		if (s >= heap_pdi) {
			shown = 0;
			for (i = 0; i < 1024 && shown < DUMP_MAX_PAGES; i++) {
				if (pt[i].pt_pres) {
					kprintf("    page 0x%08x -> frame 0x%08x\n",
						(s << 22) + (i << 12), pt[i].pt_base << 12);
					shown++;
				} else if (pt[i].pt_avail & VALID_BIT) {
					kprintf("    page 0x%08x -> (valid, not yet mapped)\n",
						(s << 22) + (i << 12));
					shown++;
				}
			}
			if (shown == DUMP_MAX_PAGES) {
				kprintf("    ... (more not shown)\n");
			}
		}
	}

	kprintf("--- end (PT frames free: %d, FFS frames free: %d) ---\n\n",
		pt_free, ffs_free);

	sys_pd_exit(mask, old);
}

/*------------------------------------------------------------------------
 * free_user_memory - release a user process's frames, PTs, PD, free list
 *   Called from kill with interrupts disabled.
 *------------------------------------------------------------------------
 */
void free_user_memory(pid32 pid)
{
	struct procent	*prptr = &proctab[pid];
	pd_t		*pd;
	pt_t		*pt;
	struct vblock	*blk, *next;
	uint32		s, i, pt_addr;

	/* Run on the system PD from here on: the tables are reachable,
	 * and we're never running on the PD we're about to free. */
	write_cr3(system_pd);
	current_pd = system_pd;

	pd = (pd_t *)prptr->prpdbr;
	for (s = VHEAP_START >> 22; s < 1024; s++) {
		if (!pd[s].pd_pres) {
			continue;
		}
		pt_addr = pd[s].pd_base << 12;
		pt = (pt_t *)pt_addr;
		for (i = 0; i < 1024; i++) {
			if (pt[i].pt_pres) {
				free_ffs_frame(pt[i].pt_base << 12);
			}
		}
		free_pt_frame(pt_addr);		/* heap PT, owned by this process */
	}
	free_pt_frame(prptr->prpdbr);		/* the PD itself. PDEs 0..7 point at */
						/* shared PTs, which we never touch  */

	for (blk = prptr->prvfree; blk != NULL; blk = next) {
		next = blk->next;
		freemem((char *)blk, sizeof(struct vblock));
	}

	prptr->prvfree  = NULL;
	prptr->prpdbr   = 0;
	prptr->prvpages = 0;
	prptr->prffs    = 0;

	/* Killing someone else: go back to the running process's PD */
	if (pid != currpid) {
		current_pd = proctab[currpid].prpdbr;
		write_cr3(current_pd);
	}
}