/* vmalloc.c - vmalloc */

#include <xinu.h>

char *vmalloc(uint32 nbytes)
{
	intmask		mask, pdmask;
	struct procent	*prptr;
	vblock_t	*blk, *prev;
	pd_t		*pd;
	pt_t		*pt;
	uint32		npages, start, addr, old, pt_addr, pdi, pti, i, j;

	mask = disable();
	prptr = &proctab[currpid];

	/* 1. Only user processes, and only non-zero requests */
	if (!prptr->pruser || nbytes == 0) {
		restore(mask);
		return (char *)SYSERR;
	}
	npages = nbytes / PAGE_SIZE + ((nbytes % PAGE_SIZE) != 0);  /* round up page allocation */

	/* 2. First-fit: lowest-addressed free block that is big enough */
	prev = NULL;
	for (blk = prptr->prvfree; blk != NULL; blk = blk->next) {
		if (blk->npages >= npages) {
			break;
		}
        prev = blk;
	}
	if (blk == NULL) {	/* no free block big enough on virtual heap for allocation */
		restore(mask);
		return (char *)SYSERR;
	}
	start = blk->start;

	/* 3. Page tables + valid bits */
	pdmask = sys_pd_enter(&old);
	pd = (pd_t *)prptr->prpdbr;
	for (i = 0; i < npages; i++) {
		addr = start + i * PAGE_SIZE;	/* pages virutal addresss*/
		pdi = addr >> 22;				/* relevant PD entry (points to PT)*/
		pti = (addr >> 12) & 0x3FF;		/* relevant entry within the PT */

		if (!pd[pdi].pd_pres) {			/* no page table exists for this slice yet */
			pt_addr = get_pt_frame();
			if (pt_addr == (uint32)SYSERR) {	/* failed PT allocation */
				for (j = 0; j < i; j++) {		/* undo: clear the valid bits set so far */
					addr = start + j * PAGE_SIZE;
					pt = (pt_t *)(pd[addr >> 22].pd_base << 12);
					pt[(addr >> 12) & 0x3FF].pt_avail &= ~VALID_BIT;
				}
				sys_pd_exit(pdmask, old);
				restore(mask);
				return (char *)SYSERR;
			}

			zero_frame(pt_addr);		/* clear leftover bits */
			pd[pdi].pd_pres  = 1;		/* point PD entry at PT frame */
			pd[pdi].pd_write = 1;
			pd[pdi].pd_user  = 1;
			pd[pdi].pd_base  = pt_addr >> 12;
		}
		pt = (pt_t *)(pd[pdi].pd_base << 12);	/* find actual Page Table frame */
		pt[pti].pt_avail |= VALID_BIT;			/* allocated, V=1, P stays 0 */
	}
	sys_pd_exit(pdmask, old);	/* return to current processes's page directory */

	/* 4. Take the pages off the front of the block */
	blk->start  += npages * PAGE_SIZE;
	blk->npages -= npages;
	if (blk->npages == 0) {				/* block used up: unlink it from linked list */
		if (prev == NULL) {
			prptr->prvfree = blk->next;
		} else {
			prev->next = blk->next;
		}
		freemem((char *)blk, sizeof(vblock_t));
	}

	prptr->prvpages += npages;
	restore(mask);
	return (char *)start;
}