/* paging.h */

#ifndef _PAGING_H_
#define _PAGING_H_

/* Macros */
#define TOTAL_VIRTUAL_PAGES	1048576		/* 2^32 / 4096: pages in a 32-bit address space */

#define XINU_PAGES      8192        /* number of pages used by default by Xinu               */
#define PAGE_SIZE       4096        /* number of bytes per page                              */
#define MAX_FFS_SIZE    (16*1024)   /* size of FFS space  (in frames)                        */
#define MAX_PT_SIZE     1024        /* size of space used for page tables (in frames)        */

#define PT_AREA_START    (XINU_PAGES * PAGE_SIZE)  /* starting address of page table area */
#define FFS_START        (PT_AREA_START + MAX_PT_SIZE * PAGE_SIZE)  /* starting address of FFS area */
#define VHEAP_START      (FFS_START + MAX_FFS_SIZE * PAGE_SIZE)     /* starting address of virtual heap area */
#define VALID_BIT        0x1  /* bit mask for valid bit in page table entry */


/* Globals */
extern uint32 system_pd, current_pd;  /* physical addresses of system and current page directories */

/* Structure for a page directory entry */

typedef struct {

  unsigned int pd_pres	: 1;		/* page table present?		*/
  unsigned int pd_write : 1;		/* page is writable?		*/
  unsigned int pd_user	: 1;		/* is use level protection?	*/
  unsigned int pd_pwt	: 1;		/* write through cachine for pt?*/
  unsigned int pd_pcd	: 1;		/* cache disable for this pt?	*/
  unsigned int pd_acc	: 1;		/* page table was accessed?	*/
  unsigned int pd_mbz	: 1;		/* must be zero			*/
  unsigned int pd_fmb	: 1;		/* four MB pages?		*/
  unsigned int pd_global: 1;		/* global (ignored)		*/
  unsigned int pd_avail : 3;		/* for programmer's use		*/
  unsigned int pd_base	: 20;		/* location of page table?	*/
} pd_t;

/* Structure for a page table entry */

typedef struct {

  unsigned int pt_pres	: 1;		/* page is present?		*/
  unsigned int pt_write : 1;		/* page is writable?		*/
  unsigned int pt_user	: 1;		/* is use level protection?	*/
  unsigned int pt_pwt	: 1;		/* write through for this page? */
  unsigned int pt_pcd	: 1;		/* cache disable for this page? */
  unsigned int pt_acc	: 1;		/* page was accessed?		*/
  unsigned int pt_dirty : 1;		/* page was written?		*/
  unsigned int pt_mbz	: 1;		/* must be zero			*/
  unsigned int pt_global: 1;		/* should be zero in 586	*/
  unsigned int pt_avail : 3;		/* for programmer's use		*/
  unsigned int pt_base	: 20;		/* location of page?		*/
} pt_t;

/* Structure for a virtual address */

typedef struct{
  unsigned int pg_offset : 12;		/* page offset			*/
  unsigned int pt_offset : 10;		/* page table offset		*/
  unsigned int pd_offset : 10;		/* page directory offset	*/
} virt_addr_t;

/* Structure for a physical address */

typedef struct{
  unsigned int fm_offset : 12;		/* frame offset			*/
  unsigned int fm_num : 20;		/* frame number			*/
} phy_addr_t;

/* Structure for process's virtual heap free list */

typedef struct vblock {
  uint32 start;         /* first virtual address (page aligned) */
  uint32 npages;        /* */
  struct vblock *next;  /* next free range, higher address */
} vblock_t;



/* Functions to manipulate control registers and enable paging (see control_reg.c)	 */

unsigned long read_cr0(void);

unsigned long read_cr2(void);

unsigned long read_cr3(void);

unsigned long read_cr4(void);

void write_cr0(unsigned long n);

void write_cr3(unsigned long n);

void write_cr4(unsigned long n);

void enable_paging();

#endif
