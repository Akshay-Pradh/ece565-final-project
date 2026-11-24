/* vfree.c - vfree */

#include <xinu.h>

/* Helper functions */

local void vfree_insert(struct procent *prptr, uint32 start, uint32 npages, vblock_t *newblk) {
	vblock_t	*prev, *cur;
	uint32		end;
	bool8		touch_prev, touch_next;

    prev = NULL;            /* pointer to previous block*/
    cur = prptr->prvfree;   /* pointer to next block */
    end = start + npages * PAGE_SIZE;
    
	/* find where in freelist block will sit */
	while (cur != NULL && cur->start < start) {
		prev = cur;
		cur = cur->next;
	}

    /* are we touching blocks before/after in the free list */
	touch_prev = (prev != NULL) && (prev->start + prev->npages * PAGE_SIZE == start);
	touch_next = (cur != NULL) && (end == cur->start);

	if (touch_prev && touch_next) {  /* merge: free blocks before + after */
		prev->npages += npages + cur->npages;
		prev->next = cur->next;
		freemem((char *)cur, sizeof(vblock_t));     /* merging curr into prev */
		freemem((char *)newblk, sizeof(vblock_t));  /* newblk not needed */
	} else if (touch_prev) {  /* merge: free block before */
		prev->npages += npages;
		freemem((char *)newblk, sizeof(vblock_t));
	} else if (touch_next) {  /* merge: free block after */
		cur->start   = start;
		cur->npages += npages;
		freemem((char *)newblk, sizeof(vblock_t));  /* newblk not needed */
	} else {  /* no-merge case */
		newblk->start  = start;
		newblk->npages = npages;
		newblk->next   = cur;
		if (prev == NULL) {
			prptr->prvfree = newblk;
		} else {
			prev->next = newblk;
		}
	}
}

/* Main function */

syscall vfree(char *ptr, uint32 nbytes)
{
	intmask		mask, pdmask;
	struct procent	*prptr;
	vblock_t	*newblk;
	pd_t		*pd;
	pt_t		*pt;
	uint32		start = (uint32)ptr;
	uint32		npages, addr, old, pdi, pti, i;

	mask = disable();
	prptr = &proctab[currpid];

	/* 1. Validity checks */
	if (!prptr->pruser || nbytes == 0 || (start & (PAGE_SIZE - 1)) || start < VHEAP_START) {
		restore(mask);
		return SYSERR;
	}
	npages = nbytes / PAGE_SIZE + ((nbytes % PAGE_SIZE) != 0);
	if (npages > TOTAL_VIRTUAL_PAGES - start / PAGE_SIZE) {	/* runs past 4 GB */
		restore(mask);
		return SYSERR;
	}

	/* 2. Every page must currently be allocated (valid bit set) */
	pdmask = sys_pd_enter(&old);
	pd = (pd_t *)prptr->prpdbr;
	for (i = 0; i < npages; i++) {
		addr = start + i * PAGE_SIZE;	/* pages virutal addresss*/
		pdi = addr >> 22;				/* relevant PD entry (points to PT)*/
		pti = (addr >> 12) & 0x3FF;		/* relevant entry within the PT */
		if (!pd[pdi].pd_pres) {
			sys_pd_exit(pdmask, old);
			restore(mask);
			return SYSERR;
		}
		pt = (pt_t *)(pd[pdi].pd_base << 12);
		if (!(pt[pti].pt_avail & VALID_BIT)) {
			sys_pd_exit(pdmask, old);
			restore(mask);
			return SYSERR;
		}
	}
	sys_pd_exit(pdmask, old);

	/* 3. Reserve the free-list node before changing anything */
	newblk = (vblock_t *)getmem(sizeof(vblock_t));
	if (newblk == (vblock_t *)SYSERR) {
		restore(mask);
		return SYSERR;
	}

	/* 4. Release frames and clear the PTEs */
	pdmask = sys_pd_enter(&old);
	for (i = 0; i < npages; i++) {
		addr = start + i * PAGE_SIZE;
		pt = (pt_t *)(pd[addr >> 22].pd_base << 12);
		pti = (addr >> 12) & 0x3FF;
		if (pt[pti].pt_pres) {				/* page was touched */
			free_ffs_frame(pt[pti].pt_base << 12);
			prptr->prffs--;
		}
		*(uint32 *)&pt[pti] = 0;			/* clear valid, P, everything */
	}
	sys_pd_exit(pdmask, old);	/* reloading CR3 flushes the TLB */

	/* 5. Back into the free list, merged with neighbours */
	vfree_insert(prptr, start, npages, newblk);

	prptr->prvpages -= npages;
	restore(mask);
	return OK;
}