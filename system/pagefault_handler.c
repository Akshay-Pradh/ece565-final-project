/* pagefault_handler.c - pagefault_handler */

#include <xinu.h>

/*------------------------------------------------------------------------
 * pagefault_handler - temporary: report the fault and halt
 *------------------------------------------------------------------------
 */
void pagefault_handler(void)
{
	kprintf("PAGE FAULT at 0x%08x\n", read_cr2());
	while (1);
}